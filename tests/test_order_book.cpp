#include <gtest/gtest.h>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>
#include "../order_book/order_book.h"

namespace {

std::uint64_t add(OrderBook& book, Side side, std::int32_t price, std::int32_t qty = 10) {
    return book.addOrder(qty, price, side);
}

// Orders resting at the level that holds `id`, head to tail.
std::vector<std::uint64_t> queueOf(const OrderBook& book, std::uint64_t id) {
    std::vector<std::uint64_t> ids;
    Order* o = book.getOrder(id);
    if (!o || !o->getLevel()) return ids;
    for (Order* it = o->getLevel()->getHead(); it; it = it->getNext()) ids.push_back(it->getOrderId());
    return ids;
}

} // namespace

// ---------- empty book ----------

TEST(OrderBook, EmptyBookHasNoBestBidOrAsk) {
    OrderBook book(16);
    EXPECT_EQ(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestAsk(), nullptr);
}

TEST(OrderBook, GetUnknownOrderReturnsNull) {
    OrderBook book(16);
    EXPECT_EQ(book.getOrder(12345), nullptr);
}

// ---------- add ----------

TEST(OrderBook, AddReturnsUniqueIdsAndStoresOrder) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 5);
    std::uint64_t b = add(book, Side::SELL, 110, 7);
    EXPECT_NE(a, b);
    Order* oa = book.getOrder(a);
    ASSERT_NE(oa, nullptr);
    EXPECT_EQ(oa->getOrderId(), a);
    EXPECT_EQ(oa->getPrice(), 100);
    EXPECT_EQ(oa->getQuantity(), 5);
    EXPECT_EQ(oa->getSide(), Side::BUY);
    ASSERT_NE(oa->getLevel(), nullptr) << "a resting order must belong to a level";
    EXPECT_EQ(oa->getLevel()->getPrice(), 100);
}

TEST(OrderBook, AddRejectsNonPositiveInput) {
    OrderBook book(16);
    EXPECT_THROW(book.addOrder(0, 100, Side::BUY), std::invalid_argument);
    EXPECT_THROW(book.addOrder(10, 0, Side::BUY), std::invalid_argument);
    EXPECT_THROW(book.addOrder(-5, 100, Side::BUY), std::invalid_argument);
}

TEST(OrderBook, FirstBuySetsBestBid) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 100);
    EXPECT_EQ(book.getBestBid()->getHead()->getOrderId(), a);
    EXPECT_EQ(book.getBestAsk(), nullptr) << "a buy must not create an ask";
}

TEST(OrderBook, FirstSellSetsBestAsk) {
    OrderBook book(16);
    add(book, Side::SELL, 110);
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 110);
    EXPECT_EQ(book.getBestBid(), nullptr) << "a sell must not create a bid";
}

TEST(OrderBook, BestBidIsHighestBuyPrice) {
    OrderBook book(16);
    add(book, Side::BUY, 100);
    add(book, Side::BUY, 105);
    add(book, Side::BUY, 103);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 105);
}

TEST(OrderBook, BestAskIsLowestSellPrice) {
    OrderBook book(16);
    add(book, Side::SELL, 110);
    add(book, Side::SELL, 107);
    add(book, Side::SELL, 109);
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 107);
}

TEST(OrderBook, SameSideSamePriceSharesOneLevelInFifoOrder) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    std::uint64_t b = add(book, Side::BUY, 100);
    std::uint64_t c = add(book, Side::BUY, 100);
    EXPECT_EQ(book.getOrder(a)->getLevel(), book.getOrder(c)->getLevel());
    EXPECT_EQ(book.getOrder(a)->getLevel()->getDepth(), 3);
    EXPECT_EQ(queueOf(book, a), (std::vector<std::uint64_t>{a, b, c}));
}

TEST(OrderBook, BuyAndSellAtSamePriceUseDifferentLevels) {
    OrderBook book(16);
    std::uint64_t b = add(book, Side::BUY, 100);
    std::uint64_t s = add(book, Side::SELL, 100);
    EXPECT_NE(book.getOrder(b)->getLevel(), book.getOrder(s)->getLevel());
}

