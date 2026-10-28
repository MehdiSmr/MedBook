#pragma once
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include "../command/command.h"
#include "../order_book/order_book.h"
#include "../trade/trade.h"

class MatchingEngine {
public:
    using TradeListener = std::function<void(const Trade&)>;

    // One book per symbol, fixed for the engine's lifetime. Trades for each symbol
    // go to <outputDir>/trades_<symbol>.csv ('/' and other non-alphanumerics become '_').
    MatchingEngine(const std::vector<std::string>& symbols, int poolSize = ORDER_POOL_SIZE,
                   const std::string& outputDir = ".");

    // Returns the order id (new id after a price change or quantity increase), or 0 for a cancel.
    // Throws std::invalid_argument for an unknown symbol or an invalid command.
    std::uint64_t processCommand(const std::string& symbol, const Command& command);

    const OrderBook& getOrderBook(const std::string& symbol) const;
    bool hasSymbol(const std::string& symbol) const;
    // Called for every trade, after it is written to the trade file (e.g. for live monitoring).
    void setTradeListener(TradeListener listener);
    void printOrderBook(const std::string& symbol, std::ostream& out = std::cout) const;

private:
    std::unordered_map<std::string, OrderBook> order_books; // Map to hold order books for each symbol
    std::unordered_map<std::string, std::ofstream> tradeFiles; // Map to hold trade output files for each symbol
    std::uint64_t tradeSequence = 0; // Sequence number for trades, shared across symbols
    TradeListener tradeListener;

    OrderBook& getBook(const std::string& symbol);
    std::ofstream& getTradeFile(const std::string& symbol); // Get the trade output file for a specific symbol
    void writeTradeToFile(const Trade& trade); // Write trades to an output file
    std::uint64_t processAddCommand(const std::string& symbol, const AddCommand& addCommand);
    std::uint64_t processModifyCommand(const std::string& symbol, const ModifyCommand& modifyCommand);
    std::uint64_t processCancelCommand(const std::string& symbol, const CancelCommand& cancelCommand);
    // Matches an incoming order against the opposite side, then rests any remainder.
    std::uint64_t match(const std::string& symbol, Side side, std::int32_t quantity, std::int32_t price);
};
