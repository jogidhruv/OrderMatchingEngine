#pragma once
#include <cstdint>
#include <vector>

#include "OrderBook.hpp"
#include "MatchEngineTypes.hpp"
#include "Types.hpp"

using namespace std;

namespace Engine
{
    class MatchingEngine
    {
    public:

        MatchingEngine() = default;

        // Invoked by the consumer thread of SymbolWorker to actually execute the order
        SubmitResult submit(const OrderRequest& request);

        // Used for telemetry, logging etc.
        [[nodiscard]] const OrderBook& book() const;

    private:

        // Match the incoming request with existing opposite order book
        void match(OrderTypes& incoming, vector<Trade>& out, bool& is_self);

        // Execute the matched orders with the opposite order book
        void execute(OrderTypes& maker, OrderTypes& taker, vector<Trade>& out);

        // Check for duplicate or unknown orders before executing the order.
        [[nodiscard]] RejectReason validate(const OrderRequest& order) const;

        // Helper for cancel order
        SubmitResult cancel(const OrderRequest& request);

        // Helper for modify order
        SubmitResult modify(const OrderRequest& request);

        // Helper for Market, Limit, IOC orders
        SubmitResult place(const OrderRequest& request);

        OrderBook book_;
    };


}