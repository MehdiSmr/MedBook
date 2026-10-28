#include <cstdint>
#include <stdexcept>
#include "order_book.h"
#include "../price_level/price_level.h"
#include "../order/order.h"
#include "../side/side.h"


OrderBook::RedBlackTree::RedBlackTree() :  nil(new PriceLevel(0)), root(nullptr) {
    nil->setColor(PriceLevel::Color::BLACK);
    root = nil;
}

OrderBook::RedBlackTree::~RedBlackTree() {
    delete nil;
}

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
    if (nodeToRemove == nullptr || nodeToRemove == nil) {
        return;
    }
    PriceLevel* y = nodeToRemove;
    PriceLevel::Color originalColor = y->getColor();
    PriceLevel* x;

    if (nodeToRemove->getLeftChild() == nil) {
        x = nodeToRemove->getRightChild();
        transplant(nodeToRemove, nodeToRemove->getRightChild());
    } else if (nodeToRemove->getRightChild() == nil) {
        x = nodeToRemove->getLeftChild();
        transplant(nodeToRemove, nodeToRemove->getLeftChild());
    } else {
        y = nodeToRemove->getRightChild();
        while (y->getLeftChild() != nil) {
            y = y->getLeftChild();
        }
        originalColor = y->getColor();
        x = y->getRightChild();
        if (y->getParent() == nodeToRemove) {
            x->setParent(y);
        } else {
            transplant(y, y->getRightChild());
            y->setRightChild(nodeToRemove->getRightChild());
            y->getRightChild()->setParent(y);
        }
        transplant(nodeToRemove, y);
        y->setLeftChild(nodeToRemove->getLeftChild());
        y->getLeftChild()->setParent(y);
        y->setColor(nodeToRemove->getColor());
    }

    if (originalColor == PriceLevel::Color::BLACK) {
        fixRemove(x);
    }    
}

void OrderBook::RedBlackTree::fixRemove(PriceLevel* x) {
    while (x != root && x->getColor() == PriceLevel::Color::BLACK) {
        if (x == x->getParent()->getLeftChild()) {
            PriceLevel* w = x->getParent()->getRightChild();
            if (w->getColor() == PriceLevel::Color::RED) {
                w->setColor(PriceLevel::Color::BLACK);
                x->getParent()->setColor(PriceLevel::Color::RED);
                leftRotate(x->getParent());
                w = x->getParent()->getRightChild();
            }
            if (w->getLeftChild()->getColor() == PriceLevel::Color::BLACK && w->getRightChild()->getColor() == PriceLevel::Color::BLACK) {
                w->setColor(PriceLevel::Color::RED);
                x = x->getParent();
            } else {
                if (w->getRightChild()->getColor() == PriceLevel::Color::BLACK) {
                    w->getLeftChild()->setColor(PriceLevel::Color::BLACK);
                    w->setColor(PriceLevel::Color::RED);
                    rightRotate(w);
                    w = x->getParent()->getRightChild();
                }
                w->setColor(x->getParent()->getColor());
                x->getParent()->setColor(PriceLevel::Color::BLACK);
                w->getRightChild()->setColor(PriceLevel::Color::BLACK);
                leftRotate(x->getParent());
                x = root;
            }
        } else {
            PriceLevel* w = x->getParent()->getLeftChild();
            if (w->getColor() == PriceLevel::Color::RED) {
                w->setColor(PriceLevel::Color::BLACK);
                x->getParent()->setColor(PriceLevel::Color::RED);
                rightRotate(x->getParent());
                w = x->getParent()->getLeftChild();
            }
            if (w->getRightChild()->getColor() == PriceLevel::Color::BLACK && w->getLeftChild()->getColor() == PriceLevel::Color::BLACK) {
                w->setColor(PriceLevel::Color::RED);
                x = x->getParent();
            } else {
                if (w->getLeftChild()->getColor() == PriceLevel::Color::BLACK) {
                    w->getRightChild()->setColor(PriceLevel::Color::BLACK);
                    w->setColor(PriceLevel::Color::RED); 
                    leftRotate(w);
                    w = x->getParent()->getLeftChild();
                }
                w->setColor(x->getParent()->getColor());
                x->getParent()->setColor(PriceLevel::Color::BLACK);
                w->getLeftChild()->setColor(PriceLevel::Color::BLACK);
                rightRotate(x->getParent());
                x = root;
            }
        }
    }
    x->setColor(PriceLevel::Color::BLACK);
}

