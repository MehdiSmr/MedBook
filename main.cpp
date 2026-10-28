// Replays a feed file through the matching engine with a live terminal dashboard.
//
//   ./build-release/main [feed.csv] [--speed X] [--no-ui] [--out DIR] [--pool N]
//                        [--expected trades.csv] [--focus SYMBOL]
//
//   --speed X     1 = real time by the feed's timestamps, 2 = twice as fast, 0 = as fast as possible
//   --no-ui       no dashboard; one progress line per second on stderr, then the report
//   --out DIR     where the engine writes trades_<symbol>.csv (default: out)
//   --pool N      order pool slots per symbol (default: 200000)
//   --expected F  expected trades to compare against (default: <feed>_trades.csv if it exists)
//   --focus SYM   symbol shown in the depth panel first
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include "feed_source/feed_source.h"
#include "matching_engine/matching_engine.h"
#include "monitor/dashboard.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

volatile std::sig_atomic_t interrupted = 0;
void onSigint(int) { interrupted = 1; }

// Used when the feed has no "# tick sizes" header (e.g. scenarios.csv).
const std::vector<std::pair<std::string, double>> DEFAULT_SYMBOLS = {
    {"SPX", 0.01}, {"LEH", 0.01}, {"AIG", 0.01}, {"C", 0.01},
    {"BAC", 0.01}, {"GS", 0.01}, {"EUR/USD", 0.00001}, {"GOLD", 0.10},
};

struct Options {
    std::string feed = "data/crisis_2008.csv";
    double speed = 1;
    bool ui = true;
    std::string out = "out";
    int pool = 200'000;
    std::string expected;
    std::string focus;
};

[[noreturn]] void usage(const char* error) {
    std::cerr << "error: " << error << "\n"
              << "usage: main [feed.csv] [--speed X] [--no-ui] [--out DIR] [--pool N] [--expected F] [--focus SYM]\n";
    std::exit(2);
}

Options parseArgs(int argc, char** argv) {
    Options o;
    bool feedSet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(("missing value for " + a).c_str());
            return argv[++i];
        };
        if (a == "--speed") o.speed = std::strtod(value().c_str(), nullptr);
        else if (a == "--no-ui") o.ui = false;
        else if (a == "--out") o.out = value();
        else if (a == "--pool") o.pool = std::atoi(value().c_str());
        else if (a == "--expected") o.expected = value();
        else if (a == "--focus") o.focus = value();
        else if (a.rfind("--", 0) == 0) usage(("unknown option " + a).c_str());
        else if (!feedSet) { o.feed = a; feedSet = true; }
        else usage("more than one feed file");
    }
    if (o.speed < 0) usage("--speed must be >= 0");
    if (o.pool <= 0) usage("--pool must be positive");
    if (o.expected.empty()) {
        fs::path guess = fs::path(o.feed).replace_extension("");
        guess += "_trades.csv";
        if (fs::exists(guess)) o.expected = guess.string();
    }
    return o;
}