TEST(OrderBook, FullPoolRejectsAddWithoutCorruptingBook) {
    OrderBook book(2);
    add(book, Side::BUY, 100);
    add(book, Side::BUY, 101);
    EXPECT_THROW(add(book, Side::BUY, 99), std::runtime_error);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 101);
}

// ---------- remove ----------

TEST(OrderBook, RemoveOrderMakesItUnreachable) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    book.removeOrder(a);
    EXPECT_EQ(book.getOrder(a), nullptr);
}

TEST(OrderBook, RemoveUnknownIdThrows) {
    OrderBook book(16);
    EXPECT_THROW(book.removeOrder(999), std::invalid_argument);
}

TEST(OrderBook, RemoveTwiceThrows) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    book.removeOrder(a);
    EXPECT_THROW(book.removeOrder(a), std::invalid_argument);
}

TEST(OrderBook, RemoveOneOfManyAtLevelKeepsOthersInOrder) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 110);
    std::uint64_t b = add(book, Side::SELL, 110);
    std::uint64_t c = add(book, Side::SELL, 110);
    book.removeOrder(b);
    EXPECT_EQ(queueOf(book, a), (std::vector<std::uint64_t>{a, c}));
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 110);
}

TEST(OrderBook, RemovingLastOrderAtBestBidMovesBestToNextLevel) {
    OrderBook book(16);
    add(book, Side::BUY, 100);
    std::uint64_t top = add(book, Side::BUY, 105);
    book.removeOrder(top);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 100);
}

TEST(OrderBook, RemovingLastOrderAtBestAskMovesBestToNextLevel) {
    OrderBook book(16);
    add(book, Side::SELL, 110);
    std::uint64_t top = add(book, Side::SELL, 107);
    book.removeOrder(top);
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 110);
}

TEST(OrderBook, RemovingNonBestLevelKeepsBest) {
    OrderBook book(16);
    std::uint64_t low = add(book, Side::BUY, 100);
    add(book, Side::BUY, 105);
    book.removeOrder(low);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 105);
}

TEST(OrderBook, RemovingEverythingEmptiesBothSides) {
    OrderBook book(16);
    std::vector<std::uint64_t> ids = {add(book, Side::BUY, 100), add(book, Side::BUY, 101),
                                      add(book, Side::SELL, 110), add(book, Side::SELL, 111)};
    for (auto id : ids) book.removeOrder(id);
    EXPECT_EQ(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestAsk(), nullptr);
}

TEST(OrderBook, PriceLevelCanBeRecreatedAfterItEmpties) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    book.removeOrder(a);
    std::uint64_t b = add(book, Side::BUY, 100);
    ASSERT_NE(book.getOrder(b)->getLevel(), nullptr);
    EXPECT_EQ(book.getOrder(b)->getLevel()->getDepth(), 1);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 100);
}

TEST(OrderBook, RemovedOrderFreesPoolSlot) {
    OrderBook book(1);
    for (int i = 0; i < 1000; ++i) {
        std::uint64_t id = add(book, i % 2 ? Side::BUY : Side::SELL, 100 + i % 7);
        book.removeOrder(id);
    }
    SUCCEED();
}

// ---------- modify ----------

TEST(OrderBook, ModifyUnknownIdThrows) {
    OrderBook book(16);
    EXPECT_THROW(book.modifyOrder(999, 5, 100), std::invalid_argument);
}

TEST(OrderBook, ModifyRejectsNonPositiveInput) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    EXPECT_THROW(book.modifyOrder(a, 0, 100), std::invalid_argument);
    EXPECT_THROW(book.modifyOrder(a, 5, 0), std::invalid_argument);
}

TEST(OrderBook, ModifyWithNoChangeKeepsIdAndPosition) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    std::uint64_t b = add(book, Side::BUY, 100, 10);
    EXPECT_EQ(book.modifyOrder(a, 10, 100), a);
    EXPECT_EQ(queueOf(book, a), (std::vector<std::uint64_t>{a, b}));
}

