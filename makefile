all: clean classes

clean:
	rm -f main

classes:
	clang++ -Wall -Werror -std=c++23 -o main main.cpp order/order.cpp price_level/price_level.cpp order_pool/order_pool.cpp order_book/order_book.cpp
