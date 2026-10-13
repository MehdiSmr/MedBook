#include <gtest/gtest.h>
#include <set>
#include <stdexcept>
#include "../order_pool/order_pool.h"

TEST(OrderPool, CreateReturnsInitializedOrder) {
    OrderPool pool(4);
    Order* o = pool.createOrder(9, 10, 100, Side::SELL);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->getOrderId(), 9u);
    EXPECT_EQ(o->getQuantity(), 10);
    EXPECT_EQ(o->getRemaining(), 10);
    EXPECT_EQ(o->getPrice(), 100);
    EXPECT_EQ(o->getSide(), Side::SELL);
    EXPECT_EQ(o->getNext(), nullptr);
    EXPECT_EQ(o->getPrev(), nullptr);
    EXPECT_EQ(o->getLevel(), nullptr);
}

TEST(OrderPool, LiveOrdersHaveDistinctAddresses) {
    OrderPool pool(64);
    std::set<Order*> seen;
    for (int i = 0; i < 64; ++i) {
        Order* o = pool.createOrder(i + 1, 1, 100, Side::BUY);
        EXPECT_TRUE(seen.insert(o).second) << "slot handed out twice at i=" << i;
    }
}

TEST(OrderPool, FullPoolThrows) {
    OrderPool pool(3);
    for (int i = 0; i < 3; ++i) pool.createOrder(i + 1, 1, 100, Side::BUY);
    EXPECT_THROW(pool.createOrder(4, 1, 100, Side::BUY), std::runtime_error);
}

TEST(OrderPool, DestroyedSlotIsReused) {
    OrderPool pool(2);
    Order* a = pool.createOrder(1, 1, 100, Side::BUY);
    pool.createOrder(2, 1, 100, Side::BUY);
    pool.destroyOrder(a);
    Order* c = nullptr;
    ASSERT_NO_THROW(c = pool.createOrder(3, 5, 101, Side::SELL));
    EXPECT_EQ(c, a) << "the only free slot should be the one just destroyed";
    EXPECT_EQ(c->getOrderId(), 3u) << "reused slot must hold the new order's data";
    EXPECT_EQ(c->getPrice(), 101);
}

TEST(OrderPool, CanCycleFarMoreOrdersThanCapacity) {
    OrderPool pool(2);
    for (int i = 0; i < 10000; ++i) {
        Order* o = pool.createOrder(i + 1, 1, 100, Side::BUY);
        pool.destroyOrder(o);
    }
    SUCCEED();
}

TEST(OrderPool, DoubleDestroyThrows) {
    OrderPool pool(2);
    Order* a = pool.createOrder(1, 1, 100, Side::BUY);
    pool.destroyOrder(a);
    EXPECT_THROW(pool.destroyOrder(a), std::runtime_error);
}

TEST(OrderPool, DestroyNullThrows) {
    OrderPool pool(2);
    EXPECT_THROW(pool.destroyOrder(nullptr), std::invalid_argument);
}

TEST(OrderPool, DestroyPointerFromOutsidePoolThrows) {
    OrderPool pool(2);
    Order outside(1, 1, 100, Side::BUY, nullptr, nullptr, nullptr);
    EXPECT_THROW(pool.destroyOrder(&outside), std::invalid_argument);
}

TEST(OrderPool, DestroyPointerFromAnotherPoolThrows) {
    OrderPool a(2), b(2);
    Order* fromB = b.createOrder(1, 1, 100, Side::BUY);
    EXPECT_THROW(a.destroyOrder(fromB), std::invalid_argument);
}
