#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../matching_engine/matching_engine.h"

namespace fs = std::filesystem;

namespace {

const std::string BTC = "BTC/USD";
const std::string SPX = "S&P500";

struct TradeRow {
    std::uint64_t sequence;
    std::string symbol;
    std::uint64_t restingId;
    std::uint64_t aggressorId;
    std::int32_t price;
    std::int32_t quantity;
};

// Each test gets its own folder for trade files.
class MatchingEngineTest : public ::testing::Test {
protected:
    fs::path dir;
    std::unique_ptr<MatchingEngine> engine;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir = fs::path(::testing::TempDir()) / ("medbook_" + std::string(info->name()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        engine = std::make_unique<MatchingEngine>(std::vector<std::string>{BTC, SPX}, 64, dir.string());
    }
    void TearDown() override {
        engine.reset();
        fs::remove_all(dir);
    }

    std::uint64_t add(Side side, std::int32_t price, std::int32_t qty, const std::string& sym = BTC) {
        return engine->processCommand(sym, AddCommand{side, qty, price});
    }
    std::uint64_t modify(std::uint64_t id, std::int32_t qty, std::int32_t price, const std::string& sym = BTC) {
        return engine->processCommand(sym, ModifyCommand{id, qty, price});
    }
    const OrderBook& book(const std::string& sym = BTC) { return engine->getOrderBook(sym); }

    // Destroys the engine (flushing its files) and reads one symbol's trades back.
    std::vector<TradeRow> trades(const std::string& fileName) {
        engine.reset();
        std::ifstream in(dir / fileName);
        EXPECT_TRUE(in) << "missing " << fileName;
        std::string line;
        std::getline(in, line);
        EXPECT_EQ(line, "sequence,symbol,restingId,aggressorId,price,quantity");
        std::vector<TradeRow> rows;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            TradeRow r;
            std::string field;
            std::getline(ss, field, ','); r.sequence = std::stoull(field);
            std::getline(ss, r.symbol, ',');
            std::getline(ss, field, ','); r.restingId = std::stoull(field);
            std::getline(ss, field, ','); r.aggressorId = std::stoull(field);
            std::getline(ss, field, ','); r.price = std::stoi(field);
            std::getline(ss, field, ','); r.quantity = std::stoi(field);
            rows.push_back(r);
        }
        return rows;
    }
};

} // namespace

// ---------- construction / symbols ----------

TEST(MatchingEngineSetup, DuplicateSymbolThrows) {
    auto dir = fs::path(::testing::TempDir()) / "medbook_dup";
    fs::create_directories(dir);
    EXPECT_THROW(MatchingEngine({"BTC/USD", "BTC/USD"}, 8, dir.string()), std::invalid_argument);
    fs::remove_all(dir);
}

TEST(MatchingEngineSetup, SymbolsSharingAFileNameThrow) {
    auto dir = fs::path(::testing::TempDir()) / "medbook_clash";
    fs::create_directories(dir);
    EXPECT_THROW(MatchingEngine({"BTC/USD", "BTC_USD"}, 8, dir.string()), std::invalid_argument);
    fs::remove_all(dir);
}

TEST_F(MatchingEngineTest, CreatesOneSafeTradeFilePerSymbol) {
    EXPECT_TRUE(fs::exists(dir / "trades_BTC_USD.csv"));
    EXPECT_TRUE(fs::exists(dir / "trades_S_P500.csv"));
}

TEST_F(MatchingEngineTest, UnknownSymbolThrows) {
    EXPECT_THROW(add(Side::BUY, 100, 10, "ETH/USD"), std::invalid_argument);
    EXPECT_THROW(engine->getOrderBook("ETH/USD"), std::invalid_argument);
    EXPECT_THROW(engine->processCommand("ETH/USD", CancelCommand{1}), std::invalid_argument);
}

TEST_F(MatchingEngineTest, SymbolsAreIndependentBooks) {
    add(Side::SELL, 100, 10, BTC);
    std::uint64_t buy = add(Side::BUY, 100, 10, SPX);
    EXPECT_NE(book(SPX).getOrder(buy), nullptr) << "a buy on S&P500 must not match a BTC sell";
    EXPECT_NE(book(BTC).getBestAsk(), nullptr);
    EXPECT_TRUE(trades("trades_BTC_USD.csv").empty());
}

