#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include "matching_engine.h"
#include "../price_level/price_level.h"

namespace {

// "BTC/USD" -> "BTC_USD": a '/' in a filename would point into a folder.
std::string fileSafe(const std::string& symbol) {
    std::string out = symbol;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    }
    return out;
}

// In-order walk, so levels come out lowest price first.
void collectLevels(const PriceLevel* node, const PriceLevel* nil, std::vector<const PriceLevel*>& out) {
    if (node == nullptr || node == nil) return;
    collectLevels(node->getLeftChild(), nil, out);
    out.push_back(node);
    collectLevels(node->getRightChild(), nil, out);
}

std::int32_t levelQuantity(const PriceLevel* level) {
    std::int32_t total = 0;
    for (const Order* o = level->getHead(); o; o = o->getNext()) total += o->getRemaining();
    return total;
}

} // namespace

MatchingEngine::MatchingEngine(const std::vector<std::string>& symbols, int poolSize,
                               const std::string& outputDir) {
    std::unordered_set<std::string> fileNames;
    for (const std::string& symbol : symbols) {
        if (order_books.contains(symbol)) {
            throw std::invalid_argument("Duplicate symbol: " + symbol);
        }
        std::string fileName = "trades_" + fileSafe(symbol) + ".csv";
        if (!fileNames.insert(fileName).second) {
            // e.g. "BTC/USD" and "BTC_USD" would write to the same file
            throw std::invalid_argument("Symbols share a trade file: " + fileName);
        }
        std::ofstream file(std::filesystem::path(outputDir) / fileName);
        if (!file) {
            throw std::runtime_error("Could not open trade file: " + fileName);
        }
        file << "sequence,symbol,restingId,aggressorId,price,quantity\n";

        order_books.try_emplace(symbol, poolSize); // OrderBook can't be copied or moved, so build it in place
        tradeFiles.emplace(symbol, std::move(file));
    }
}

std::uint64_t MatchingEngine::processCommand(const std::string& symbol, const Command& command) {
    getBook(symbol); // Reject unknown symbols before doing anything
    return std::visit([&](const auto& cmd) -> std::uint64_t {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, AddCommand>) return processAddCommand(symbol, cmd);
        else if constexpr (std::is_same_v<T, ModifyCommand>) return processModifyCommand(symbol, cmd);
        else return processCancelCommand(symbol, cmd);
    }, command);
}

const OrderBook& MatchingEngine::getOrderBook(const std::string& symbol) const {
    auto it = order_books.find(symbol);
    if (it == order_books.end()) {
        throw std::invalid_argument("Unknown symbol: " + symbol);
    }
    return it->second;
}

OrderBook& MatchingEngine::getBook(const std::string& symbol) {
    auto it = order_books.find(symbol);
    if (it == order_books.end()) {
        throw std::invalid_argument("Unknown symbol: " + symbol);
    }
    return it->second;
}

std::ofstream& MatchingEngine::getTradeFile(const std::string& symbol) {
    return tradeFiles.at(symbol);
}

void MatchingEngine::writeTradeToFile(const Trade& trade) {
    // '\n' rather than std::endl: the stream flushes in chunks instead of on every trade
    getTradeFile(trade.symbol) << trade.sequence << ',' << trade.symbol << ','
                               << trade.restingId << ',' << trade.aggressorId << ','
                               << trade.price << ',' << trade.quantity << '\n';
}

std::uint64_t MatchingEngine::processAddCommand(const std::string& symbol, const AddCommand& addCommand) {
    if (addCommand.quantity <= 0 || addCommand.price <= 0) {
        throw std::invalid_argument("Quantity and price must be positive");
    }
    return match(symbol, addCommand.side, addCommand.quantity, addCommand.price);
}

