#pragma once
#include <unordered_map>
#include <cstdint>
#include <vector>
#include "../order/order.h"
#include "../price_level/price_level.h"
#include "../side/side.h"
#include "../order_pool/order_pool.h"

class OrderBook {
public:
    class RedBlackTree {
    public: 
        RedBlackTree();
        ~RedBlackTree(); 
        PriceLevel* insert(std::int32_t price);
        void remove(PriceLevel* nodeToRemove);
        PriceLevel* getRoot() const;
        PriceLevel* getNil() const;
    private:
        void fixInsert(PriceLevel* z);
        void fixRemove(PriceLevel* x); 
        void transplant(PriceLevel* u, PriceLevel* v);
        void leftRotate(PriceLevel* x); 
        void rightRotate(PriceLevel* y);
        PriceLevel* nil;
        PriceLevel* root;
    };
    OrderBook(int poolSize = ORDER_POOL_SIZE);
    ~OrderBook(); 
    std::uint64_t nextOrderId(); // Hands out a fresh id without adding an order
    std::uint64_t addOrder(std::int32_t quantity, std::int32_t price, Side side );
    // id must come from nextOrderId(); alreadyTraded marks the rest of an incoming order that partly filled.
    std::uint64_t addOrder(std::uint64_t orderId, std::int32_t quantity, std::int32_t price, Side side,
                           bool alreadyTraded = false);
    void fillOrder(std::uint64_t orderId, std::int32_t quantity); // Reduces remaining; removes the order when it reaches 0
    void removeOrder(std::uint64_t orderId);
    std::uint64_t modifyOrder(std::uint64_t orderId, std::int32_t newQuantity, std::int32_t newPrice);
    Order* getOrder(std::uint64_t orderId) const;
    PriceLevel* getBestBid() const;
    PriceLevel* getBestAsk() const;
    const RedBlackTree* getTree(Side side) const;
    std::size_t getOrderCount() const; // Resting orders, both sides
    // Up to maxLevels levels on one side, best price first (highest bid / lowest ask).
    std::vector<const PriceLevel*> getLevels(Side side, std::size_t maxLevels) const;
    void insertPriceLevel(std::int32_t price, Side side);
    void removePriceLevel(std::int32_t price, Side side);

private:
    void setBestBid(PriceLevel* level);
    void setBestAsk(PriceLevel* level);
    std::unordered_map<std::uint64_t, Order*> orderMap;
    std::unordered_map<std::int32_t, PriceLevel*> buyPriceLevelMap;
    std::unordered_map<std::int32_t, PriceLevel*> sellPriceLevelMap;
    OrderPool orderPool; 
    RedBlackTree* buyTree;
    PriceLevel* bestBid;
    RedBlackTree* sellTree;
    PriceLevel* bestAsk;
    std::uint64_t idCounter;
};
