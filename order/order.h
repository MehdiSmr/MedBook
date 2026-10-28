#pragma once
#include <cstdint>
#include "../side/side.h"

class PriceLevel; // Forward declaration

class Order
{
private:
    std::uint64_t orderId;
    std::int32_t quantity;
    std::int32_t remaining; 
    std::int32_t price;
    Order *next;
    Order *prev;
    PriceLevel *level;
    Side side;

public:
    Order(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, 
            Side side, Order *next, Order *prev, PriceLevel *level);
    std::uint64_t getOrderId() const;
    std::int32_t getQuantity() const;
    std::int32_t getRemaining() const;
    std::int32_t getPrice() const;
    Side getSide() const;
    Order* getNext() const; 
    Order* getPrev() const;
    PriceLevel* getLevel() const;

private:
    // Only the book changes quantities and only PriceLevel changes links,
    // so outside code can't break the book's invariants.
    friend class OrderBook;
    friend class PriceLevel;
    void setQuantity(std::int32_t newQuantity);
    void reduceRemaining(std::int32_t amount);
    void setNext(Order* nextOrder); 
    void setPrev(Order* prevOrder);
    void setLevel(PriceLevel* level); 
};
