#pragma once
#include <cstdint>
#include "../side/side.h"

class PriceLevel; // Forward declaration

class Order
{
private:
    std::uint64_t orderId;
    std::int64_t quantity;
    std::int64_t remaining; 
    std::int64_t price;
    std::int64_t sequence;
    Order *next;
    Order *prev;
    PriceLevel *level;
    Side side;

public:
    Order(std::uint64_t orderId, std::int64_t quantity, std::int64_t price, std::int64_t sequence, 
            Side side, Order *next, Order *prev, PriceLevel *level);
    std::uint64_t getOrderId() const;
    std::int64_t getQuantity() const;
    std::int64_t getRemaining() const;
    std::int64_t getPrice() const;
    std::int64_t getSequence() const;
    Side getSide() const;
    void reduceRemaining(std::int64_t amount);
    Order* getNext() const; 
    void setNext(Order* nextOrder); 
    Order* getPrev() const;
    void setPrev(Order* prevOrder);
    PriceLevel* getLevel() const;
    void setLevel(PriceLevel* level); 
};