// ---------- add without a cross ----------

TEST_F(MatchingEngineTest, AddWithoutCrossRestsAndReturnsId) {
    std::uint64_t bid = add(Side::BUY, 99, 10);
    std::uint64_t ask = add(Side::SELL, 101, 10);
    ASSERT_NE(book().getOrder(bid), nullptr);
    ASSERT_NE(book().getOrder(ask), nullptr);
    EXPECT_EQ(book().getBestBid()->getPrice(), 99);
    EXPECT_EQ(book().getBestAsk()->getPrice(), 101);
    EXPECT_TRUE(trades("trades_BTC_USD.csv").empty());
}

TEST_F(MatchingEngineTest, InvalidAddThrowsAndChangesNothing) {
    EXPECT_THROW(add(Side::BUY, 100, 0), std::invalid_argument);
    EXPECT_THROW(add(Side::BUY, 0, 10), std::invalid_argument);
    EXPECT_EQ(book().getBestBid(), nullptr);
}

// ---------- matching ----------

TEST_F(MatchingEngineTest, ExactFullFillRemovesBothAndAggressorKeepsAnId) {
    std::uint64_t ask = add(Side::SELL, 100, 10);
    std::uint64_t buy = add(Side::BUY, 100, 10);
    EXPECT_NE(buy, 0u);
    EXPECT_NE(buy, ask);
    EXPECT_EQ(book().getOrder(ask), nullptr);
    EXPECT_EQ(book().getOrder(buy), nullptr) << "a fully filled aggressor never rests";
    EXPECT_EQ(book().getBestAsk(), nullptr);
    EXPECT_EQ(book().getBestBid(), nullptr);

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].symbol, BTC);
    EXPECT_EQ(rows[0].restingId, ask);
    EXPECT_EQ(rows[0].aggressorId, buy);
    EXPECT_EQ(rows[0].price, 100);
    EXPECT_EQ(rows[0].quantity, 10);
}

TEST_F(MatchingEngineTest, PartialFillOfRestingOrderKeepsItAtTheFront) {
    std::uint64_t first = add(Side::SELL, 100, 10);
    std::uint64_t second = add(Side::SELL, 100, 10);
    add(Side::BUY, 100, 4);
    const Order* o = book().getOrder(first);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->getRemaining(), 6);
    EXPECT_EQ(book().getBestAsk()->getHead(), o) << "a partial fill keeps queue priority";
    EXPECT_EQ(book().getOrder(second)->getRemaining(), 10);
}

TEST_F(MatchingEngineTest, AggressorRemainderRestsAtItsLimit) {
    std::uint64_t ask = add(Side::SELL, 100, 5);
    std::uint64_t buy = add(Side::BUY, 101, 8);
    EXPECT_EQ(book().getOrder(ask), nullptr);
    const Order* rested = book().getOrder(buy);
    ASSERT_NE(rested, nullptr);
    EXPECT_EQ(rested->getRemaining(), 3);
    EXPECT_EQ(rested->getPrice(), 101);
    EXPECT_EQ(book().getBestBid()->getPrice(), 101);
    EXPECT_EQ(book().getBestAsk(), nullptr);

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].price, 100) << "trades execute at the resting price";
}

TEST_F(MatchingEngineTest, SweepsLevelsInPriceOrderAndStopsAtLimit) {
    std::uint64_t a100 = add(Side::SELL, 100, 5);
    std::uint64_t a103 = add(Side::SELL, 103, 5);
    std::uint64_t a101 = add(Side::SELL, 101, 5);
    std::uint64_t buy = add(Side::BUY, 102, 12);

    EXPECT_EQ(book().getOrder(a100), nullptr);
    EXPECT_EQ(book().getOrder(a101), nullptr);
    ASSERT_NE(book().getOrder(a103), nullptr) << "103 is above the 102 limit";
    EXPECT_EQ(book().getOrder(buy)->getRemaining(), 2);
    EXPECT_EQ(book().getBestBid()->getPrice(), 102);
    EXPECT_EQ(book().getBestAsk()->getPrice(), 103);

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].restingId, a100);
    EXPECT_EQ(rows[0].price, 100);
    EXPECT_EQ(rows[1].restingId, a101);
    EXPECT_EQ(rows[1].price, 101);
    EXPECT_EQ(rows[1].sequence, rows[0].sequence + 1);
}

