#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include "dashboard.h"
#include "../price_level/price_level.h"

// ---------- latency ----------

void LatencyHistogram::record(std::uint64_t ns) {
    counts[std::min<std::uint64_t>(ns / BUCKET_NS, BUCKETS)]++;
    ++count;
    max = std::max(max, ns);
}

std::uint64_t LatencyHistogram::percentile(double p) const {
    if (count == 0) return 0;
    auto target = static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(count)));
    target = std::max<std::uint64_t>(target, 1);
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < BUCKETS; ++i) {
        seen += counts[i];
        if (seen >= target) return std::min(max, (i + 1) * BUCKET_NS);
    }
    return max; // In the overflow bucket
}

std::uint64_t LatencyHistogram::getMax() const { return max; }
std::uint64_t LatencyHistogram::getCount() const { return count; }

std::uint64_t ReplayState::rejects() const {
    std::uint64_t total = 0;
    for (std::size_t i = 1; i < outcomes.size(); ++i) total += outcomes[i]; // [0] is accepted
    return total;
}

// ---------- formatting ----------

int decimalsForTick(double tick) {
    int decimals = 0;
    while (decimals < 9 && std::fabs(tick - std::round(tick)) > 1e-9) {
        tick *= 10;
        ++decimals;
    }
    return decimals;
}

std::string formatPrice(std::int32_t ticks, const SymbolStats& symbol) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", symbol.decimals, ticks * symbol.tick);
    return buf;
}

std::string formatCount(std::uint64_t n) {
    std::string digits = std::to_string(n), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string formatNs(std::uint64_t ns) {
    char buf[32];
    if (ns < 1000) std::snprintf(buf, sizeof buf, "%lluns", static_cast<unsigned long long>(ns));
    else if (ns < 1'000'000) std::snprintf(buf, sizeof buf, "%.1fµs", ns / 1e3);
    else std::snprintf(buf, sizeof buf, "%.1fms", ns / 1e6);
    return buf;
}

std::string crisisDate(const ReplayState& state) {
    if (state.durationSeconds <= 0 || state.title.find("2008 crisis") == std::string::npos) return "";
    using namespace std::chrono;
    const sys_days start = 2008y / August / 31;
    const auto totalDays = (sys_days{2009y / March / 31} - start).count();
    double fraction = std::clamp(state.feedSeconds / state.durationSeconds, 0.0, 1.0);
    year_month_day day{start + days{static_cast<int>(fraction * totalDays)}};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(day.year()),
                  static_cast<unsigned>(day.month()), static_cast<unsigned>(day.day()));
    return buf;
}

namespace {

const char* RESET = "\x1b[0m";
const char* BOLD = "\x1b[1m";
const char* DIM = "\x1b[2m";
const char* RED = "\x1b[31m";
const char* GREEN = "\x1b[32m";
const char* YELLOW = "\x1b[33m";
const char* CYAN = "\x1b[36m";
const char* EOL = "\x1b[K\n"; // Clear the rest of the line, so shorter redraws leave no leftovers
constexpr std::size_t DEPTH_ROWS = 8;
constexpr int BAR_WIDTH = 28;

std::string pad(const std::string& s, std::size_t width, bool right = true) {
    // Pads by visible characters; "µ" and "—" are multi-byte in UTF-8.
    std::size_t visible = 0;
    for (unsigned char c : s) visible += (c & 0xC0) != 0x80;
    if (visible >= width) return s;
    std::string fill(width - visible, ' ');
    return right ? fill + s : s + fill;
}

std::int32_t levelQuantity(const PriceLevel* level) {
    std::int32_t total = 0;
    for (const Order* o = level->getHead(); o; o = o->getNext()) total += o->getRemaining();
    return total;
}

std::string bar(std::int32_t qty, std::int32_t maxQty) {
    int n = maxQty > 0 ? static_cast<int>(std::lround(BAR_WIDTH * static_cast<double>(qty) / maxQty)) : 0;
    n = std::clamp(n, qty > 0 ? 1 : 0, BAR_WIDTH);
    std::string out;
    for (int i = 0; i < n; ++i) out += "█";
    return out;
}

} // namespace

// ---------- terminal ----------

struct Dashboard::TerminalState {
    termios original;
};

Dashboard::Dashboard(bool enable) : enabled(enable) {
    if (!enabled) return;
    termios original{};
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original) == 0) {
        termios raw = original;
        raw.c_lflag &= ~(ICANON | ECHO); // Keys arrive one at a time and aren't echoed; Ctrl-C still works
        raw.c_cc[VMIN] = 0;              // read() returns right away when no key is waiting
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            saved = std::make_unique<TerminalState>(TerminalState{original});
        }
    }
    std::fputs("\x1b[?1049h\x1b[?25l", stdout); // Alternate screen, hide cursor
    std::fflush(stdout);
}