TEST(OrderBook, ModifyReduceQuantityKeepsIdAndPriority) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    std::uint64_t b = add(book, Side::BUY, 100, 10);
    std::uint64_t r = book.modifyOrder(a, 4, 100);
    EXPECT_EQ(r, a);
    ASSERT_NE(book.getOrder(a), nullptr);
    EXPECT_EQ(book.getOrder(a)->getQuantity(), 4);
    EXPECT_EQ(book.getOrder(a)->getRemaining(), 4);
    EXPECT_EQ(queueOf(book, a), (std::vector<std::uint64_t>{a, b})) << "a reduce keeps its place";
}

TEST(OrderBook, ModifyIncreaseQuantityLosesPriority) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    std::uint64_t b = add(book, Side::BUY, 100, 10);
    std::uint64_t r = book.modifyOrder(a, 20, 100);
    EXPECT_NE(r, a) << "an increase is re-added with a new id";
    EXPECT_EQ(book.getOrder(a), nullptr) << "old id must be gone";
    ASSERT_NE(book.getOrder(r), nullptr);
    EXPECT_EQ(book.getOrder(r)->getQuantity(), 20);
    EXPECT_EQ(queueOf(book, b), (std::vector<std::uint64_t>{b, r})) << "an increase goes to the back";
}

TEST(OrderBook, ModifyPriceMovesOrderToNewLevel) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    std::uint64_t b = add(book, Side::BUY, 100);
    std::uint64_t r = book.modifyOrder(a, 10, 101);
    EXPECT_NE(r, a);
    EXPECT_EQ(book.getOrder(a), nullptr);
    ASSERT_NE(book.getOrder(r), nullptr);
    EXPECT_EQ(book.getOrder(r)->getPrice(), 101);
    EXPECT_EQ(book.getOrder(r)->getLevel()->getPrice(), 101);
    EXPECT_EQ(queueOf(book, b), (std::vector<std::uint64_t>{b})) << "old level keeps the rest";
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 101);
}

TEST(OrderBook, ModifyPriceAndQuantityTogetherAppliesBoth) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 110, 10);
    std::uint64_t r = book.modifyOrder(a, 4, 112);
    ASSERT_NE(book.getOrder(r), nullptr);
    EXPECT_EQ(book.getOrder(r)->getPrice(), 112);
    EXPECT_EQ(book.getOrder(r)->getQuantity(), 4);
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 112);
}

TEST(OrderBook, ModifyPriceToExistingLevelJoinsItsBack) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100);
    std::uint64_t b = add(book, Side::BUY, 101);
    std::uint64_t r = book.modifyOrder(a, 10, 101);
    EXPECT_EQ(queueOf(book, b), (std::vector<std::uint64_t>{b, r}));
}

TEST(OrderBook, ModifyMovingBestBidAwayUpdatesBest) {
    OrderBook book(16);
    add(book, Side::BUY, 100);
    std::uint64_t top = add(book, Side::BUY, 105);
    book.modifyOrder(top, 10, 99);
    ASSERT_NE(book.getBestBid(), nullptr);
    EXPECT_EQ(book.getBestBid()->getPrice(), 100);
}

TEST(OrderBook, ModifyRepeatedlyDoesNotLeakPoolSlots) {
    OrderBook book(1);
    std::uint64_t id = add(book, Side::BUY, 100);
    for (int i = 0; i < 1000; ++i) id = book.modifyOrder(id, 10, 100 + (i % 2 ? 1 : 2));
    ASSERT_NE(book.getOrder(id), nullptr);
}

// ---------- randomized check against a simple reference model ----------

