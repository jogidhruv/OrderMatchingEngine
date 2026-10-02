#pragma once

#include <list>
#include <flat_map>
#include <unordered_map>

#include "OrderList.hpp"
#include "OrderPool.hpp"
#include "OrderTypes.hpp"

namespace Engine
{
    class OrderBook
    {
        public:
            using AskMap = std::flat_map<uint64_t, std::list<OrderTypes>>;
            using BidMap = std::flat_map<uint64_t, std::list<OrderTypes>, greater<>>;

            // Inserts the order into internal structures. Updates AskMap / BidMap, index for quickly retrieving
            // the order for deletions and client-key -> globally unique order ID map.
            bool insert(OrderTypes& order);

            // Removes the active order identified by the client key
            // (clientId, clientOrderId) from EVERY internal structure: the price
            // level list (dropping the level if it empties), the global-id index,
            // and the client-key map. Returns false (graceful no-op) when no such
            // active order exists.
            bool remove(uint64_t clientId, uint64_t clientOrderId);

            OrderTypes* take_from_pool() { return order_pool_.take(); };

            // Read Only accesses
            [[nodiscard]] const AskMap& asks() const;
            [[nodiscard]] const BidMap& bids() const;

            [[nodiscard]] OrderTypes* best_bid();
            [[nodiscard]] OrderTypes* best_ask();
            [[nodiscard]] bool empty() const;

            [[nodiscard]] MarketData top_of_book() const;
            [[nodiscard]] bool orderExists(const OrderRequest& order) const;
            [[nodiscard]] OrderTypes* find(uint64_t clientId, uint64_t clientOrderId);

        private:
            AskMap asks_;
            BidMap bids_;
            OrderPool order_pool_;
            //unordered_map<uint64_t, Order*> index_;  // Global unique order ID -> Iterator of the actual Order in the maps

            using Key = std::pair<uint64_t, uint64_t>;

            // Hash functor for Key
            struct KeyHash {
                std::size_t operator()(const Key& p) const noexcept {
                    auto h1 = std::hash<uint64_t>{}(p.first);
                    auto h2 = std::hash<uint64_t>{}(p.second);
                    // hash_combine style
                    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
                }
            };

            // Equality functor for Key
            struct KeyEqual {
                bool operator()(const Key& a, const Key& b) const noexcept {
                    return a.first == b.first && a.second == b.second;
                }
            };

            // Used for checking duplicate and unknown orders from clients
            std::unordered_map<Key, std::list<OrderTypes>::iterator, KeyHash, KeyEqual> index_;
    };

}
