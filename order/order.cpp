#include <stdexcept>
#include <cstdint>
#include "order.h" 
#include "../side/side.h"

Order::Order(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, std::uint64_t sequence, 
        Side side, Order *next, Order *prev, PriceLevel *level)
    : orderId(orderId), quantity(quantity), remaining(quantity), price(price), sequence(sequence), 
    next(next), prev(prev), level(level), side(side) {}

std::uint64_t Order::getOrderId() const {
    return orderId;
}

std::int32_t Order::getQuantity() const {
    return quantity;
}

std::int32_t Order::getRemaining() const {
    return remaining;
}

std::int32_t Order::getPrice() const {
    return price;
}

std::uint64_t Order::getSequence() const {
    return sequence;
}

Side Order::getSide() const {
    return side;
}

void Order::reduceRemaining(std::int32_t amount) {
    if (amount > remaining) {
        throw std::invalid_argument("Amount to reduce exceeds remaining quantity");
    }
    remaining -= amount;
}

Order* Order::getNext() const {
    return next;
}

void Order::setNext(Order* nextOrder) {
    next = nextOrder;
}

Order* Order::getPrev() const {
    return prev;
}

void Order::setPrev(Order* prevOrder) {
    prev = prevOrder;
}

PriceLevel* Order::getLevel() const {
    return level;
}

void Order::setLevel(PriceLevel* level) {
    this->level = level;
}

