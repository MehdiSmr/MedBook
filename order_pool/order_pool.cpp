#include "order_pool.h"
#include <iostream>
#include <numeric>

OrderPool::OrderPool(int size): 
    pool{std::make_unique<std::byte[]>(size * sizeof(Order))},
    inUse{std::make_unique<bool[]>(size)}, 
    poolSize(size),
    freeIndices(size)
{
    std::iota(freeIndices.begin(), freeIndices.end(), 0);
}

Order* OrderPool::createOrder(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, std::uint64_t sequence, 
                              Side side) {
    if (freeIndices.empty()) {
        throw std::runtime_error("Order pool is full");
    }
    std::uint32_t index = freeIndices.back();
    freeIndices.pop_back();
    Order* order = new (&pool[index * sizeof(Order)]) Order(orderId, quantity, price, sequence, side, nullptr, nullptr, nullptr);
    inUse[index] = true; // Mark this index as in use 
    return order;
}

void OrderPool::destroyOrder(Order* order) {
    //we need to ensure that order is valid 
    if (!order) {
        throw std::invalid_argument("Order pointer is null");
    } else if ( order < reinterpret_cast<Order*>(pool.get()) || 
         order >= reinterpret_cast<Order*>(pool.get() + poolSize * sizeof(Order))) {
        throw std::invalid_argument("Order pointer is out of bounds of the pool");
    } else {
        auto bytes = reinterpret_cast<std::byte*>(order) - pool.get();
        auto denom = sizeof(Order);
        if (bytes % denom != 0) {
            throw std::invalid_argument("Order pointer is not aligned to the size of Order");
        }
        uint32_t index = bytes / denom;
        if (!inUse[index]) {
            throw std::runtime_error("Order at this index is not in use");
        } 
        freeIndices.push_back(index);
        inUse[index] = false; // Mark this index as free 
    }
}