TEST(OrderBook, RandomOperationsMatchReferenceModel) {
    OrderBook book(5000);
    struct Live { Side side; std::int32_t price; };
    std::map<std::uint64_t, Live> live;
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> pct(0, 99), price(90, 120), qty(1, 50);

    auto bestOf = [&](Side side) {
        bool found = false;
        std::int32_t best = 0;
        for (auto& [_, o] : live) {
            if (o.side != side) continue;
            if (!found || (side == Side::BUY ? o.price > best : o.price < best)) best = o.price;
            found = true;
        }
        return std::make_pair(found, best);
    };
    auto pick = [&]() {
        auto it = live.begin();
        std::advance(it, std::uniform_int_distribution<std::size_t>(0, live.size() - 1)(rng));
        return it->first;
    };

    for (int op = 0; op < 4000; ++op) {
        int r = pct(rng);
        if (live.empty() || r < 50) {
            Side side = pct(rng) < 50 ? Side::BUY : Side::SELL;
            std::int32_t p = price(rng);
            std::uint64_t id = add(book, side, p, qty(rng));
            live[id] = {side, p};
        } else if (r < 80) {
            std::uint64_t id = pick();
            book.removeOrder(id);
            live.erase(id);
        } else {
            std::uint64_t id = pick();
            std::int32_t p = price(rng);
            Side side = live[id].side;
            std::uint64_t newId = book.modifyOrder(id, qty(rng), p);
            live.erase(id);
            live[newId] = {side, p};
        }

        for (Side side : {Side::BUY, Side::SELL}) {
            auto [has, best] = bestOf(side);
            PriceLevel* got = side == Side::BUY ? book.getBestBid() : book.getBestAsk();
            const char* name = side == Side::BUY ? "bid" : "ask";
            if (!has) {
                ASSERT_EQ(got, nullptr) << "op " << op << ": best " << name << " should be empty";
            } else {
                ASSERT_NE(got, nullptr) << "op " << op << ": best " << name << " missing";
                ASSERT_EQ(got->getPrice(), best) << "op " << op << ": wrong best " << name;
                ASSERT_FALSE(got->isEmpty()) << "op " << op << ": best " << name << " level is empty";
            }
        }
    }
    for (auto& [id, o] : live) {
        Order* ord = book.getOrder(id);
        ASSERT_NE(ord, nullptr) << "live order " << id << " missing";
        EXPECT_EQ(ord->getPrice(), o.price);
        EXPECT_EQ(ord->getSide(), o.side);
    }
}

// ---------- ids handed out separately ----------

TEST(OrderBook, NextOrderIdThenAddUsesThatId) {
    OrderBook book(16);
    std::uint64_t id = book.nextOrderId();
    EXPECT_EQ(book.addOrder(id, 10, 100, Side::BUY), id);
    ASSERT_NE(book.getOrder(id), nullptr);
    EXPECT_EQ(add(book, Side::BUY, 100), id + 1) << "the counter is shared by both add paths";
}

TEST(OrderBook, AddWithIdInUseThrows) {
    OrderBook book(16);
    std::uint64_t id = add(book, Side::BUY, 100);
    EXPECT_THROW(book.addOrder(id, 5, 101, Side::BUY), std::invalid_argument);
    EXPECT_EQ(book.getOrder(id)->getPrice(), 100);
}

TEST(OrderBook, AddWithIdNotHandedOutThrows) {
    OrderBook book(16);
    EXPECT_THROW(book.addOrder(std::uint64_t{0}, 5, 100, Side::BUY), std::invalid_argument);
    EXPECT_THROW(book.addOrder(std::uint64_t{7}, 5, 100, Side::BUY), std::invalid_argument);
    EXPECT_EQ(book.getBestBid(), nullptr);
}

// ---------- fill ----------

TEST(OrderBook, PartialFillReducesRemainingAndKeepsPriority) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 100, 10);
    std::uint64_t b = add(book, Side::SELL, 100, 10);
    book.fillOrder(a, 3);
    EXPECT_EQ(book.getOrder(a)->getRemaining(), 7);
    EXPECT_EQ(book.getOrder(a)->getQuantity(), 10) << "original quantity must not change on a fill";
    EXPECT_EQ(queueOf(book, a), (std::vector<std::uint64_t>{a, b}));
}

TEST(OrderBook, FullFillRemovesOrderAndEmptyLevel) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 100, 10);
    std::uint64_t b = add(book, Side::SELL, 101, 10);
    book.fillOrder(a, 4);
    book.fillOrder(a, 6);
    EXPECT_EQ(book.getOrder(a), nullptr);
    ASSERT_NE(book.getBestAsk(), nullptr);
    EXPECT_EQ(book.getBestAsk()->getPrice(), 101) << "the emptied level must be gone";
    book.fillOrder(b, 10);
    EXPECT_EQ(book.getBestAsk(), nullptr);
}

