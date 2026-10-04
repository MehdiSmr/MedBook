#pragma once
#include <cstdint>
#include "../order/order.h"

class PriceLevel
{
private:
    Order* head;
    Order* tail;
    PriceLevel* rightChild; 
    PriceLevel* leftChild;
    std::int64_t depth;
    const std::int32_t price;

public:
    PriceLevel(std::int32_t price);
    Order* getHead() const;
    Order* getTail() const;
    PriceLevel* getRightChild() const;
    PriceLevel* getLeftChild() const;
    void setRightChild(PriceLevel* child);
    void setLeftChild(PriceLevel* child);
    std::int64_t getDepth() const;
    std::int32_t getPrice() const; 
    void addOrder(Order* order);
    void removeOrder(Order* order);
    bool isEmpty() const;
};
