#pragma once
#include <cstdint>

struct Trade 
{ 
    std::uint64_t restingId; 
    std::uint64_t aggressorId; 
    std::int64_t quantity; 
    std::int64_t price; 
    std::int64_t sequence; 
};