void OrderBook::RedBlackTree::transplant(PriceLevel* u, PriceLevel* v) {
    if (u->getParent() == nullptr) {
        root = v;
    } else if (u == u->getParent()->getLeftChild()) {
        u->getParent()->setLeftChild(v);
    } else {
        u->getParent()->setRightChild(v);
    }
    v->setParent(u->getParent());
}

void OrderBook::RedBlackTree::leftRotate(PriceLevel* x) {
    PriceLevel* y = x->getRightChild();
    x->setRightChild(y->getLeftChild());
    if (y->getLeftChild() != nil) {
        y->getLeftChild()->setParent(x);
    }
    y->setParent(x->getParent());
    if (x->getParent() == nullptr) {
        root = y;
    } else if (x == x->getParent()->getLeftChild()) {
        x->getParent()->setLeftChild(y);
    } else {
        x->getParent()->setRightChild(y);
    }
    y->setLeftChild(x);
    x->setParent(y);
}

void OrderBook::RedBlackTree::rightRotate(PriceLevel* y) {
    PriceLevel* x = y->getLeftChild();
    y->setLeftChild(x->getRightChild());
    if (x->getRightChild() != nil) {
        x->getRightChild()->setParent(y);
    }
    x->setParent(y->getParent());
    if (y->getParent() == nullptr) {
        root = x;
    } else if (y == y->getParent()->getRightChild()) {
        y->getParent()->setRightChild(x);
    } else {
        y->getParent()->setLeftChild(x);
    }
    x->setRightChild(y);
    y->setParent(x); 
}

OrderBook::OrderBook(int size) : orderPool(size), buyTree(new OrderBook::RedBlackTree()), bestBid(nullptr),
    sellTree(new OrderBook::RedBlackTree()), bestAsk(nullptr), idCounter(0) {}

OrderBook::~OrderBook() {
    while (!orderMap.empty()) {
        auto it = orderMap.begin();
        removeOrder(it->first);
    }
    orderMap.clear();
    for (auto& pair : buyPriceLevelMap) {
        delete pair.second;
    } 
    buyPriceLevelMap.clear();
    delete buyTree;
    for (auto& pair: sellPriceLevelMap) {
        delete pair.second;
    }
    sellPriceLevelMap.clear();
    delete sellTree;
}

std::uint64_t OrderBook::nextOrderId() {
    return ++idCounter;
}

std::uint64_t OrderBook::addOrder(std::int32_t quantity, std::int32_t price, Side side) {
    if (quantity <= 0 || price <= 0) {
        throw std::invalid_argument("Quantity and price must be positive");
    }
    return addOrder(nextOrderId(), quantity, price, side);
}

