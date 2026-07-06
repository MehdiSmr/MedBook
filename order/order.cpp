#include <stdexcept>
#include <cstdint>
#include "order.h" 
#include "../side/side.h"

Order::Order(std::uint64_t orderId, std::int64_t quantity, std::int64_t price, std::int64_t sequence, 
        Side side, Order *next, Order *prev, PriceLevel *level)
    : orderId(orderId), quantity(quantity), remaining(quantity), price(price), sequence(sequence), 
    side(side), next(next), prev(prev), level(level) {}

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

