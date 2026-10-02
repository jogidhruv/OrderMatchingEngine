#include <engine/MatchEngine.hpp>

#include <chrono>

#include "engine/Clock.hpp"

namespace Engine
{
    const OrderBook& MatchingEngine::book() const {
        return book_;
    }

    void MatchingEngine::execute(OrderTypes& maker, OrderTypes& taker, vector<Trade>& out) {
        const uint64_t quantity = min(maker.remaining, taker.remaining);
        taker.remaining -= quantity;
        out.push_back(Trade{ .maker_order_id = maker.client_order_id,
            .taker_order_id = taker.client_order_id, .price = maker.price, .quantity = quantity });

        if (maker.remaining == quantity) {
            // maker is the resting order so needs to be cleaned up if fully filled
            book_.remove(maker.client_id, maker.client_order_id);
        }
        // taker is incoming so it will be handled after all the immediate matches are executed
    }

    void MatchingEngine::match(OrderTypes &incoming, vector<Trade> &out, bool& is_self) {
        const bool is_market = incoming.type == OrderType::Market;
        while (incoming.remaining > 0) {
            OrderTypes* maker = incoming.is_buy() ? book_.best_ask() : book_.best_bid();
            if (!maker) break;

            if (maker->client_id == incoming.client_id) {
                is_self = true; // STP
                break;
            }

            const auto price_diff = static_cast<int64_t>(maker->price) - static_cast<int64_t>(incoming.price);
            if (const auto dir = (price_diff >> 63) & 1; !is_market & (price_diff != 0) & (incoming.is_buy() ^ dir)) {
                break;
            }

            execute(*maker, incoming, out);
        }
    }

    RejectReason MatchingEngine::validate(const OrderRequest &order) const {
        // Checks unknown and duplicate order IDs
        RejectReason reason = RejectReason::None;
        if (order.type == OrderType::Modify || order.type == OrderType::Cancel) {
            if (!book_.orderExists(order)) {
                reason = RejectReason::UnknownOrderId;
            }
        }
        else {
            if (book_.orderExists(order)) {
                reason = RejectReason::DuplicateOrderId;
            }
        }

        return reason;
    }

    SubmitResult MatchingEngine::cancel(const OrderRequest &request) {
        // Reach here only when validate() has proven the order exists and can be
        // cancelled. Resolve purely by client key.
        book_.remove(request.clientId, request.clientOrderId);
        SubmitResult result{};
        result.status = OrderStatus::CANCELLED;
        result.clientId = request.clientId;
        result.clientOrderId = request.clientOrderId;
        return result;
    }

    SubmitResult MatchingEngine::submit(uint64_t orderId, const OrderRequest& request) {
        if (const RejectReason reason = validate(request); reason != RejectReason::None) {
            SubmitResult result{};
            result.rejectReason = reason;
            result.clientId = request.clientId;
            result.clientOrderId = request.clientOrderId;
            return result;
        }

        switch (request.type) {
            case OrderType::Cancel: {
                return cancel(request);
            }
            case OrderType::Modify: {
                return modify(request);
            }
            default: ;
        }

        return place(orderId, request);
    }


    SubmitResult MatchingEngine::place(uint64_t orderId, const OrderRequest& request) {
        //Order* incoming = book_.take_from_pool();
        OrderTypes incoming;
        incoming.id = orderId;
        incoming.client_id = request.clientId;
        incoming.client_order_id = request.clientOrderId;
        incoming.side = request.side;
        incoming.type = request.type;
        incoming.quantity = request.quantity;
        incoming.remaining = request.quantity;
        incoming.price = request.price;
        incoming.timestamp = now_ns();

        SubmitResult result;
        result.rejectReason = RejectReason::None;
        result.clientId = request.clientId;
        result.clientOrderId = request.clientOrderId;
        bool is_self = false;
        match(incoming, result.trades, is_self);
        result.filled_quantity = request.quantity - incoming.remaining;

        if (incoming.remaining == 0) {
            result.status = OrderStatus::FILLED;
        }
        else if (request.type == OrderType::Market || request.type == OrderType::IOC) {
            // Market/IOC leftover is never rested: liquidity is exhausted (or STP
            // triggered), so the unfilled remainder is cancelled.
            result.status = OrderStatus::CANCELLED;
        }
        else if (is_self) {
            result.status = OrderStatus::CANCELLED;
        }
        else {
            book_.insert(incoming);
            result.status = OrderStatus::RESTING; // Limit remainder rests on the book
        }

        return result;
    }

    SubmitResult MatchingEngine::modify(const OrderRequest& request) {
        SubmitResult result;
        result.clientId = request.clientId;
        result.clientOrderId = request.clientOrderId;

        const uint64_t new_quantity = request.quantity;
        result.rejectReason = RejectReason::None;
        if (new_quantity == 0) {
            // A modify down to zero quantity is treated as a cancel.
            book_.remove(request.clientId, request.clientOrderId);
            result.status = OrderStatus::CANCELLED;
            return result;
        }

        OrderTypes* resting = book_.find(request.clientId, request.clientOrderId);
        // Valid order should always be there because checked earlier
        const uint64_t new_price = request.price;
        const bool price_changed = (new_price != resting->price);
        // Compare against REMAINING: an increase relative to the live remaining
        // quantity loses time priority; anything else may be applied in place.
        const bool quantity_inc = (new_quantity > resting->remaining);
        if (!price_changed && !quantity_inc) {
            // Quantity decrease (or no change) at the same price retains priority.
            resting->remaining = new_quantity;
            result.status = OrderStatus::MODIFIED;
            return result;
        }

        // Priority-resetting change: pull the order and re-enter it. Copy first
        // because remove() invalidates the resting pointer. The global id is
        // preserved through the copy.
        //Order* replacement = book_.take_from_pool();
        OrderTypes replacement;
        replacement.client_id = resting->client_id;
        replacement.client_order_id = resting->client_order_id;
        replacement.side = resting->side;
        replacement.type = resting->type;
        replacement.id = resting->id;
        replacement.price = new_price;
        replacement.quantity = new_quantity;
        replacement.remaining = new_quantity;
        replacement.timestamp = now_ns();
        book_.remove(resting->client_id, resting->client_order_id);

        bool is_self = false;
        if (price_changed) {
            // Only a price change can cross the book; a pure quantity increase
            // re-rests at the same price without matching.
            match(replacement, result.trades, is_self);
            result.filled_quantity = new_quantity - replacement.remaining;
        }

        if (replacement.remaining == 0) {
            result.status = OrderStatus::FILLED;
        }
        else if (is_self) {
            result.status = OrderStatus::CANCELLED;
        }
        else {
            book_.insert(replacement);
            result.status = OrderStatus::MODIFIED;
        }

        return result;
    }
}



