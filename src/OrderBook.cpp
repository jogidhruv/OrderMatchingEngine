#include "engine/OrderBook.hpp"
#include "engine/MatchEngine.hpp"

namespace Engine {
    bool OrderBook::orderExists(const OrderRequest &order) const {
        return orders_.contains({ order.clientId, order.clientOrderId });
    }

    bool OrderBook::insert(OrderTypes& order) {
        if (index_.contains(order.id)) {
            return false;
        }

        auto& level = order.is_buy() ? bids_[order.price] : asks_[order.price];
        level.insert(level.end(), order);
        index_.emplace(order.id, --level.end());
        // Duplicate orders are already checked before reaching here
        orders_.insert({ {order.client_id, order.client_order_id}, order.id });
        return true;
    }

    bool OrderBook::remove(const uint64_t clientId, const uint64_t clientOrderId) {
        // Resolve the client key -> global order id. Absent means there is no
        // active order to remove (e.g. a cancel of an already-completed order):
        // return false and leave every structure untouched.
        const auto oit = orders_.find({ clientId, clientOrderId });
        if (oit == orders_.end()) {
            return false;
        }

        auto it = index_.find(oit->second);
        if (it == index_.end()) {
            // index_ and orders_ are meant to stay in lockstep; if the index is
            // somehow missing, still drop the stale client-key entry.
            orders_.erase(oit);
            return false;
        }

        auto node = it->second;
        const bool isBuy = node->is_buy();
        auto level = isBuy ? bids_.find(node->price) : asks_.find(node->price);
        level->second.erase(node);
        if (level->second.empty()) {
            if (isBuy) bids_.erase(level);
            else asks_.erase(level);
        }
        index_.erase(it);
        orders_.erase(oit);
        return true;
    }

    OrderTypes* OrderBook::find(const uint64_t clientId, const uint64_t clientOrderId) {
        auto oit = orders_.find({ clientId, clientOrderId });
        if (oit == orders_.end()) {
            return nullptr;
        }
        auto it = index_.find(oit->second);
        return it == index_.end() ? nullptr : &(*it->second);
    }

    OrderTypes* OrderBook::best_bid() {
        return bids_.empty() ? nullptr : &bids_.begin()->second.front();
    }

    OrderTypes* OrderBook::best_ask() {
        return asks_.empty() ? nullptr : &asks_.begin()->second.front();
    }

    bool OrderBook::empty() const {
        return index_.empty();
    }

    const OrderBook::BidMap& OrderBook::bids() const {
        return bids_;
    }

    const OrderBook::AskMap& OrderBook::asks() const {
        return asks_;
    }

    MarketData OrderBook::top_of_book() const
    {
        MarketData data{};
        if (!asks_.empty()) {
            data.has_ask = true;
            data.ask_price = asks_.begin()->first;
            for (const auto& order : asks_.begin()->second) {
                data.ask_quantity += order.remaining;
            }
        }

        if (!bids_.empty()) {
            data.has_bid = true;
            data.bid_price = bids_.begin()->first;
            for (const auto& order : bids_.begin()->second) {
                data.bid_quantity += order.remaining;
            }
        }

        return data;
    }
}  // namespace engine
