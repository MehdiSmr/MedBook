#include "order_book.h"
#include <cstdint>
#include "../price_level/price_level.h"
#include "../order/order.h"
#include "../side/side.h"

OrderBook::OrderBook(int size) : orderPool(size), buyTree(nullptr), sellTree(nullptr), idCounter(0) {}

OrderBook::~OrderBook() {
    orderMap.clear();
    buyPriceLevelMap.clear();
    sellPriceLevelMap.clear();
}

std::uint64_t OrderBook::addOrder(std::int32_t quantity, std::int32_t price, std::uint64_t sequence, Side side) {
    if (quantity <= 0 || price <= 0 || sequence <= 0) {
        throw std::invalid_argument("Quantity and price and sequence must be positive");
    }

    std::uint64_t orderId = ++idCounter;
    Order* newOrder = orderPool.createOrder(orderId, quantity, price, sequence, side);
    orderMap[orderId] = newOrder;

    if (side == Side::BUY) {
        insertPriceLevel(price);
        buyPriceLevelMap[price]->addOrder(newOrder);
    } else {
        insertPriceLevel(price);
        sellPriceLevelMap[price]->addOrder(newOrder);
    }
    
    return orderId;
}

void OrderBook::removeOrder(std::uint64_t orderId) {
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* orderToRemove = it->second;
        PriceLevel* level = orderToRemove->getLevel();
        if (level) {
            level->removeOrder(orderToRemove);
            if (level->isEmpty()) {
                removePriceLevel(level->getPrice());
            }
        }
        orderPool.destroyOrder(orderToRemove);
        orderMap.erase(it);
    } else {
        throw std::invalid_argument("Order ID not found");
    }
}

void OrderBook::modifyOrder(std::uint64_t orderId, std::int32_t newQuantity, std::int32_t newPrice) {
    if (newQuantity <= 0 || newPrice <= 0) {
        throw std::invalid_argument("New quantity and price must be positive");
    } 
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* orderToModify = it->second;
        if (orderToModify->getQuantity() == newQuantity && orderToModify->getPrice() == newPrice) {
            return; // No modification needed
        }
        else if (orderToModify->getRemaining() != orderToModify->getQuantity()) {
            throw std::invalid_argument("Cannot modify an order that has already been partially filled");
        }
        else if (orderToModify->getQuantity() != newQuantity) {
            orderToModify->reduceRemaining(orderToModify->getQuantity() - newQuantity); // Update the remaining quantity
            return; // No need to change the price level if only quantity changes
        }
        PriceLevel* level = orderToModify->getLevel();
        if (level) {
            level->removeOrder(orderToModify);
            if (level->isEmpty()) {
                removePriceLevel(level->getPrice());
            }
        }
        orderToModify->reduceRemaining(orderToModify->getQuantity() - newQuantity);
        orderToModify->setLevel(nullptr);
        addOrder(newQuantity, newPrice, orderToModify->getSequence(), orderToModify->getSide());
    } else {
        throw std::invalid_argument("Order ID not found");
    }
}

Order* OrderBook::getOrder(std::uint64_t orderId) const {
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        return it->second;
    }
    return nullptr;
}
//TODO
PriceLevel* OrderBook::getBestBid() const {
    return buyTree;
}
//TODO
PriceLevel* OrderBook::getBestAsk() const {
    return sellTree;
}
//TODO
void OrderBook::insertPriceLevel(std::int32_t price) {
    if (buyPriceLevelMap.find(price) == buyPriceLevelMap.end()) {
        PriceLevel* newLevel = new PriceLevel(price);
        buyPriceLevelMap[price] = newLevel;
        if (!buyTree || price > buyTree->getPrice()) {
            newLevel->setLeftChild(buyTree);
            buyTree = newLevel;
        } else {
            PriceLevel* current = buyTree;
            while (current->getRightChild() && current->getRightChild()->getPrice() > price) {
                current = current->getRightChild();
            }
            newLevel->setRightChild(current->getRightChild());
            current->setRightChild(newLevel);
        }
    }
}
//TODO
void OrderBook::removePriceLevel(std::int32_t price) {
    auto it = buyPriceLevelMap.find(price);
    if (it != buyPriceLevelMap.end()) {
        PriceLevel* levelToRemove = it->second;
        if (levelToRemove == buyTree) {
            buyTree = levelToRemove->getLeftChild();
        } else {
            PriceLevel* current = buyTree;
            while (current && current->getRightChild() != levelToRemove) {
                current = current->getRightChild();
            }
            if (current) {
                current->setRightChild(levelToRemove->getRightChild());
            }
        }
        delete levelToRemove;
        buyPriceLevelMap.erase(it);
    }
}
