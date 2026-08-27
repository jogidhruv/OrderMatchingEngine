#pragma once
#include <cstdint>

namespace Engine
{
    // Used in SubmitResult to denote the executed trades
    struct Trade
    {
        uint64_t maker_order_id;
        uint64_t taker_order_id;
        uint64_t price;
        uint64_t quantity;
    };
}

