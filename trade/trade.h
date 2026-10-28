#pragma once
#include <cstdint>
#include <string>
struct Trade 
{
    std::string symbol;
    std::uint64_t restingId; 
    std::uint64_t aggressorId; 
    std::int32_t quantity; 
    std::int32_t price; 
    std::uint64_t sequence; 
};
