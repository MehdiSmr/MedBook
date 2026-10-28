#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include "feed_source.h"

namespace {

constexpr std::uint64_t SWEEP_EVERY = 1'000'000; // Lines between sweeps of filled ids

template <typename T>
bool toNumber(std::string_view text, T& out) {
    if (text.empty()) return false;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size(); // Whole field, no overflow
}

} // namespace

const char* toString(RejectReason reason) {
    switch (reason) {
        case RejectReason::NONE: return "accepted";
        case RejectReason::MALFORMED: return "malformed";
        case RejectReason::UNKNOWN_SYMBOL: return "unknown symbol";
        case RejectReason::DUPLICATE_ID: return "duplicate id";
        case RejectReason::UNKNOWN_ID: return "unknown id";
        case RejectReason::ENGINE_REJECTED: return "engine rejected";
        case RejectReason::POOL_FULL: return "pool full";
        case RejectReason::COUNT: break;
    }
    return "?";
}

bool parseFeedLine(const std::string& raw, FeedLine& line) {
    std::array<std::string_view, 8> f;
    std::size_t n = 0;
    std::string_view rest(raw);
    while (true) {
        if (n == f.size()) return false; // Too many fields
        std::size_t comma = rest.find(',');
        f[n++] = rest.substr(0, comma);
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    if (n < 3 || !toNumber(f[0], line.timestampUs)) return false;
    if (!toNumber(f[2], line.feedId) || line.feedId == 0) return false;

    if (f[1] == "ADD") {
        if (n != 7 || f[3].empty()) return false;
        line.action = FeedAction::ADD;
        line.symbol.assign(f[3]);
        if (f[4] == "BUY") line.side = Side::BUY;
        else if (f[4] == "SELL") line.side = Side::SELL;
        else return false;
        return toNumber(f[5], line.quantity) && toNumber(f[6], line.price);
    }
    if (f[1] == "MODIFY") {
        if (n != 5) return false;
        line.action = FeedAction::MODIFY;
        return toNumber(f[3], line.quantity) && toNumber(f[4], line.price);
    }
    if (f[1] == "CANCEL") {
        line.action = FeedAction::CANCEL;
        return n == 3;
    }
    return false; // Unknown command
}

FeedSource::FeedSource(const std::string& path) : file(path), in(&file) {
    if (!file) {
        throw std::runtime_error("Could not open feed: " + path);
    }
    std::error_code ec;
    totalBytes = std::filesystem::file_size(path, ec);
    if (ec) totalBytes = 0;
    readHeader();
}

FeedSource::FeedSource(std::istream& stream) : in(&stream) {
    readHeader();
}

bool FeedSource::readRawLine() {
    if (!std::getline(*in, raw)) return false;
    bytesRead += raw.size() + 1;
    ++lineNumber;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    return true;
}

void FeedSource::readHeader() {
    // Comments before the first command can carry metadata; keep the first command for next().
    while (readRawLine()) {
        if (raw.empty()) continue;
        if (raw[0] == '#') {
            parseComment(raw);
            continue;
        }
        pending = raw;
        hasPending = true;
        return;
    }
}

void FeedSource::parseComment(const std::string& comment) {
    // # MedBook feed "2008 crisis, accelerated": seed=42 rate=50000/s seconds=120
    if (title.empty()) {
        std::size_t open = comment.find('"');
        std::size_t close = open == std::string::npos ? open : comment.find('"', open + 1);
        if (close != std::string::npos) title = comment.substr(open + 1, close - open - 1);
    }
    if (std::size_t at = comment.find("seconds="); at != std::string::npos) {
        durationSeconds = std::strtod(comment.c_str() + at + 8, nullptr);
    }
    // # tick sizes (real price = ticks * tick): SPX=0.01 LEH=0.01 ... EUR/USD=1e-05
    const std::string marker = "# tick sizes";
    if (comment.rfind(marker, 0) == 0) {
        std::size_t colon = comment.find(':');
        if (colon == std::string::npos) return;
        std::istringstream words(comment.substr(colon + 1));
        std::string word;
        while (words >> word) {
            std::size_t eq = word.rfind('=');
            if (eq == std::string::npos || eq == 0) continue;
            tickSizes.emplace_back(word.substr(0, eq), std::strtod(word.c_str() + eq + 1, nullptr));
        }
    }
}

bool FeedSource::next(FeedLine& line) {
    while (true) {
        if (hasPending) {
            raw.swap(pending);
            hasPending = false;
        } else if (!readRawLine()) {
            return false;
        }
        if (raw.empty() || raw[0] == '#') continue;
        line.lineNumber = lineNumber;
        line.malformed = !parseFeedLine(raw, line);
        return true;
    }
}

bool FeedSource::isLive(const Entry& entry) const {
    return books[entry.symbol]->getOrder(entry.engineId) != nullptr;
}

ApplyResult FeedSource::run(MatchingEngine& engine, const std::string& symbol, const Command& command,
                            std::uint64_t& engineId) {
    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    RejectReason reason = RejectReason::NONE;
    try {
        engineId = engine.processCommand(symbol, command);
    } catch (const std::invalid_argument&) {
        reason = RejectReason::ENGINE_REJECTED;
    } catch (const std::runtime_error&) {
        reason = RejectReason::POOL_FULL; // OrderPool::createOrder
    }
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count();
    return {reason, static_cast<std::uint64_t>(ns)};
}

ApplyResult FeedSource::apply(MatchingEngine& engine, const FeedLine& line) {
    if (line.malformed) return {RejectReason::MALFORMED, 0};
    if (++applied % SWEEP_EVERY == 0) sweep();

    if (line.action == FeedAction::ADD) {
        auto known = symbolIndex.find(line.symbol);
        std::uint16_t sym;
        if (known != symbolIndex.end()) {
            sym = known->second;
        } else {
            if (!engine.hasSymbol(line.symbol)) return {RejectReason::UNKNOWN_SYMBOL, 0};
            sym = static_cast<std::uint16_t>(symbolNames.size());
            symbolIndex.emplace(line.symbol, sym);
            symbolNames.push_back(line.symbol);
            books.push_back(&engine.getOrderBook(line.symbol));
        }

        auto existing = ids.find(line.feedId);
        if (existing != ids.end()) {
            if (isLive(existing->second)) return {RejectReason::DUPLICATE_ID, 0};
            ids.erase(existing); // Old order already filled; the id is free again
        }

        std::uint64_t engineId = 0;
        ApplyResult result = run(engine, symbolNames[sym], AddCommand{line.side, line.quantity, line.price}, engineId);
        // Only remember orders that rest; one that filled completely can't be modified or cancelled.
        if (result.reason == RejectReason::NONE && books[sym]->getOrder(engineId) != nullptr) {
            ids.emplace(line.feedId, Entry{sym, engineId});
        }
        return result;
    }

    // MODIFY / CANCEL: these lines have no symbol, so the map supplies it.
    auto it = ids.find(line.feedId);
    if (it == ids.end()) return {RejectReason::UNKNOWN_ID, 0};
    if (!isLive(it->second)) { // Filled inside the engine since we last looked
        ids.erase(it);
        return {RejectReason::UNKNOWN_ID, 0};
    }
    Entry& entry = it->second;
    const std::string& symbol = symbolNames[entry.symbol];

    if (line.action == FeedAction::CANCEL) {
        std::uint64_t ignored = 0;
        ApplyResult result = run(engine, symbol, CancelCommand{entry.engineId}, ignored);
        if (result.reason == RejectReason::NONE) ids.erase(it);
        return result;
    }

    std::uint64_t newId = 0;
    ApplyResult result = run(engine, symbol, ModifyCommand{entry.engineId, line.quantity, line.price}, newId);
    if (result.reason == RejectReason::NONE) {
        // A price change or size increase gets a new engine id; it may also have filled completely.
        entry.engineId = newId;
        if (!isLive(entry)) ids.erase(it);
    }
    return result;
}

void FeedSource::sweep() {
    std::erase_if(ids, [this](const auto& kv) { return !isLive(kv.second); });
}

const std::vector<std::pair<std::string, double>>& FeedSource::getTickSizes() const { return tickSizes; }
const std::string& FeedSource::getTitle() const { return title; }
double FeedSource::getDurationSeconds() const { return durationSeconds; }
std::uint64_t FeedSource::getBytesRead() const { return bytesRead; }
std::uint64_t FeedSource::getTotalBytes() const { return totalBytes; }
std::size_t FeedSource::getLiveIdCount() const { return ids.size(); }
