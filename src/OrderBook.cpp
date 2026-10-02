#include "engine/OrderBook.hpp"

namespace Engine {
    bool OrderBook::orderExists(const OrderRequest &order) const {
        return index_.contains({ order.clientId, order.clientOrderId });
    }

    OrderTypes* OrderBook::find(const uint64_t clientId, const uint64_t clientOrderId) {
        const auto it = index_.find({clientId, clientOrderId});
        if (it == index_.end()) return nullptr;
        return &*it->second;
    }

    bool OrderBook::insert(OrderTypes& order) {
        // Validation step already confirms this is a new order
        auto& level = order.is_buy() ? bids_[order.price] : asks_[order.price];
        level.insert(level.end(), order);
        index_.insert({{order.client_id, order.client_order_id}, --level.end()});
        return true;
    }

    bool OrderBook::remove(const uint64_t clientId, const uint64_t clientOrderId) {
        const auto it = index_.find({ clientId, clientOrderId });
        if (it == index_.end()) {
            return false;
        }

        auto node = it->second;
        if (node->is_buy()) {
            auto level = bids_.find(node->price);
            if (level->second.size() == 1) {
                bids_.erase(level);
            }
            else {
                level->second.erase(node);
            }
        }
        else {
            auto level = asks_.find(node->price);
            if (level->second.size() == 1) {
                asks_.erase(level);
            }
            else {
                level->second.erase(node);
            }
        }
        index_.erase(it);
        return true;
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