std::uint64_t MatchingEngine::processModifyCommand(const std::string& symbol, const ModifyCommand& modifyCommand) {
    OrderBook& book = getBook(symbol);
    if (modifyCommand.newQuantity <= 0 || modifyCommand.newPrice <= 0) {
        throw std::invalid_argument("New quantity and price must be positive");
    }
    const Order* order = book.getOrder(modifyCommand.orderId);
    if (order == nullptr) {
        throw std::invalid_argument("Order ID not found");
    }

    // Same price can't cross the book, so the book's own modify rules apply
    // (a reduction keeps the id and priority, an increase gets a new id at the back).
    if (order->getPrice() == modifyCommand.newPrice) {
        return book.modifyOrder(modifyCommand.orderId, modifyCommand.newQuantity, modifyCommand.newPrice);
    }

    // Same rule as OrderBook::modifyOrder: a partially filled order can't be modified.
    if (order->getRemaining() != order->getQuantity()) {
        throw std::invalid_argument("Cannot modify an order that has already been partially filled");
    }

    // A price change is a new incoming order: take it out, then match it like an add.
    Side side = order->getSide();
    book.removeOrder(modifyCommand.orderId);
    return match(symbol, side, modifyCommand.newQuantity, modifyCommand.newPrice);
}

std::uint64_t MatchingEngine::processCancelCommand(const std::string& symbol, const CancelCommand& cancelCommand) {
    getBook(symbol).removeOrder(cancelCommand.orderId);
    return 0;
}

std::uint64_t MatchingEngine::match(const std::string& symbol, Side side, std::int32_t quantity, std::int32_t price) {
    OrderBook& book = getBook(symbol);
    // The aggressor gets its id up front, so trades can name it even if it never rests.
    std::uint64_t aggressorId = book.nextOrderId();
    std::int32_t remaining = quantity;

    while (remaining > 0) {
        PriceLevel* best = (side == Side::BUY) ? book.getBestAsk() : book.getBestBid();
        if (best == nullptr) break; // Opposite side is empty

        bool crosses = (side == Side::BUY) ? best->getPrice() <= price : best->getPrice() >= price;
        if (!crosses) break; // Best opposite price is past our limit

        Order* resting = best->getHead(); // Oldest order at the best price
        std::int32_t fillQuantity = std::min(remaining, resting->getRemaining());

        // Build the trade before filling: a full fill frees the resting order and maybe its level.
        Trade trade{symbol, resting->getOrderId(), aggressorId, fillQuantity, best->getPrice(), ++tradeSequence};
        book.fillOrder(resting->getOrderId(), fillQuantity);
        writeTradeToFile(trade);
        remaining -= fillQuantity;
    }

    if (remaining > 0) {
        book.addOrder(aggressorId, remaining, price, side);
    }
    return aggressorId;
}

void MatchingEngine::printOrderBook(const std::string& symbol, std::ostream& out) const {
    const OrderBook& book = getOrderBook(symbol);
    const OrderBook::RedBlackTree* asks = book.getTree(Side::SELL);
    const OrderBook::RedBlackTree* bids = book.getTree(Side::BUY);

    std::vector<const PriceLevel*> askLevels, bidLevels;
    collectLevels(asks->getRoot(), asks->getNil(), askLevels);
    collectLevels(bids->getRoot(), bids->getNil(), bidLevels);

    // Highest price at the top: asks above the spread, bids below.
    out << "== " << symbol << " ==\n";
    out << "ASKS (price qty orders)\n";
    for (auto it = askLevels.rbegin(); it != askLevels.rend(); ++it) {
        out << "  " << (*it)->getPrice() << ' ' << levelQuantity(*it) << ' ' << (*it)->getDepth() << '\n';
    }
    out << "----\n";
    out << "BIDS (price qty orders)\n";
    for (auto it = bidLevels.rbegin(); it != bidLevels.rend(); ++it) {
        out << "  " << (*it)->getPrice() << ' ' << levelQuantity(*it) << ' ' << (*it)->getDepth() << '\n';
    }
}