Dashboard::~Dashboard() {
    if (!enabled) return;
    std::fputs("\x1b[?25h\x1b[?1049l", stdout); // Show cursor, back to the normal screen
    std::fflush(stdout);
    if (saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved->original);
}

bool Dashboard::isEnabled() const { return enabled; }

char Dashboard::readKey() {
    if (!saved) return 0;
    char c = 0;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : 0;
}

// ---------- drawing ----------

void Dashboard::render(const MatchingEngine& engine, const ReplayState& s) {
    if (!enabled) return;
    std::string f = "\x1b[H"; // Cursor home; every line overwrites the previous frame
    char buf[256];

    f += std::string(BOLD) + " MedBook" + RESET + "  " + (s.title.empty() ? s.feedPath : s.title);
    if (s.finished) f += std::string("  ") + GREEN + "[DONE]" + RESET;
    else if (s.paused) f += std::string("  ") + YELLOW + "[PAUSED]" + RESET;
    f += EOL;

    // Progress line
    int filled = static_cast<int>(std::lround(s.progress * 24));
    std::string progressBar;
    for (int i = 0; i < 24; ++i) progressBar += i < filled ? "█" : "░";
    std::snprintf(buf, sizeof buf, " feed %6.1fs", s.feedSeconds);
    f += buf;
    if (s.durationSeconds > 0) {
        std::snprintf(buf, sizeof buf, " / %.0fs", s.durationSeconds);
        f += buf;
    }
    std::snprintf(buf, sizeof buf, "  %s %3.0f%%", progressBar.c_str(), s.progress * 100);
    f += buf;
    if (std::string date = crisisDate(s); !date.empty()) f += std::string("   ") + CYAN + "≈ " + date + RESET;
    if (s.speed > 0) std::snprintf(buf, sizeof buf, "   speed %gx", s.speed);
    else std::snprintf(buf, sizeof buf, "   speed max");
    f += buf;
    if (s.speed > 0 && s.lagMs > 100) {
        std::snprintf(buf, sizeof buf, "  %slag %.0fms%s", RED, s.lagMs, RESET);
        f += buf;
    }
    f += EOL;

    // Throughput and latency
    std::snprintf(buf, sizeof buf, " msgs %s (%.1fk/s)   trades %s   rejects %s   live ids %s",
                  formatCount(s.lines).c_str(), s.linesPerSecond / 1e3, formatCount(s.trades).c_str(),
                  formatCount(s.rejects()).c_str(), formatCount(s.liveIds).c_str());
    f += buf;
    f += EOL;
    f += " latency  p50 " + formatNs(s.latency.percentile(0.50)) + "  p99 " + formatNs(s.latency.percentile(0.99)) +
         "  p99.9 " + formatNs(s.latency.percentile(0.999)) + "  max " + formatNs(s.latency.getMax()) +
         DIM + "   (MatchingEngine::processCommand)" + RESET + EOL;
    f += EOL;

    // One row per symbol
    f += std::string(DIM) + "   SYMBOL           BID           ASK    SPREAD          LAST      CHG     TRADES  RESTING" +
         RESET + EOL;
    for (std::size_t i = 0; i < s.symbols.size(); ++i) {
        const SymbolStats& sym = s.symbols[i];
        const OrderBook& book = engine.getOrderBook(sym.name);
        const PriceLevel* bid = book.getBestBid();
        const PriceLevel* ask = book.getBestAsk();
        std::string bidText = bid ? formatPrice(bid->getPrice(), sym) : "—";
        std::string askText = ask ? formatPrice(ask->getPrice(), sym) : "—";
        std::string spread = (bid && ask) ? formatPrice(ask->getPrice() - bid->getPrice(), sym) : "—";
        std::string last = sym.trades ? formatPrice(sym.lastPrice, sym) : "—";
        std::string change = "—";
        const char* changeColor = DIM;
        if (sym.trades && sym.firstPrice > 0) {
            double pct = 100.0 * (sym.lastPrice - sym.firstPrice) / sym.firstPrice;
            std::snprintf(buf, sizeof buf, "%+.1f%%", pct);
            change = buf;
            changeColor = pct < 0 ? RED : GREEN;
        }
        f += i == s.focus ? std::string(BOLD) + " > " : std::string("   ");
        f += pad(sym.name, 8, false) + RESET + " " + GREEN + pad(bidText, 12) + RESET + "  " + RED + pad(askText, 12) +
             RESET + pad(spread, 10) + pad(last, 14) + " " + changeColor + pad(change, 8) + RESET +
             pad(formatCount(sym.trades), 11) + pad(formatCount(book.getOrderCount()), 9) + EOL;
    }
    f += EOL;

    // Depth ladder for the focused symbol
    if (!s.symbols.empty()) {
        const SymbolStats& sym = s.symbols[s.focus];
        const OrderBook& book = engine.getOrderBook(sym.name);
        auto asks = book.getLevels(Side::SELL, DEPTH_ROWS); // Lowest first
        auto bids = book.getLevels(Side::BUY, DEPTH_ROWS);  // Highest first
        std::int32_t maxQty = 0;
        for (auto* l : asks) maxQty = std::max(maxQty, levelQuantity(l));
        for (auto* l : bids) maxQty = std::max(maxQty, levelQuantity(l));

        f += std::string(BOLD) + " DEPTH " + sym.name + RESET + DIM +
             "      price         qty  orders     [n/p] symbol  [+/-] speed  [space] pause  [q] quit" + RESET + EOL;
        auto row = [&](const PriceLevel* level, const char* color) {
            std::int32_t qty = levelQuantity(level);
            f += std::string("   ") + color + pad(formatPrice(level->getPrice(), sym), 18) + RESET + pad(formatCount(qty), 12) +
                 pad(std::to_string(level->getDepth()), 8) + "  " + color + bar(qty, maxQty) + RESET + EOL;
        };
        for (std::size_t i = asks.size(); i < DEPTH_ROWS; ++i) f += EOL; // Keep the spread line in place
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) row(*it, RED);
        std::string spread = (!asks.empty() && !bids.empty())
                                 ? "spread " + formatPrice(asks.front()->getPrice() - bids.front()->getPrice(), sym)
                                 : (asks.empty() && bids.empty() ? "book empty" : "one side empty");
        f += std::string("   ") + DIM + "──────────────── " + spread + " ────────────────" + RESET + EOL;
        for (auto* l : bids) row(l, GREEN);
        for (std::size_t i = bids.size(); i < DEPTH_ROWS; ++i) f += EOL;
    }
    f += EOL;
    if (!s.lastReject.empty()) f += std::string(" ") + DIM + "last reject: " + s.lastReject + RESET + EOL;
    f += "\x1b[J"; // Clear anything below
    std::fwrite(f.data(), 1, f.size(), stdout);
    std::fflush(stdout);
}

