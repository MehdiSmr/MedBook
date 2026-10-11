#include "order_book.h"
#include <cstdint>
#include "../price_level/price_level.h"
#include "../order/order.h"
#include "../side/side.h"


OrderBook::RedBlackTree::RedBlackTree() :  nil(nullptr), root(nil) {}

PriceLevel* OrderBook::RedBlackTree::getRoot() const {
    return root;
}

PriceLevel* OrderBook::RedBlackTree::getNil() const {
    return nil;
}

PriceLevel* OrderBook::RedBlackTree::insert(std::int32_t price) {
    PriceLevel* newNode = new PriceLevel(price);
    newNode->setLeftChild(nil);
    newNode->setRightChild(nil);
    PriceLevel* y = nullptr;
    PriceLevel* x = root;
    while (x != nil) {
        y = x;
        if (price > x->getPrice()) {
             x = x->getRightChild();
        } else {
            x = x->getLeftChild();
        }
    }

    newNode->setParent(y);
    if (y == nullptr) {
        root = newNode;
        newNode->setColor(PriceLevel::Color::BLACK); // Root is always black
    } else if (price > y->getPrice()) {
        y->setRightChild(newNode);
    } else {
        y->setLeftChild(newNode);
    }
    fixInsert(newNode);
    return newNode;
}

void OrderBook::RedBlackTree::fixInsert(PriceLevel* z) {
    if (z->getColor() == PriceLevel::Color::BLACK || z->getParent()->getColor() == PriceLevel::Color::BLACK) {
        return;
    }
    while (z != root && z->getParent()->getColor() == PriceLevel::Color::RED) {
        if (z->getParent() == z->getParent()->getParent()->getLeftChild()) {
            PriceLevel* y = z->getParent()->getParent()->getRightChild();
            if (y != nil && y->getColor() == PriceLevel::Color::RED) {
                z->getParent()->setColor(PriceLevel::Color::BLACK);
                y->setColor(PriceLevel::Color::BLACK);
                z->getParent()->getParent()->setColor(PriceLevel::Color::RED);
                z = z->getParent()->getParent();
            } else {
                if (z == z->getParent()->getRightChild()) {
                    z = z->getParent();
                    leftRotate(z);
                }
                z->getParent()->setColor(PriceLevel::Color::BLACK);
                z->getParent()->getParent()->setColor(PriceLevel::Color::RED);
                rightRotate(z->getParent()->getParent());
            }
        } else {
            PriceLevel* y = z->getParent()->getParent()->getLeftChild();
            if (y != nil && y->getColor() == PriceLevel::Color::RED) {
                z->getParent()->setColor(PriceLevel::Color::BLACK);
                y->setColor(PriceLevel::Color::BLACK);
                z->getParent()->getParent()->setColor(PriceLevel::Color::RED);
                z = z->getParent()->getParent();
            } else {
                if (z == z->getParent()->getLeftChild()) {
                    z = z->getParent();
                    rightRotate(z);
                }
                z->getParent()->setColor(PriceLevel::Color::BLACK);
                z->getParent()->getParent()->setColor(PriceLevel::Color::RED);
                leftRotate(z->getParent()->getParent());
            }
        }
    }
    root->setColor(PriceLevel::Color::BLACK);
}

void OrderBook::RedBlackTree::remove(PriceLevel* nodeToRemove) {
    // Implementation of Red-Black Tree removal logic goes here
}

void OrderBook::RedBlackTree::fixRemove(PriceLevel* x) {
    // Implementation of Red-Black Tree removal fix-up logic goes here
}

void OrderBook::RedBlackTree::transplant(PriceLevel* u, PriceLevel* v) {
    // Implementation of Red-Black Tree transplant logic goes here
}

void OrderBook::RedBlackTree::leftRotate(PriceLevel* x) {
    // Implementation of left rotation logic goes here
}

void OrderBook::RedBlackTree::rightRotate(PriceLevel* y) {
    // Implementation of right rotation logic goes here
}

OrderBook::OrderBook(int size) : orderPool(size), buyTree(), sellTree(), idCounter(0) {}

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
        insertPriceLevel(price, Side::BUY);
        buyPriceLevelMap[price]->addOrder(newOrder);
    } else {
        insertPriceLevel(price, Side::SELL);
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
                removePriceLevel(level->getPrice(), orderToRemove->getSide());
            }
        }
        orderPool.destroyOrder(orderToRemove);
        orderMap.erase(it);
    } else {
        throw std::invalid_argument("Order ID not found");
    }
}

std::uint64_t OrderBook::modifyOrder(std::uint64_t orderId, std::int32_t newQuantity, std::int32_t newPrice) {
    if (newQuantity <= 0 || newPrice <= 0) {
        throw std::invalid_argument("New quantity and price must be positive");
    } 
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* orderToModify = it->second;
        if (orderToModify->getQuantity() == newQuantity && orderToModify->getPrice() == newPrice) {
            return orderId; // No modification needed
        }
        else if (orderToModify->getRemaining() != orderToModify->getQuantity()) {
            throw std::invalid_argument("Cannot modify an order that has already been partially filled");
        }
        else if (orderToModify->getQuantity() > newQuantity && orderToModify->getPrice() == newPrice) {
            orderToModify->setQuantity(newQuantity); // Update the quantity  
            return orderId; // No need to change the price level if only quantity changes
        }
        PriceLevel* level = orderToModify->getLevel();
        if (level) {
            level->removeOrder(orderToModify);
            if (level->isEmpty()) {
                removePriceLevel(level->getPrice(), orderToModify->getSide());
            }
        }
        std::uint64_t newSequence = orderToModify->getSequence(); 
        Side newSide = orderToModify->getSide(); 
        orderPool.destroyOrder(orderToModify); 
        orderMap.erase(orderId);
        std::uint64_t newOrderId = addOrder(newQuantity, newPrice, newSequence, newSide);
        return newOrderId;
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

PriceLevel* OrderBook::getBestBid() const {
    return bestBid;
}

void OrderBook::setBestBid(PriceLevel* level) {
    bestBid = level;
}

PriceLevel* OrderBook::getBestAsk() const {
    return bestAsk;
}

void OrderBook::setBestAsk(PriceLevel* level) {
    bestAsk = level;
}

void OrderBook::insertPriceLevel(std::int32_t price, Side side) {
    if (side == Side::BUY) { 
        if (buyPriceLevelMap.find(price) == buyPriceLevelMap.end()) {
            PriceLevel* newLevel = buyTree->insert(price);
            buyPriceLevelMap[price] = newLevel;

        }
    } else {
        if (sellPriceLevelMap.find(price) == sellPriceLevelMap.end()) {
            PriceLevel* newLevel = sellTree->insert(price); 
            sellPriceLevelMap[price] = newLevel;
        }
    }
}

void OrderBook::removePriceLevel(std::int32_t price, Side side) {
    
    if (side == Side::SELL) {
        auto it = sellPriceLevelMap.find(price);
        if (it != sellPriceLevelMap.end()) {
            PriceLevel* levelToRemove = it->second;
            sellTree->remove(levelToRemove);
            sellPriceLevelMap.erase(it);
            delete levelToRemove;
        }
    } else {
        auto it = buyPriceLevelMap.find(price);
        if (it != buyPriceLevelMap.end()) {
            PriceLevel* levelToRemove = it->second;
            buyTree->remove(levelToRemove);
            buyPriceLevelMap.erase(it);
            delete levelToRemove;
        }
    }
}