TEST_F(MatchingEngineTest, SellSweepsBidsFromTheHighest) {
    std::uint64_t b98 = add(Side::BUY, 98, 5);
    std::uint64_t b99 = add(Side::BUY, 99, 5);
    add(Side::SELL, 98, 7);
    EXPECT_EQ(book().getOrder(b99), nullptr);
    ASSERT_NE(book().getOrder(b98), nullptr);
    EXPECT_EQ(book().getOrder(b98)->getRemaining(), 3);

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].price, 99);
    EXPECT_EQ(rows[1].price, 98);
}

TEST_F(MatchingEngineTest, SamePriceFillsOldestFirst) {
    std::uint64_t first = add(Side::SELL, 100, 5);
    std::uint64_t second = add(Side::SELL, 100, 5);
    add(Side::BUY, 100, 7);
    EXPECT_EQ(book().getOrder(first), nullptr);
    EXPECT_EQ(book().getOrder(second)->getRemaining(), 3);

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].restingId, first);
    EXPECT_EQ(rows[0].quantity, 5);
    EXPECT_EQ(rows[1].restingId, second);
    EXPECT_EQ(rows[1].quantity, 2);
}

TEST_F(MatchingEngineTest, TradeSequenceIsSharedAcrossSymbols) {
    add(Side::SELL, 100, 1, BTC);
    add(Side::BUY, 100, 1, BTC);
    add(Side::SELL, 50, 1, SPX);
    add(Side::BUY, 50, 1, SPX);
    auto btc = trades("trades_BTC_USD.csv");
    auto spx = trades("trades_S_P500.csv");
    ASSERT_EQ(btc.size(), 1u);
    ASSERT_EQ(spx.size(), 1u);
    EXPECT_EQ(btc[0].sequence, 1u);
    EXPECT_EQ(spx[0].sequence, 2u);
}

// ---------- modify ----------

TEST_F(MatchingEngineTest, ModifyPriceAcrossTheSpreadTrades) {
    std::uint64_t bid = add(Side::BUY, 99, 10);
    std::uint64_t ask = add(Side::SELL, 101, 10);
    std::uint64_t newId = modify(bid, 10, 101);
    EXPECT_NE(newId, bid);
    EXPECT_EQ(book().getOrder(bid), nullptr);
    EXPECT_EQ(book().getOrder(ask), nullptr);
    EXPECT_EQ(book().getOrder(newId), nullptr) << "fully filled, so it never rests";

    auto rows = trades("trades_BTC_USD.csv");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].aggressorId, newId);
    EXPECT_EQ(rows[0].restingId, ask);
}

TEST_F(MatchingEngineTest, ModifyPriceWithoutCrossRestsWithNewId) {
    std::uint64_t bid = add(Side::BUY, 99, 10);
    add(Side::SELL, 105, 10);
    std::uint64_t newId = modify(bid, 10, 100);
    EXPECT_NE(newId, bid);
    EXPECT_EQ(book().getOrder(bid), nullptr);
    ASSERT_NE(book().getOrder(newId), nullptr);
    EXPECT_EQ(book().getBestBid()->getPrice(), 100);
}

TEST_F(MatchingEngineTest, ModifyReduceAtSamePriceKeepsIdAndPriority) {
    std::uint64_t first = add(Side::BUY, 100, 10);
    add(Side::BUY, 100, 10);
    EXPECT_EQ(modify(first, 4, 100), first);
    EXPECT_EQ(book().getOrder(first)->getRemaining(), 4);
    EXPECT_EQ(book().getBestBid()->getHead()->getOrderId(), first);
}

TEST_F(MatchingEngineTest, ModifyIncreaseAtSamePriceGetsNewIdAtBack) {
    std::uint64_t first = add(Side::BUY, 100, 10);
    std::uint64_t second = add(Side::BUY, 100, 10);
    std::uint64_t newId = modify(first, 20, 100);
    EXPECT_NE(newId, first);
    EXPECT_EQ(book().getBestBid()->getHead()->getOrderId(), second);
    EXPECT_EQ(book().getBestBid()->getTail()->getOrderId(), newId);
}