// ---------- final report ----------

void printReport(std::ostream& out, const ReplayState& s) {
    out << "\n== MedBook replay: " << (s.title.empty() ? s.feedPath : s.title) << " ==\n";
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s lines in %.2fs wall (%.0f lines/s), feed time %.1fs%s\n",
                  formatCount(s.lines).c_str(), s.wallSeconds,
                  s.wallSeconds > 0 ? s.lines / s.wallSeconds : 0.0, s.feedSeconds,
                  s.finished ? "" : "  (stopped early)");
    out << buf;
    out << "trades " << formatCount(s.trades) << ", accepted "
        << formatCount(s.outcomes[static_cast<std::size_t>(RejectReason::NONE)]) << ", rejected "
        << formatCount(s.rejects()) << '\n';
    for (std::size_t i = 1; i < s.outcomes.size(); ++i) {
        if (s.outcomes[i]) out << "  " << pad(toString(static_cast<RejectReason>(i)), 16, false) << formatCount(s.outcomes[i]) << '\n';
    }
    out << "processCommand latency: p50 " << formatNs(s.latency.percentile(0.50)) << ", p90 "
        << formatNs(s.latency.percentile(0.90)) << ", p99 " << formatNs(s.latency.percentile(0.99)) << ", p99.9 "
        << formatNs(s.latency.percentile(0.999)) << ", max " << formatNs(s.latency.getMax()) << '\n';
    out << pad("symbol", 9, false) << pad("trades", 11) << pad("volume", 14) << pad("first", 14) << pad("last", 14) << '\n';
    for (const SymbolStats& sym : s.symbols) {
        out << pad(sym.name, 9, false) << pad(formatCount(sym.trades), 11) << pad(formatCount(sym.volume), 14)
            << pad(sym.trades ? formatPrice(sym.firstPrice, sym) : "—", 14)
            << pad(sym.trades ? formatPrice(sym.lastPrice, sym) : "—", 14) << '\n';
    }
}
