#include <iostream>
#include "order_pool/order_pool.h"
#include "order/order.h"


int main() {
    OrderPool orderPool; // Create an OrderPool with 1000 orders 
    Order *order = orderPool.createOrder(1, 100, 50, 1, Side::BUY);
    orderPool.destroyOrder(order);
    Order *order2 = orderPool.createOrder(2, 200, 60, 2, Side::SELL);
    std::cout << "Order ID: " << order2->getOrderId() << ", Quantity: " << order2->getQuantity() 
              << ", Price: " << order2->getPrice() << ", Side: " 
              << (order2->getSide() == Side::BUY ? "BUY" : "SELL") << std::endl;
    return 0;
}
