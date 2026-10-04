#pragma once
#include <cstdint>

struct Trade 
{ 
    std::uint64_t restingId; 
    std::uint64_t aggressorId; 
    std::int32_t quantity; 
    std::int32_t price; 
    std::uint64_t sequence; 
};