std::uint64_t OrderBook::addOrder(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, Side side,
                                  bool alreadyTraded) {
    if (quantity <= 0 || price <= 0) {
        throw std::invalid_argument("Quantity and price must be positive");
    }
    // Only ids handed out by nextOrderId() are allowed, and each one only once.
    if (orderId == 0 || orderId > idCounter) {
        throw std::invalid_argument("Order ID was not handed out by nextOrderId");
    }
    if (orderMap.contains(orderId)) {
        throw std::invalid_argument("Order ID already in use");
    }

    Order* newOrder = orderPool.createOrder(orderId, quantity, price, side);
    if (alreadyTraded) newOrder->markTraded();
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

void OrderBook::fillOrder(std::uint64_t orderId, std::int32_t quantity) {
    if (quantity <= 0) {
        throw std::invalid_argument("Fill quantity must be positive");
    }
    auto it = orderMap.find(orderId);
    if (it == orderMap.end()) {
        throw std::invalid_argument("Order ID not found");
    }
    Order* order = it->second;
    order->reduceRemaining(quantity); // Throws, leaving the order unchanged, if quantity > remaining
    order->markTraded();
    if (order->getRemaining() == 0) {
        removeOrder(orderId); // List, orderMap and pool, plus the level if it empties
    }
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
        // Checked first: a traded order can't be modified, even with unchanged values.
        if (orderToModify->hasTraded()) {
            throw std::invalid_argument("Cannot modify an order that has already been partially filled");
        }
        else if (orderToModify->getQuantity() == newQuantity && orderToModify->getPrice() == newPrice) {
            return orderId; // No modification needed
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
        Side newSide = orderToModify->getSide(); 
        orderPool.destroyOrder(orderToModify); 
        orderMap.erase(orderId);
        std::uint64_t newOrderId = addOrder(newQuantity, newPrice, newSide);
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

const OrderBook::RedBlackTree* OrderBook::getTree(Side side) const {
    return side == Side::BUY ? buyTree : sellTree;
}

std::size_t OrderBook::getOrderCount() const {
    return orderMap.size();
}

std::vector<const PriceLevel*> OrderBook::getLevels(Side side, std::size_t maxLevels) const {
    // In-order walk with an explicit stack, stopping after maxLevels.
    // Asks go left-first (ascending), bids right-first (descending), so the best comes first.
    const RedBlackTree* tree = getTree(side);
    const PriceLevel* nil = tree->getNil();
    const bool ascending = (side == Side::SELL);
    std::vector<const PriceLevel*> levels;
    std::vector<const PriceLevel*> stack;
    const PriceLevel* node = tree->getRoot();
    while (levels.size() < maxLevels && ((node != nullptr && node != nil) || !stack.empty())) {
        while (node != nullptr && node != nil) {
            stack.push_back(node);
            node = ascending ? node->getLeftChild() : node->getRightChild();
        }
        node = stack.back();
        stack.pop_back();
        levels.push_back(node);
        node = ascending ? node->getRightChild() : node->getLeftChild();
    }
    return levels;
}

void OrderBook::insertPriceLevel(std::int32_t price, Side side) {
    if (side == Side::BUY) { 
        if (buyPriceLevelMap.find(price) == buyPriceLevelMap.end()) {
            PriceLevel* newLevel = buyTree->insert(price);
            buyPriceLevelMap[price] = newLevel;
            if (bestBid == nullptr || price > bestBid->getPrice()) {
                setBestBid(newLevel);
            }
        }
    } else {
        if (sellPriceLevelMap.find(price) == sellPriceLevelMap.end()) {
            PriceLevel* newLevel = sellTree->insert(price);
            sellPriceLevelMap[price] = newLevel;
            if (bestAsk == nullptr || price < bestAsk->getPrice()) {
                setBestAsk(newLevel);
            }
        }
    }
}

void OrderBook::removePriceLevel(std::int32_t price, Side side) {
    
    if (side == Side::SELL) {
        auto it = sellPriceLevelMap.find(price);
        if (it != sellPriceLevelMap.end()) {
            PriceLevel* levelToRemove = it->second;
            // Find the next best ask before remove() relinks the tree:
            // leftmost of the right subtree, or the parent (nullptr if the side empties)
            if (levelToRemove == bestAsk) {
                PriceLevel* nil = sellTree->getNil();
                PriceLevel* next = levelToRemove->getRightChild();
                if (next != nil) {
                    while (next->getLeftChild() != nil) {
                        next = next->getLeftChild();
                    }
                } else {
                    next = levelToRemove->getParent();
                }
                setBestAsk(next);
            }
            sellTree->remove(levelToRemove);
            sellPriceLevelMap.erase(it);
            delete levelToRemove;
        }
    } else {
        auto it = buyPriceLevelMap.find(price);
        if (it != buyPriceLevelMap.end()) {
            PriceLevel* levelToRemove = it->second;
            // Find the next best bid before remove() relinks the tree:
            // rightmost of the left subtree, or the parent (nullptr if the side empties)
            if (levelToRemove == bestBid) {
                PriceLevel* nil = buyTree->getNil();
                PriceLevel* next = levelToRemove->getLeftChild();
                if (next != nil) {
                    while (next->getRightChild() != nil) {
                        next = next->getRightChild();
                    }
                } else {
                    next = levelToRemove->getParent();
                }
                setBestBid(next);
            }
            buyTree->remove(levelToRemove);
            buyPriceLevelMap.erase(it);
            delete levelToRemove;
        }
    }
}
