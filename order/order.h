#pragma once
#include <string>
#include <cstdint>
#include "../side/side.h"
class Order
{
private:
    std::int64_t orderId;
    std::int64_t quantity;
    std::int64_t remaining; 
    std::int64_t price;
    std::int64_t sequence;
    Side side;

public:
    Order(std::int64_t orderId, std::int64_t quantity, std::int64_t price, std::int64_t sequence, Side side);
    std::int64_t getOrderId() const;
    std::int64_t getQuantity() const;
    std::int64_t getRemaining() const;
    std::int64_t getPrice() const;
    std::int64_t getSequence() const;
    Side getSide() const;
    void reduceRemaining(std::int64_t amount);

};
