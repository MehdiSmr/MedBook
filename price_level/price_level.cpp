#include <stdexcept>
#include <cstdint>
#include "price_level.h"

PriceLevel::PriceLevel(std::int32_t price) : head(nullptr), 
        tail(nullptr), rightChild(nullptr), leftChild(nullptr), depth(0), price(price) {}

Order* PriceLevel::getHead() const {
    return head;
}

Order* PriceLevel::getTail() const {
    return tail;
}

std::int64_t PriceLevel::getDepth() const {
    return depth;
}

std::int32_t PriceLevel::getPrice() const {
    return price;
}

PriceLevel* PriceLevel::getRightChild() const {
    return rightChild;
}

PriceLevel* PriceLevel::getLeftChild() const {
    return leftChild;
}

void PriceLevel::setRightChild(PriceLevel* child) {
    rightChild = child;
}

void PriceLevel::setLeftChild(PriceLevel* child) {
    leftChild = child;
}

void PriceLevel::addOrder(Order* order) {
    if (!order) {
        throw std::invalid_argument("Order is null");
    }
    if (!head) {
        head = tail = order;
        order->setPrev(nullptr);
    } else {
        tail->setNext(order);
        order->setPrev(tail);
        tail = order;
    }
    order->setNext(nullptr);
    order->setLevel(this);
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
    order->setPrev(nullptr);
    order->setNext(nullptr);
    order->setLevel(nullptr);
    depth--;
}

bool PriceLevel::isEmpty() const {
    return depth == 0;
}
