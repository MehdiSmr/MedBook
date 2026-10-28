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

// Fills, quantity changes and link changes are private to OrderBook and PriceLevel now;
// they are tested through OrderBook::fillOrder (test_order_book.cpp) and PriceLevel.
