#pragma once
#include <cstdint>

namespace Engine {
    // Top of the order book data used while logging market data
    struct MarketData {
        bool has_bid{false};
        bool has_ask{false};
        uint64_t ask_price{0};
        uint64_t bid_price{0};
        uint64_t ask_quantity{0};
        uint64_t bid_quantity{0};
    };
}