TEST_F(MatchingEngineTest, ModifyPartiallyFilledOrderThrows) {
    std::uint64_t ask = add(Side::SELL, 100, 10);
    add(Side::BUY, 100, 3);
    EXPECT_THROW(modify(ask, 7, 102), std::invalid_argument);
    EXPECT_EQ(book().getOrder(ask)->getRemaining(), 7) << "a rejected modify changes nothing";
}

TEST_F(MatchingEngineTest, ModifyUnknownOrInvalidThrows) {
    std::uint64_t bid = add(Side::BUY, 100, 10);
    EXPECT_THROW(modify(bid + 50, 5, 100), std::invalid_argument);
    EXPECT_THROW(modify(bid, 0, 100), std::invalid_argument);
    EXPECT_THROW(modify(bid, 5, -1), std::invalid_argument);
    EXPECT_EQ(book().getOrder(bid)->getRemaining(), 10);
}

// ---------- cancel ----------

TEST_F(MatchingEngineTest, CancelRemovesOrderAndReturnsZero) {
    std::uint64_t bid = add(Side::BUY, 100, 10);
    EXPECT_EQ(engine->processCommand(BTC, CancelCommand{bid}), 0u);
    EXPECT_EQ(book().getOrder(bid), nullptr);
    EXPECT_EQ(book().getBestBid(), nullptr);
    EXPECT_THROW(engine->processCommand(BTC, CancelCommand{bid}), std::invalid_argument);
}

// ---------- print ----------

TEST_F(MatchingEngineTest, PrintShowsLevelsHighestFirst) {
    add(Side::SELL, 102, 5);
    add(Side::SELL, 101, 3);
    add(Side::SELL, 101, 2);
    add(Side::BUY, 99, 4);
    add(Side::BUY, 98, 6);
    std::ostringstream out;
    engine->printOrderBook(BTC, out);
    EXPECT_EQ(out.str(),
              "== BTC/USD ==\n"
              "ASKS (price qty orders)\n"
              "  102 5 1\n"
              "  101 5 2\n"
              "----\n"
              "BIDS (price qty orders)\n"
              "  99 4 1\n"
              "  98 6 1\n");
}

// ---------- listener ----------

TEST_F(MatchingEngineTest, TradeListenerSeesEveryTrade) {
    std::vector<Trade> seen;
    engine->setTradeListener([&](const Trade& t) { seen.push_back(t); });
    std::uint64_t a = add(Side::SELL, 100, 5);
    add(Side::SELL, 101, 5);
    std::uint64_t buy = add(Side::BUY, 101, 7);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].symbol, BTC);
    EXPECT_EQ(seen[0].restingId, a);
    EXPECT_EQ(seen[0].aggressorId, buy);
    EXPECT_EQ(seen[0].quantity, 5);
    EXPECT_EQ(seen[1].price, 101);
    EXPECT_EQ(seen[1].quantity, 2);
    EXPECT_TRUE(engine->hasSymbol(BTC));
    EXPECT_FALSE(engine->hasSymbol("ETH/USD"));
}

// ---------- traded flag on the aggressor's remainder ----------

TEST_F(MatchingEngineTest, RestOfAPartlyFilledAggressorCannotBeModified) {
    add(Side::SELL, 100, 4);
    std::uint64_t buy = add(Side::BUY, 101, 10);   // 4 fill, 6 rest
    const Order* rested = book().getOrder(buy);
    ASSERT_NE(rested, nullptr);
    EXPECT_TRUE(rested->hasTraded());
    EXPECT_THROW(modify(buy, 3, 101), std::invalid_argument) << "same-price reduce";
    EXPECT_THROW(modify(buy, 6, 99), std::invalid_argument) << "price change";
    EXPECT_EQ(book().getOrder(buy)->getRemaining(), 6);
}

TEST_F(MatchingEngineTest, AggressorThatDidNotTradeIsNotMarked) {
    std::uint64_t buy = add(Side::BUY, 99, 10);
    EXPECT_FALSE(book().getOrder(buy)->hasTraded());
}
