#include <stdexcept>
#include <cstdint>
#include "order.h" 
#include "../side/side.h"

Order::Order(std::uint64_t orderId, std::int64_t quantity, std::int64_t price, std::int64_t sequence, Side side)
    : orderId(orderId), quantity(quantity), remaining(quantity), price(price), sequence(sequence), side(side) {}

std::uint64_t Order::getOrderId() const {
    return orderId;
}

std::int64_t Order::getQuantity() const {
    return quantity;
}

std::int64_t Order::getRemaining() const {
    return remaining;
}

std::int64_t Order::getPrice() const {
    return price;
}

std::int64_t Order::getSequence() const {
    return sequence;
}

Side Order::getSide() const {
    return side;
}

void Order::reduceRemaining(std::int64_t amount) {
    if (amount > remaining) {
        throw std::invalid_argument("Amount to reduce exceeds remaining quantity");
    }
    remaining -= amount;
}
