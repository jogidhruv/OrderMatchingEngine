#pragma once
#include <cstdint>
#include <vector>

#include "OrderTypes.hpp"

namespace Engine {
    // Used in SubmitResult to denote the executed trades
    struct Trade
    {
        uint64_t maker_order_id;
        uint64_t taker_order_id;
        uint64_t price;
        uint64_t quantity;
    };

    struct SubmitResult
    {
        OrderStatus status;
        uint64_t clientId;
        uint64_t clientOrderId;
        uint64_t filled_quantity;
        vector<Trade> trades;  // Trades that happened while matching the request
        RejectReason rejectReason;  // The request could get rejected because of invalid OrderId (from client)
    };

    // This is used by the Sink for logging
    struct MatchingResult {
        SubmitResult result;
        MarketData data;
    };
}
