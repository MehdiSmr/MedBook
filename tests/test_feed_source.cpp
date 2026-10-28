#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include "../feed_source/feed_source.h"

namespace fs = std::filesystem;

namespace {

// ---------- parsing ----------

FeedLine parsed(const std::string& raw, bool expectOk = true) {
    FeedLine line;
    EXPECT_EQ(parseFeedLine(raw, line), expectOk) << raw;
    return line;
}

// ---------- replay fixture ----------

class FeedReplay : public ::testing::Test {
protected:
    fs::path dir;
    std::unique_ptr<MatchingEngine> engine;
    std::vector<std::tuple<std::string, std::int32_t, std::int32_t>> trades; // symbol, qty, price

    void SetUp() override { makeEngine(64); }
    void TearDown() override {
        engine.reset();
        fs::remove_all(dir);
    }
    void makeEngine(int pool) {
        engine.reset();
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir = fs::path(::testing::TempDir()) / ("medbook_feed_" + std::string(info->name()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        engine = std::make_unique<MatchingEngine>(
            std::vector<std::string>{"SPX", "LEH", "AIG", "C", "BAC", "GS", "EUR/USD", "GOLD"}, pool, dir.string());
        engine->setTradeListener([this](const Trade& t) { trades.emplace_back(t.symbol, t.quantity, t.price); });
    }

    // Runs every line of `text` and returns the outcome of each command line.
    std::vector<RejectReason> run(FeedSource& feed) {
        std::vector<RejectReason> out;
        FeedLine line;
        while (feed.next(line)) out.push_back(feed.apply(*engine, line).reason);
        return out;
    }
    std::vector<RejectReason> run(const std::string& text) {
        std::istringstream in(text);
        FeedSource feed(in);
        return run(feed);
    }
    const OrderBook& book(const std::string& s) { return engine->getOrderBook(s); }
};

using R = RejectReason;

} // namespace

TEST(FeedParse, Add) {
    FeedLine l = parsed("1500,ADD,7,EUR/USD,SELL,25,146730");
    EXPECT_EQ(l.timestampUs, 1500u);
    EXPECT_EQ(l.action, FeedAction::ADD);
    EXPECT_EQ(l.feedId, 7u);
    EXPECT_EQ(l.symbol, "EUR/USD");
    EXPECT_EQ(l.side, Side::SELL);
    EXPECT_EQ(l.quantity, 25);
    EXPECT_EQ(l.price, 146730);
}

TEST(FeedParse, ModifyAndCancel) {
    FeedLine m = parsed("9,MODIFY,3,4,101");
    EXPECT_EQ(m.action, FeedAction::MODIFY);
    EXPECT_EQ(m.feedId, 3u);
    EXPECT_EQ(m.quantity, 4);
    EXPECT_EQ(m.price, 101);
    FeedLine c = parsed("10,CANCEL,3");
    EXPECT_EQ(c.action, FeedAction::CANCEL);
    EXPECT_EQ(c.feedId, 3u);
}

TEST(FeedParse, NegativeAndZeroNumbersParseSoTheEngineCanRejectThem) {
    EXPECT_EQ(parsed("1,ADD,1,C,BUY,-5,100").quantity, -5);
    EXPECT_EQ(parsed("1,ADD,1,C,BUY,5,0").price, 0);
}

TEST(FeedParse, MalformedLines) {
    for (const char* raw : {
             "1,ADD,1,C,BUY,5",            // too few fields
             "1,ADD,1,C,BUY,5,100,9",      // too many
             "1,ADD,1,C,HOLD,5,100",       // bad side
             "1,ADD,1,,BUY,5,100",         // empty symbol
             "1,ADD,1,C,BUY,five,100",     // not a number
             "1,ADD,1,C,BUY,5,3000000000", // price > int32
             "1,ADD,,C,BUY,5,100",         // empty id
             "1,ADD,0,C,BUY,5,100",        // id 0
             "1,FOO,46,C,BUY,5,100",       // unknown command
             "1,CANCEL",                   // missing id
             "1,CANCEL,4,5",               // extra field
             "1,MODIFY,4,5",               // missing price
             "abc,ADD,47,C,BUY,5,100",     // bad timestamp
             "ADD,48,C,BUY,5,100",         // no timestamp
             "1,ADD,1,C,BUY, 5,100",       // space inside a number
         }) {
        parsed(raw, false);
    }
}

TEST(FeedHeader, ReadsTitleDurationAndTickSizes) {
    std::istringstream in(
        "# MedBook feed \"2008 crisis, accelerated\": seed=42 rate=50000/s seconds=120\n"
        "# ts_us,ADD,id,symbol,BUY|SELL,qty,price | ts_us,MODIFY,id,qty,price | ts_us,CANCEL,id\n"
        "# tick sizes (real price = ticks * tick): SPX=0.01 EUR/USD=1e-05 GOLD=0.1\n"
        "0,ADD,1,SPX,BUY,5,100\n");
    FeedSource feed(in);
    EXPECT_EQ(feed.getTitle(), "2008 crisis, accelerated");
    EXPECT_DOUBLE_EQ(feed.getDurationSeconds(), 120);
    ASSERT_EQ(feed.getTickSizes().size(), 3u);
    EXPECT_EQ(feed.getTickSizes()[1].first, "EUR/USD");
    EXPECT_DOUBLE_EQ(feed.getTickSizes()[1].second, 1e-05);
    FeedLine line;
    ASSERT_TRUE(feed.next(line)) << "the first command, read while scanning the header, isn't lost";
    EXPECT_EQ(line.feedId, 1u);
    EXPECT_FALSE(feed.next(line));
}

TEST(FeedHeader, SkipsCommentsBlankLinesAndCarriageReturns) {
    std::istringstream in("\n# note\n1,ADD,1,C,BUY,5,100\r\n\n# more\n2,CANCEL,1\n");
    FeedSource feed(in);
    FeedLine line;
    ASSERT_TRUE(feed.next(line));
    EXPECT_FALSE(line.malformed);
    EXPECT_EQ(line.lineNumber, 3u);
    ASSERT_TRUE(feed.next(line));
    EXPECT_EQ(line.action, FeedAction::CANCEL);
    EXPECT_EQ(line.lineNumber, 6u);
    EXPECT_FALSE(feed.next(line));
}

TEST(FeedSourceFile, MissingFileThrows) {
    EXPECT_THROW(FeedSource("/nonexistent/feed.csv"), std::runtime_error);
}

// ---------- id mapping ----------

TEST_F(FeedReplay, CancelUsesTheFeedId) {
    EXPECT_EQ(run("1,ADD,100,C,BUY,5,100\n"
                  "2,CANCEL,100\n"),
              (std::vector<R>{R::NONE, R::NONE}));
    EXPECT_EQ(book("C").getOrderCount(), 0u);
}

TEST_F(FeedReplay, ModifyThatChangesTheEngineIdKeepsTheFeedIdWorking) {
    // A price change gives the order a new engine id; later lines still use feed id 7.
    EXPECT_EQ(run("1,ADD,7,GS,BUY,5,100\n"
                  "2,MODIFY,7,5,101\n"
                  "3,MODIFY,7,9,101\n"   // size up: another new engine id
                  "4,CANCEL,7\n"),
              (std::vector<R>{R::NONE, R::NONE, R::NONE, R::NONE}));
    EXPECT_EQ(book("GS").getOrderCount(), 0u);
}

TEST_F(FeedReplay, ModifyThatCrossesTradesAndFreesTheId) {
    EXPECT_EQ(run("1,ADD,1,BAC,SELL,5,410\n"
                  "2,ADD,2,BAC,BUY,5,405\n"
                  "3,MODIFY,2,5,410\n"
                  "4,CANCEL,2\n"),   // filled by the modify, so no longer live
              (std::vector<R>{R::NONE, R::NONE, R::NONE, R::UNKNOWN_ID}));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0], std::make_tuple(std::string("BAC"), 5, 410));
}

