#pragma once
#include <unordered_map>
#include <cstdint>
#include "../order/order.h"
#include "../price_level/price_level.h"

class OrderBook {
private:
    std::unordered_map<std::uint64_t, Order*> orderMap;
    std::unordered_map<std::int32_t, PriceLevel*> buyPriceLevelMap;
    std::unordered_map<std::int32_t, PriceLevel*> sellPriceLevelMap;
    PriceLevel* buyTree;
    PriceLevel* sellTree; 
    
public:
    OrderBook();
    ~OrderBook(); 
    void addOrder(); // to figure out how to support add command and execute command and defer the trade exec to matching engine 
    void removeOrder(std::uint64_t orderId);
    void modifyOrder(std::uint64_t orderId, std::int32_t newQuantity, std::int32_t newPrice);
    Order* getOrder(std::uint64_t orderId) const;
    PriceLevel* getBestBid() const; 
    PriceLevel* getBestAsk() const;
    void insertPriceLevel(std::int32_t price);
    void removePriceLevel(std::int32_t price); 
};
