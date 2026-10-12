#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include "../order/order.h"
#include "../price_level/price_level.h"

namespace {

Order makeOrder(std::uint64_t id, std::int32_t price = 100) {
    return Order(id, 10, price, id, Side::BUY, nullptr, nullptr, nullptr);
}

// Walks head -> tail and checks the links match `expected` in both directions.
void expectQueue(const PriceLevel& level, const std::vector<Order*>& expected) {
    ASSERT_EQ(level.getDepth(), static_cast<std::int64_t>(expected.size()));
    if (expected.empty()) {
        EXPECT_EQ(level.getHead(), nullptr);
        EXPECT_EQ(level.getTail(), nullptr);
        EXPECT_TRUE(level.isEmpty());
        return;
    }
    EXPECT_FALSE(level.isEmpty());
    EXPECT_EQ(level.getHead(), expected.front());
    EXPECT_EQ(level.getTail(), expected.back());
    EXPECT_EQ(expected.front()->getPrev(), nullptr);
    EXPECT_EQ(expected.back()->getNext(), nullptr);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i]->getLevel(), &level) << "order " << i << " has wrong level";
        if (i + 1 < expected.size()) {
            EXPECT_EQ(expected[i]->getNext(), expected[i + 1]) << "next link broken at " << i;
            EXPECT_EQ(expected[i + 1]->getPrev(), expected[i]) << "prev link broken at " << i + 1;
        }
    }
}

void expectDetached(const Order& o) {
    EXPECT_EQ(o.getNext(), nullptr);
    EXPECT_EQ(o.getPrev(), nullptr);
    EXPECT_EQ(o.getLevel(), nullptr);
}

} // namespace

TEST(PriceLevel, NewLevelIsEmpty) {
    PriceLevel level(100);
    EXPECT_EQ(level.getPrice(), 100);
    expectQueue(level, {});
}

TEST(PriceLevel, AddOrdersKeepsFifoOrder) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2), c = makeOrder(3);
    level.addOrder(&a);
    level.addOrder(&b);
    level.addOrder(&c);
    expectQueue(level, {&a, &b, &c});
}

TEST(PriceLevel, RemoveHead) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2), c = makeOrder(3);
    level.addOrder(&a); level.addOrder(&b); level.addOrder(&c);
    level.removeOrder(&a);
    expectQueue(level, {&b, &c});
    expectDetached(a);
}

TEST(PriceLevel, RemoveMiddle) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2), c = makeOrder(3);
    level.addOrder(&a); level.addOrder(&b); level.addOrder(&c);
    level.removeOrder(&b);
    expectQueue(level, {&a, &c});
    expectDetached(b);
}

TEST(PriceLevel, RemoveTail) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2), c = makeOrder(3);
    level.addOrder(&a); level.addOrder(&b); level.addOrder(&c);
    level.removeOrder(&c);
    expectQueue(level, {&a, &b});
    expectDetached(c);
}

TEST(PriceLevel, RemoveOnlyOrderLeavesLevelEmpty) {
    PriceLevel level(100);
    Order a = makeOrder(1);
    level.addOrder(&a);
    level.removeOrder(&a);
    expectQueue(level, {});
    expectDetached(a);
}

TEST(PriceLevel, ReAddedOrderGoesToBack) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2);
    level.addOrder(&a); level.addOrder(&b);
    level.removeOrder(&a);
    level.addOrder(&a);
    expectQueue(level, {&b, &a});
}

TEST(PriceLevel, EmptyThenRefill) {
    PriceLevel level(100);
    Order a = makeOrder(1), b = makeOrder(2);
    level.addOrder(&a);
    level.removeOrder(&a);
    level.addOrder(&b);
    expectQueue(level, {&b});
}

TEST(PriceLevel, RemoveOrderFromAnotherLevelThrows) {
    PriceLevel level(100), other(101);
    Order a = makeOrder(1), b = makeOrder(2, 101);
    level.addOrder(&a);
    other.addOrder(&b);
    EXPECT_THROW(level.removeOrder(&b), std::invalid_argument);
    expectQueue(level, {&a});
    expectQueue(other, {&b});
}

TEST(PriceLevel, NullOrderThrows) {
    PriceLevel level(100);
    EXPECT_THROW(level.addOrder(nullptr), std::invalid_argument);
    EXPECT_THROW(level.removeOrder(nullptr), std::invalid_argument);
}
