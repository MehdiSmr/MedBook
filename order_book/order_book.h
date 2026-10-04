#pragma once
#include <unordered_map>
#include <cstdint>
#include "../order/order.h"
#include "../price_level/price_level.h"
#include "../side/side.h"
#include "../order_pool/order_pool.h"

class OrderBook {
private:
    std::unordered_map<std::uint64_t, Order*> orderMap;
    std::unordered_map<std::int32_t, PriceLevel*> buyPriceLevelMap;
    std::unordered_map<std::int32_t, PriceLevel*> sellPriceLevelMap;
    OrderPool orderPool; 
    PriceLevel* buyTree;
    PriceLevel* sellTree;
    std::uint64_t idCounter;
    
public:
    OrderBook(int poolSize = ORDER_POOL_SIZE);
    ~OrderBook(); 
    uint64_t addOrder(std::int32_t quantity, std::int32_t price, std::uint64_t sequence, Side side );
    void removeOrder(std::uint64_t orderId);
    void modifyOrder(std::uint64_t orderId, std::int32_t newQuantity, std::int32_t newPrice);
    Order* getOrder(std::uint64_t orderId) const;
    PriceLevel* getBestBid() const; 
    PriceLevel* getBestAsk() const;
    void insertPriceLevel(std::int32_t price);
    void removePriceLevel(std::int32_t price); 
};
