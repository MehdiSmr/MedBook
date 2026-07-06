#pragma once
#include <cstdint>
#include "../order/order.h"

class PriceLevel
{
private:
    Order* head = nullptr;
    Order* tail = nullptr;
    std::int64_t depth = 0;

public:
    Order* getHead() const;
    Order* getTail() const;
    std::int64_t getDepth() const;
    void addOrder(Order* order);
    void removeOrder(Order* order);
    bool isEmpty() const;
};