TEST_F(FeedReplay, OrdersFilledInsideTheEngineBecomeUnknown) {
    EXPECT_EQ(run("1,ADD,1,C,SELL,10,100\n"
                  "2,ADD,2,C,BUY,10,100\n"   // fills #1 completely
                  "3,CANCEL,1\n"
                  "4,MODIFY,1,5,100\n"
                  "5,CANCEL,2\n"),           // #2 never rested
              (std::vector<R>{R::NONE, R::NONE, R::UNKNOWN_ID, R::UNKNOWN_ID, R::UNKNOWN_ID}));
}

TEST_F(FeedReplay, RejectionReasons) {
    EXPECT_EQ(run("1,ADD,1,WAMU,BUY,5,100\n"   // symbol not traded
                  "2,ADD,2,C,BUY,0,100\n"      // engine: qty <= 0
                  "3,ADD,3,C,BUY,5,100\n"
                  "4,ADD,3,C,SELL,1,200\n"     // live id reused
                  "5,CANCEL,2\n"               // id 2 was rejected, never live
                  "6,MODIFY,3,5,-1\n"          // engine: price <= 0
                  "7,ADD,4,C,BUY,five,100\n"
                  "8,CANCEL,999\n"),
              (std::vector<R>{R::UNKNOWN_SYMBOL, R::ENGINE_REJECTED, R::NONE, R::DUPLICATE_ID, R::UNKNOWN_ID,
                              R::ENGINE_REJECTED, R::MALFORMED, R::UNKNOWN_ID}));
    EXPECT_EQ(book("C").getOrderCount(), 1u) << "only #3 rests";
}

