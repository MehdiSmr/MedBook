#pragma once
#include <cstdint>
#include <memory>
#include <array>
#include <vector>
#include "../order/order.h"

#define ORDER_POOL_SIZE 1000000

class OrderPool {
private:
    std::unique_ptr<std::byte[]> pool; // Use unique_ptr to manage the memory 
    std::unique_ptr<bool[]> inUse; 
    uint32_t poolSize; // Size of the pool 
    std::vector<uint32_t> freeIndices; // To keep track of free indices in the pool
public:
    OrderPool(int size = ORDER_POOL_SIZE);
    Order* createOrder(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, std::uint64_t sequence, 
                       Side side);
    void destroyOrder(Order* order);
};
