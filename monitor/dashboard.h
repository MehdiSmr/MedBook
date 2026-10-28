#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "../feed_source/feed_source.h"
#include "../matching_engine/matching_engine.h"

// Fixed 10 ns buckets up to 100 µs, plus one overflow bucket; tracks the exact max.
class LatencyHistogram {
public:
    void record(std::uint64_t ns);
    std::uint64_t percentile(double p) const; // Upper edge of the bucket holding the p-th sample
    std::uint64_t getMax() const;
    std::uint64_t getCount() const;

private:
    static constexpr std::uint64_t BUCKET_NS = 10;
    static constexpr std::size_t BUCKETS = 10'000;
    std::array<std::uint64_t, BUCKETS + 1> counts{};
    std::uint64_t count = 0;
    std::uint64_t max = 0;
};

struct SymbolStats {
    std::string name;
    double tick = 1.0;
    int decimals = 0;               // Digits to print after the point, from the tick size
    std::uint64_t trades = 0;
    std::uint64_t volume = 0;       // Sum of trade quantities
    std::uint64_t notional = 0;     // Sum of quantity * price (ticks), used as a checksum
    std::int32_t firstPrice = 0;
    std::int32_t lastPrice = 0;
};

// Everything the dashboard and the final report show.
struct ReplayState {
    std::string feedPath;
    std::string title;
    std::vector<SymbolStats> symbols;
    std::uint64_t lines = 0;
    std::uint64_t trades = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(RejectReason::COUNT)> outcomes{}; // Indexed by RejectReason
    LatencyHistogram latency;
    double feedSeconds = 0;         // Timestamp of the last line processed
    double durationSeconds = 0;     // From the feed header, 0 if unknown
    double progress = 0;            // 0..1, by bytes read
    double linesPerSecond = 0;      // Wall clock
    double wallSeconds = 0;
    double speed = 1;               // 0 = as fast as possible
    double lagMs = 0;               // How far behind the feed's timestamps we are
    bool paused = false;
    bool finished = false;
    std::size_t focus = 0;          // Symbol shown in the depth panel
    std::size_t liveIds = 0;
    std::string lastReject;

    std::uint64_t rejects() const;
};

// Full-screen terminal view. Does nothing when disabled (e.g. output isn't a terminal).
class Dashboard {
public:
    explicit Dashboard(bool enabled);
    ~Dashboard();
    Dashboard(const Dashboard&) = delete;
    Dashboard& operator=(const Dashboard&) = delete;

    bool isEnabled() const;
    void render(const MatchingEngine& engine, const ReplayState& state);
    char readKey(); // 0 when no key is waiting

private:
    bool enabled;
    struct TerminalState;                 // Saved termios settings (defined in the .cpp)
    std::unique_ptr<TerminalState> saved; // Set while the terminal is in raw mode
};

std::string formatPrice(std::int32_t ticks, const SymbolStats& symbol);
std::string formatCount(std::uint64_t n);   // 1234567 -> "1,234,567"
std::string formatNs(std::uint64_t ns);     // 1234 -> "1.2µs"
int decimalsForTick(double tick);           // 0.01 -> 2, 1e-05 -> 5
// "2008 crisis" feeds compress 2008-08-31 .. 2009-03-31 into their duration; "" otherwise.
std::string crisisDate(const ReplayState& state);
void printReport(std::ostream& out, const ReplayState& state);
