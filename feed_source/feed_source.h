#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <istream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../matching_engine/matching_engine.h"
#include "../side/side.h"

// Reads a MedBook feed file (format documented in crisis_feed.py) and replays it into
// a MatchingEngine:
//   <ts_us>,ADD,<id>,<symbol>,<BUY|SELL>,<qty>,<price>
//   <ts_us>,MODIFY,<id>,<new_qty>,<new_price>
//   <ts_us>,CANCEL,<id>
// '#' lines are comments, blank lines are skipped. Ids are the feed's own client ids;
// FeedSource maps each one to the id the engine assigned.

enum class FeedAction : std::uint8_t { ADD, MODIFY, CANCEL };

// One command line, still using the feed's ids.
struct FeedLine {
    std::uint64_t lineNumber = 0;
    bool malformed = false;      // Couldn't be parsed; the other fields are meaningless
    std::uint64_t timestampUs = 0;
    FeedAction action = FeedAction::ADD;
    std::uint64_t feedId = 0;
    std::string symbol;          // ADD only
    Side side = Side::BUY;       // ADD only
    std::int32_t quantity = 0;   // ADD and MODIFY
    std::int32_t price = 0;      // ADD and MODIFY (ticks)
};

enum class RejectReason : std::uint8_t {
    NONE,             // Accepted
    MALFORMED,        // Bad line: wrong field count, non-number, out of range, bad side/command
    UNKNOWN_SYMBOL,   // ADD for a symbol the engine doesn't trade
    DUPLICATE_ID,     // ADD reusing the id of a live order
    UNKNOWN_ID,       // MODIFY/CANCEL of an id that isn't live (never added, filled or cancelled)
    ENGINE_REJECTED,  // The engine threw invalid_argument (e.g. qty <= 0, modify of a partial fill)
    POOL_FULL,        // The symbol's order pool is out of slots
    COUNT
};
const char* toString(RejectReason reason);

struct ApplyResult {
    RejectReason reason = RejectReason::NONE;
    std::uint64_t engineNs = 0; // Time spent inside MatchingEngine::processCommand (0 if not called)
};

class FeedSource {
public:
    explicit FeedSource(const std::string& path); // Throws std::runtime_error if it can't be opened
    explicit FeedSource(std::istream& in);

    // Reads the next command line into `line`. Returns false at the end of the feed.
    bool next(FeedLine& line);
    // Sends one line to the engine, translating feed ids to engine ids.
    // Always pass the same engine: the id map points into its books.
    ApplyResult apply(MatchingEngine& engine, const FeedLine& line);

    // From the header comments the generator writes (empty / 0 when the file has none).
    const std::vector<std::pair<std::string, double>>& getTickSizes() const;
    const std::string& getTitle() const;
    double getDurationSeconds() const;

    std::uint64_t getBytesRead() const;
    std::uint64_t getTotalBytes() const; // 0 when reading from a stream
    std::size_t getLiveIdCount() const;  // Size of the feed id -> engine id map
    void sweep();  // Drops ids whose orders were filled inside the engine

private:
    struct Entry {
        std::uint16_t symbol;   // Index into symbolNames / books
        std::uint64_t engineId;
    };

    std::ifstream file;
    std::istream* in;
    std::string raw;
    std::string pending;        // First command line, read while scanning the header
    bool hasPending = false;
    std::uint64_t lineNumber = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t applied = 0;

    std::vector<std::pair<std::string, double>> tickSizes;
    std::string title;
    double durationSeconds = 0;

    std::unordered_map<std::uint64_t, Entry> ids;
    std::unordered_map<std::string, std::uint16_t> symbolIndex;
    std::vector<std::string> symbolNames;
    std::vector<const OrderBook*> books;

    void readHeader();
    void parseComment(const std::string& comment);
    bool readRawLine();
    bool isLive(const Entry& entry) const;
    ApplyResult run(MatchingEngine& engine, const std::string& symbol, const Command& command,
                    std::uint64_t& engineId);
};

// Parses one command line (no comments/blank lines). Returns false if it is malformed.
bool parseFeedLine(const std::string& raw, FeedLine& line);
