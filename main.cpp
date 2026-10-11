#include <iostream>
#include "price_level/price_level.h"
#include "order_book/order_book.h"
int main() {
    OrderBook* orderBook = new OrderBook();
    std::cout << sizeof(orderBook) << std::endl;
    return 0;
}
