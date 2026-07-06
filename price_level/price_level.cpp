#include <stdexcept>
#include <cstdint>
#include "price_level.h"

Order* PriceLevel::getHead() const {
    return head;
}

Order* PriceLevel::getTail() const {
    return tail;
}

std::int64_t PriceLevel::getDepth() const {
    return depth;
}

void PriceLevel::addOrder(Order* order) {
    if (!order) {
        throw std::invalid_argument("Order is null");
    }
    if (!head) {
        head = tail = order;
    } else {
        tail->setNext(order);
        order->setPrev(tail);
        tail = order;
    }
    depth++;
}

void PriceLevel::removeOrder(Order* order) {
    if (!order) {
        throw std::invalid_argument("Order is null");
    }
    if (order->getLevel() != this) {
        throw std::invalid_argument("Order does not belong to this price level");
    }
    if (order->getPrev()) {
        order->getPrev()->setNext(order->getNext());
    } else {
        head = order->getNext();
    }

    if (order->getNext()) {
        order->getNext()->setPrev(order->getPrev());
    } else {
        tail = order->getPrev();
    }

    depth--;
}

bool PriceLevel::isEmpty() const {
    return depth == 0;
}
