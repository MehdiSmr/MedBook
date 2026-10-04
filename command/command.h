#pragma once
#include <cstdint>
#include <variant>
#include "../side/side.h"
#include "../action/action.h"
#include "../order/order.h"

struct AddCommand { std::uint64_t orderId; Side side; std::int32_t quantity; std::int32_t price; };
struct MarketCommand { Side side; std::int32_t quantity; };
struct ModifyCommand { std::uint64_t orderId; std::int32_t newQuantity; std::int32_t newPrice; };
struct CancelCommand { std::uint64_t orderId; };

using Command = std::variant<AddCommand, MarketCommand, ModifyCommand, CancelCommand>;

