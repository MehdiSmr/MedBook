#include <gtest/gtest.h>
#include <stdexcept>
#include "../order/order.h"

namespace {

Order makeOrder(std::uint64_t id = 1, std::int32_t qty = 10, std::int32_t price = 100,
                Side side = Side::BUY) {
    return Order(id, qty, price, side, nullptr, nullptr, nullptr);
}

} // namespace

TEST(Order, ConstructorSetsAllFields) {
    Order o = makeOrder(42, 10, 100, Side::SELL);
    EXPECT_EQ(o.getOrderId(), 42u);
    EXPECT_EQ(o.getQuantity(), 10);
    EXPECT_EQ(o.getRemaining(), 10) << "a new order has nothing filled yet";
    EXPECT_EQ(o.getPrice(), 100);
    EXPECT_EQ(o.getSide(), Side::SELL);
    EXPECT_EQ(o.getNext(), nullptr);
    EXPECT_EQ(o.getPrev(), nullptr);
    EXPECT_EQ(o.getLevel(), nullptr);
}

TEST(Order, ReduceRemainingPartialFill) {
    Order o = makeOrder();
    o.reduceRemaining(3);
    EXPECT_EQ(o.getRemaining(), 7);
    EXPECT_EQ(o.getQuantity(), 10) << "original quantity must not change on a fill";
}

TEST(Order, ReduceRemainingToZero) {
    Order o = makeOrder();
    o.reduceRemaining(10);
    EXPECT_EQ(o.getRemaining(), 0);
}

TEST(Order, ReduceRemainingMoreThanRemainingThrowsAndLeavesStateUnchanged) {
    Order o = makeOrder();
    o.reduceRemaining(4);
    EXPECT_THROW(o.reduceRemaining(7), std::invalid_argument);
    EXPECT_EQ(o.getRemaining(), 6);
}

TEST(Order, SetQuantityUpdatesQuantityAndRemaining) {
    Order o = makeOrder();
    o.setQuantity(4);
    EXPECT_EQ(o.getQuantity(), 4);
    EXPECT_EQ(o.getRemaining(), 4);
}

TEST(Order, SetQuantityNegativeThrows) {
    Order o = makeOrder();
    EXPECT_THROW(o.setQuantity(-1), std::invalid_argument);
    EXPECT_EQ(o.getQuantity(), 10);
}

TEST(Order, LinkSettersRoundTrip) {
    Order a = makeOrder(1), b = makeOrder(2), c = makeOrder(3);
    b.setPrev(&a);
    b.setNext(&c);
    EXPECT_EQ(b.getPrev(), &a);
    EXPECT_EQ(b.getNext(), &c);
    auto* fakeLevel = reinterpret_cast<PriceLevel*>(0x1000);
    b.setLevel(fakeLevel);
    EXPECT_EQ(b.getLevel(), fakeLevel);
}