TEST_F(FeedReplay, IdOfAFilledOrderCanBeReused) {
    EXPECT_EQ(run("1,ADD,1,C,SELL,5,100\n"
                  "2,ADD,2,C,BUY,5,100\n"
                  "3,ADD,1,C,BUY,3,90\n"),   // #1 is gone, so the id isn't live
              (std::vector<R>{R::NONE, R::NONE, R::NONE}));
    EXPECT_EQ(book("C").getOrderCount(), 1u);
}

TEST_F(FeedReplay, FullPoolIsReportedNotFatal) {
    makeEngine(2);
    EXPECT_EQ(run("1,ADD,1,C,BUY,5,100\n"
                  "2,ADD,2,C,BUY,5,99\n"
                  "3,ADD,3,C,BUY,5,98\n"),
              (std::vector<R>{R::NONE, R::NONE, R::POOL_FULL}));
}

TEST_F(FeedReplay, SweepDropsFilledIdsOnly) {
    std::istringstream in("1,ADD,1,C,SELL,5,100\n"
                          "2,ADD,2,C,SELL,5,101\n"
                          "3,ADD,3,C,BUY,5,100\n");   // fills #1
    FeedSource feed(in);
    run(feed);
    EXPECT_EQ(feed.getLiveIdCount(), 2u) << "#1 filled inside the engine; the map can't know yet";
    feed.sweep();
    EXPECT_EQ(feed.getLiveIdCount(), 1u);
}

// ---------- the generator's hand-written scenarios ----------

TEST_F(FeedReplay, GeneratorScenarios) {
    fs::path path = fs::path(MEDBOOK_SOURCE_DIR) / "data" / "scenarios.csv";
    if (!fs::exists(path)) GTEST_SKIP() << "run: python3 crisis_feed.py --out data";
    FeedSource feed(path.string());
    std::vector<R> outcomes = run(feed);

    std::array<int, static_cast<std::size_t>(R::COUNT)> counts{};
    for (R r : outcomes) counts[static_cast<std::size_t>(r)]++;
    // Expected results are written in the comments of scenarios.csv (S1-S14).
    EXPECT_EQ(counts[static_cast<std::size_t>(R::MALFORMED)], 9);
    EXPECT_EQ(counts[static_cast<std::size_t>(R::UNKNOWN_SYMBOL)], 1);
    EXPECT_EQ(counts[static_cast<std::size_t>(R::DUPLICATE_ID)], 1);
    EXPECT_EQ(counts[static_cast<std::size_t>(R::UNKNOWN_ID)], 5);   // S13 x4, CANCEL of rejected #37
    EXPECT_EQ(counts[static_cast<std::size_t>(R::ENGINE_REJECTED)], 7)
        << "S14 x6 plus S11: MODIFY,32,10,2000 on a partially filled order must be rejected";

    using T = std::tuple<std::string, std::int32_t, std::int32_t>;
    std::vector<T> expected = {
        {"C", 10, 100},                                              // S2
        {"C", 4, 100},                                               // S3
        {"C", 5, 100},                                               // S4
        {"GS", 5, 200}, {"GS", 5, 201}, {"GS", 5, 202},              // S5
        {"SPX", 5, 500}, {"SPX", 5, 500}, {"SPX", 2, 500},           // S6
        {"EUR/USD", 4, 300}, {"EUR/USD", 2, 300},                    // S7
        {"EUR/USD", 5, 300}, {"EUR/USD", 1, 300},                    // S8
        {"BAC", 5, 410},                                             // S9
        {"BAC", 3, 407}, {"BAC", 1, 407},                            // S10
        {"GOLD", 4, 2000},                                           // S11
    };
    EXPECT_EQ(trades, expected);
    for (const char* s : {"SPX", "AIG", "C", "BAC", "GS", "EUR/USD", "GOLD"}) {
        EXPECT_EQ(book(s).getOrderCount(), 0u) << s << ": every scenario cancels what it leaves behind";
    }
}