// Expected trades file (written by crisis_feed.py): ts_us,symbol,qty,price,buy_id,sell_id
// Compares per-symbol trade count, volume and sum of qty*price with what the engine produced.
bool checkExpected(const std::string& path, const ReplayState& state, std::ostream& out) {
    std::ifstream in(path);
    if (!in) {
        out << "expected trades: could not open " << path << '\n';
        return false;
    }
    struct Totals { std::uint64_t trades = 0, volume = 0, notional = 0; };
    std::map<std::string, Totals> expected;
    std::string line, ts, symbol, qty, price;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::getline(fields, ts, ',');
        std::getline(fields, symbol, ',');
        std::getline(fields, qty, ',');
        std::getline(fields, price, ',');
        Totals& t = expected[symbol];
        std::uint64_t q = std::stoull(qty);
        t.trades += 1;
        t.volume += q;
        t.notional += q * std::stoull(price);
    }
    bool ok = true;
    out << "check against " << path << ":\n";
    for (const SymbolStats& sym : state.symbols) {
        Totals e = expected.count(sym.name) ? expected[sym.name] : Totals{};
        bool same = e.trades == sym.trades && e.volume == sym.volume && e.notional == sym.notional;
        ok = ok && same;
        out << "  " << sym.name << ": " << (same ? "OK" : "MISMATCH") << "  trades " << formatCount(sym.trades) << " vs "
            << formatCount(e.trades) << ", volume " << formatCount(sym.volume) << " vs " << formatCount(e.volume) << '\n';
        expected.erase(sym.name);
    }
    for (const auto& [name, e] : expected) {
        ok = false;
        out << "  " << name << ": MISMATCH  expected " << formatCount(e.trades) << " trades, symbol not traded\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    std::signal(SIGINT, onSigint);

    std::unique_ptr<FeedSource> feed;
    try {
        feed = std::make_unique<FeedSource>(opt.feed);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n(generate it with: python3 crisis_feed.py --out data)\n";
        return 1;
    }

    ReplayState state;
    state.feedPath = opt.feed;
    state.title = feed->getTitle();
    state.durationSeconds = feed->getDurationSeconds();
    state.speed = opt.speed;
    auto ticks = feed->getTickSizes().empty() ? DEFAULT_SYMBOLS : feed->getTickSizes();
    std::vector<std::string> names;
    std::unordered_map<std::string, std::size_t> indexOf;
    for (const auto& [name, tick] : ticks) {
        indexOf[name] = state.symbols.size();
        names.push_back(name);
        SymbolStats sym;
        sym.name = name;
        sym.tick = tick;
        sym.decimals = decimalsForTick(tick);
        state.symbols.push_back(sym);
    }
    if (auto it = indexOf.find(opt.focus); it != indexOf.end()) state.focus = it->second;

    fs::create_directories(opt.out);
    MatchingEngine engine(names, opt.pool, opt.out);
    engine.setTradeListener([&](const Trade& t) {
        SymbolStats& sym = state.symbols[indexOf.at(t.symbol)];
        if (sym.trades == 0) sym.firstPrice = t.price;
        sym.lastPrice = t.price;
        sym.trades += 1;
        sym.volume += static_cast<std::uint64_t>(t.quantity);
        sym.notional += static_cast<std::uint64_t>(t.quantity) * static_cast<std::uint64_t>(t.price);
        state.trades += 1;
    });

    auto dashboard = std::make_unique<Dashboard>(opt.ui && isatty(STDOUT_FILENO));
    const auto frame = std::chrono::milliseconds(50);
    const auto wallStart = Clock::now();
    auto origin = wallStart;          // Wall time at which feed time 0 is due
    auto nextFrame = wallStart;
    auto rateMark = wallStart;
    std::uint64_t rateLines = 0;
    bool quit = false;

    // Redraws the screen, handles keys and the --no-ui progress line. Called at most every 50 ms.
    auto tick = [&](Clock::time_point now) {
        double since = std::chrono::duration<double>(now - rateMark).count();
        if (since >= 0.5) {
            state.linesPerSecond = (state.lines - rateLines) / since;
            rateLines = state.lines;
            rateMark = now;
            if (!dashboard->isEnabled()) {
                std::fprintf(stderr, "[%6.1fs] %s lines  %s trades  %s rejects  p99 %s\n", state.feedSeconds,
                             formatCount(state.lines).c_str(), formatCount(state.trades).c_str(),
                             formatCount(state.rejects()).c_str(), formatNs(state.latency.percentile(0.99)).c_str());
            }
        }
        state.wallSeconds = std::chrono::duration<double>(now - wallStart).count();
        state.progress = feed->getTotalBytes() ? double(feed->getBytesRead()) / feed->getTotalBytes() : 0;
        state.liveIds = feed->getLiveIdCount();
        for (char key; (key = dashboard->readKey()) != 0;) {
            std::size_t n = state.symbols.size();
            if (key == 'q') quit = true;
            else if (key == 'n' && n) state.focus = (state.focus + 1) % n;
            else if (key == 'p' && n) state.focus = (state.focus + n - 1) % n;
            else if (key == ' ') state.paused = !state.paused;
            else if ((key == '+' || key == '=' || key == '-') && state.speed > 0) {
                state.speed = std::clamp(key == '-' ? state.speed / 2 : state.speed * 2, 0.125, 64.0);
                // Re-anchor so the current feed time is due now at the new speed
                origin = now - std::chrono::duration_cast<Clock::duration>(
                                   std::chrono::duration<double>(state.feedSeconds / state.speed));
            }
        }
        dashboard->render(engine, state);
        nextFrame = now + frame;
    };

    FeedLine line;
    while (!quit && !interrupted && feed->next(line)) {
        if (state.speed > 0 && !line.malformed) {
            // Wait until this line is due, keeping the screen alive meanwhile.
            while (!quit && !interrupted) {
                auto now = Clock::now();
                if (state.paused) {
                    auto pauseStart = now;
                    while (state.paused && !quit && !interrupted) {
                        tick(Clock::now());
                        std::this_thread::sleep_for(frame);
                    }
                    origin += Clock::now() - pauseStart;
                    continue;
                }
                auto due = origin + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(line.timestampUs / 1e6 / state.speed));
                state.lagMs = std::max(0.0, std::chrono::duration<double, std::milli>(now - due).count());
                if (now >= due) break;
                if (now >= nextFrame) tick(now);
                std::this_thread::sleep_until(std::min(due, nextFrame));
            }
            if (quit || interrupted) break;
        }

        ApplyResult result = feed->apply(engine, line);
        state.lines += 1;
        state.outcomes[static_cast<std::size_t>(result.reason)] += 1;
        if (result.engineNs) state.latency.record(result.engineNs);
        if (!line.malformed) state.feedSeconds = line.timestampUs / 1e6;
        if (result.reason != RejectReason::NONE) {
            state.lastReject = "line " + std::to_string(line.lineNumber) + ": " + toString(result.reason);
        }

        if ((state.lines & 255) == 0) {
            auto now = Clock::now();
            if (now >= nextFrame) tick(now);
        }
    }

    state.finished = !quit && !interrupted;
    tick(Clock::now());
    if (dashboard->isEnabled() && state.finished) {
        // Leave the final frame up until a key is pressed
        while (!dashboard->readKey() && !interrupted) std::this_thread::sleep_for(frame);
    }
    state.wallSeconds = std::chrono::duration<double>(Clock::now() - wallStart).count();
    dashboard.reset(); // Leave the dashboard screen before printing the report
    printReport(std::cout, state);

    int status = 0;
    if (!opt.expected.empty()) {
        if (state.finished) status = checkExpected(opt.expected, state, std::cout) ? 0 : 3;
        else std::cout << "check skipped: the replay was stopped early\n";
    }
    std::cout << "engine trades written to " << opt.out << "/trades_<symbol>.csv\n";
    return status;
}
