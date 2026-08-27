#pragma once
#include <cstdint>
#include <vector>

#include "MarketData.hpp"
#include "OrderBook.hpp"
#include "OrderPool.hpp"
#include "Trade.hpp"
#include "Types.hpp"

using namespace std;

namespace Engine
{
    // Status of the order after immediate matching once received
    enum class OrderStatus : uint8_t
    {
        FILLED = 1,
        CANCELLED = 2,
        RESTING = 3,
        MODIFIED = 4,
        UNKNOWN = 5
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

    class MatchingEngine
    {
    public:

        MatchingEngine() = default;

        // Invoked by the consumer thread of SymbolWorker to actually execute the order
        SubmitResult submit(uint64_t orderId, const OrderRequest& request);

        // Used for telemetry, logging etc.
        [[nodiscard]] const OrderBook& book() const;

    private:

        // Match the incoming request with existing opposite order book
        void match(Order& incoming, vector<Trade>& out, bool& is_self);

        // Execute the matched orders with the opposite order book
        void execute(Order& maker, Order& taker, vector<Trade>& out);

        // Check for duplicate or unknown orders before executing the order.
        [[nodiscard]] RejectReason validate(const OrderRequest& order) const;

        // Helper for cancel order
        SubmitResult cancel(const OrderRequest& request);

        // Helper for modify order
        SubmitResult modify(const OrderRequest& request);

        // Helper for Market, Limit, IOC orders
        SubmitResult place(uint64_t orderId, const OrderRequest& request);

        OrderBook book_;
    };


}