TEST(OrderBook, FullFillFreesPoolSlot) {
    OrderBook book(1);
    std::uint64_t a = add(book, Side::BUY, 100, 5);
    book.fillOrder(a, 5);
    EXPECT_NO_THROW(add(book, Side::BUY, 100, 5)) << "a pool of 1 needs the slot back";
}

TEST(OrderBook, OverfillThrowsAndLeavesOrderUnchanged) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    book.fillOrder(a, 4);
    EXPECT_THROW(book.fillOrder(a, 7), std::invalid_argument);
    EXPECT_EQ(book.getOrder(a)->getRemaining(), 6);
}

TEST(OrderBook, FillUnknownOrNonPositiveThrows) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    EXPECT_THROW(book.fillOrder(a + 99, 1), std::invalid_argument);
    EXPECT_THROW(book.fillOrder(a, 0), std::invalid_argument);
    EXPECT_THROW(book.fillOrder(a, -2), std::invalid_argument);
    EXPECT_EQ(book.getOrder(a)->getRemaining(), 10);
}

TEST(OrderBook, PartiallyFilledOrderCannotBeModified) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::BUY, 100, 10);
    book.fillOrder(a, 3);
    EXPECT_THROW(book.modifyOrder(a, 5, 100), std::invalid_argument);
}

// ---------- read-only views ----------

TEST(OrderBook, OrderCountCountsBothSides) {
    OrderBook book(16);
    EXPECT_EQ(book.getOrderCount(), 0u);
    add(book, Side::BUY, 100);
    add(book, Side::BUY, 100);
    std::uint64_t s = add(book, Side::SELL, 105);
    EXPECT_EQ(book.getOrderCount(), 3u);
    book.removeOrder(s);
    EXPECT_EQ(book.getOrderCount(), 2u);
}

TEST(OrderBook, GetLevelsReturnsBestFirstAndStopsAtTheLimit) {
    OrderBook book(64);
    for (std::int32_t p : {103, 99, 101, 100, 104, 102}) add(book, Side::SELL, p);
    for (std::int32_t p : {90, 95, 92, 94, 91, 93}) add(book, Side::BUY, p);
    auto prices = [](const std::vector<const PriceLevel*>& levels) {
        std::vector<std::int32_t> out;
        for (auto* l : levels) out.push_back(l->getPrice());
        return out;
    };
    EXPECT_EQ(prices(book.getLevels(Side::SELL, 3)), (std::vector<std::int32_t>{99, 100, 101}));
    EXPECT_EQ(prices(book.getLevels(Side::BUY, 3)), (std::vector<std::int32_t>{95, 94, 93}));
    EXPECT_EQ(book.getLevels(Side::SELL, 100).size(), 6u);
    EXPECT_TRUE(book.getLevels(Side::BUY, 0).empty());
    OrderBook empty(4);
    EXPECT_TRUE(empty.getLevels(Side::SELL, 5).empty());
}

// ---------- traded flag ----------

TEST(OrderBook, FillMarksOrderTraded) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 100, 10);
    EXPECT_FALSE(book.getOrder(a)->hasTraded());
    book.fillOrder(a, 3);
    EXPECT_TRUE(book.getOrder(a)->hasTraded());
}

TEST(OrderBook, UnchangedModifyOfTradedOrderStillThrows) {
    OrderBook book(16);
    std::uint64_t a = add(book, Side::SELL, 100, 10);
    book.fillOrder(a, 3);
    EXPECT_THROW(book.modifyOrder(a, 10, 100), std::invalid_argument) << "same values, but the order has traded";
    EXPECT_EQ(book.getOrder(a)->getRemaining(), 7);
}

TEST(OrderBook, AddOrderCanMarkTheRestOfAPartlyFilledOrder) {
    OrderBook book(16);
    std::uint64_t id = book.nextOrderId();
    book.addOrder(id, 6, 101, Side::BUY, true);
    EXPECT_TRUE(book.getOrder(id)->hasTraded());
    EXPECT_THROW(book.modifyOrder(id, 3, 101), std::invalid_argument);
}
