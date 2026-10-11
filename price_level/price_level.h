#pragma once
#include <cstdint>
#include "../order/order.h"

class PriceLevel { 
public:
    enum class Color { RED, BLACK };
    PriceLevel(std::int32_t price);
    Order* getHead() const;
    Order* getTail() const;
    PriceLevel* getParent() const;
    PriceLevel* getRightChild() const;
    PriceLevel* getLeftChild() const;
    void setParent(PriceLevel* p); 
    void setRightChild(PriceLevel* child);
    void setLeftChild(PriceLevel* child);
    std::int64_t getDepth() const;
    std::int32_t getPrice() const;
    Color getColor() const;
    void setColor(Color c);
    void addOrder(Order* order);
    void removeOrder(Order* order);
    bool isEmpty() const;

private:
    Order* head;
    Order* tail;
    PriceLevel::Color color;
    PriceLevel* parent;
    PriceLevel* rightChild; 
    PriceLevel* leftChild;
    std::int64_t depth;
    const std::int32_t price;
};
