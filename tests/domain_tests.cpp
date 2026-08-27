// ---------------------------------------------------------------------------
// Domain unit tests for the matching engine.
// ---------------------------------------------------------------------------
// These exercise MatchingEngine::submit directly: it is pure and single-threaded,
// so every outcome is deterministic (no sink, no files, no worker threads). A few
// cases at the end drive Exchange::submit to cover synchronous field validation
// (invalid price / quantity / type / unknown symbol), which lives above the
// engine. Order ids are assigned by a local counter per test to mimic the global
// generator; matching never depends on the id value, only on price

#include "test_framework.hpp"

#include "engine/Exchange.hpp"
#include "engine/MatchEngine.hpp"
#include "engine/Order.hpp"
#include "engine/Types.hpp"

using namespace Engine;

namespace {

// --- Request builders ------------------------------------------------------

OrderRequest limit(uint64_t cid, uint64_t coid, Side s, uint64_t px, uint64_t qty) {
    return OrderRequest{cid, coid, px, qty, OrderType::Limit, s};
}
OrderRequest market(uint64_t cid, uint64_t coid, Side s, uint64_t qty) {
    return OrderRequest{cid, coid, 0, qty, OrderType::Market, s};
}
OrderRequest ioc(uint64_t cid, uint64_t coid, Side s, uint64_t px, uint64_t qty) {
    return OrderRequest{cid, coid, px, qty, OrderType::IOC, s};
}
OrderRequest cancel(uint64_t cid, uint64_t coid) {
    return OrderRequest{cid, coid, 0, 0, OrderType::Cancel, Side::BUY};
}
OrderRequest modify(uint64_t cid, uint64_t coid, uint64_t px, uint64_t qty) {
    return OrderRequest{cid, coid, px, qty, OrderType::Modify, Side::BUY};
}

// Thin wrapper that feeds an incrementing global order id, like the exchange.
struct Harness {
    MatchingEngine engine;
    uint64_t next_id = 1;
    SubmitResult submit(const OrderRequest& r) { return engine.submit(next_id++, r); }
    const OrderBook& book() const { return engine.book(); }
};

}  // namespace

// ===========================================================================
// Price-time priority & matching
// ===========================================================================

TEST(buy_matches_lowest_ask_first) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));  // best (lower price)
    h.submit(limit(2, 2, Side::SELL, 101, 5));
    const SubmitResult r = h.submit(limit(3, 3, Side::BUY, 101, 5));
    CHECK_EQ(r.status, OrderStatus::FILLED);
    CHECK_EQ(r.trades.size(), static_cast<size_t>(1));
    CHECK_EQ(r.trades[0].price, static_cast<uint64_t>(100));  // resting price
    CHECK_EQ(r.trades[0].quantity, static_cast<uint64_t>(5));
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(5));
}

TEST(sell_matches_highest_bid_first) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));  // best (higher price)
    h.submit(limit(2, 2, Side::BUY, 99, 5));
    const SubmitResult r = h.submit(limit(3, 3, Side::SELL, 99, 5));
    CHECK_EQ(r.status, OrderStatus::FILLED);
    CHECK_EQ(r.trades.size(), static_cast<size_t>(1));
    CHECK_EQ(r.trades[0].price, static_cast<uint64_t>(100));
}

TEST(equal_price_is_fifo_by_time) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));  // id 1, earlier
    h.submit(limit(2, 2, Side::SELL, 100, 5));  // id 2, later
    const SubmitResult r = h.submit(limit(3, 3, Side::BUY, 100, 5));
    CHECK_EQ(r.trades.size(), static_cast<size_t>(1));
    CHECK_EQ(r.trades[0].maker_order_id, static_cast<uint64_t>(1));  // FIFO
}

TEST(trade_executes_at_resting_price) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(limit(2, 2, Side::BUY, 105, 5));  // aggressive
    CHECK_EQ(r.trades.size(), static_cast<size_t>(1));
    CHECK_EQ(r.trades[0].price, static_cast<uint64_t>(100));  // maker's price, not 105
}

TEST(partial_fill_rests_remainder) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(limit(2, 2, Side::BUY, 100, 8));
    CHECK_EQ(r.status, OrderStatus::RESTING);
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(5));
    const MarketData md = h.book().top_of_book();
    CHECK(md.has_bid);
    CHECK_EQ(md.bid_price, static_cast<uint64_t>(100));
    CHECK_EQ(md.bid_quantity, static_cast<uint64_t>(3));  // 8 - 5 rests
}

TEST(sweeps_multiple_levels) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 3));
    h.submit(limit(2, 2, Side::SELL, 101, 3));
    const SubmitResult r = h.submit(limit(3, 3, Side::BUY, 101, 5));
    CHECK_EQ(r.status, OrderStatus::FILLED);
    CHECK_EQ(r.trades.size(), static_cast<size_t>(2));
    CHECK_EQ(r.trades[0].price, static_cast<uint64_t>(100));  // best level first
    CHECK_EQ(r.trades[1].price, static_cast<uint64_t>(101));
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(5));
}

TEST(no_cross_rests_without_trade) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(limit(2, 2, Side::BUY, 99, 5));  // 99 < 100
    CHECK_EQ(r.status, OrderStatus::RESTING);
    CHECK_EQ(r.trades.size(), static_cast<size_t>(0));
    const MarketData md = h.book().top_of_book();
    CHECK(md.has_bid);
    CHECK(md.has_ask);
}

// ===========================================================================
// Order types: MARKET / IOC
// ===========================================================================

TEST(market_fills_then_cancels_remainder) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(market(2, 2, Side::BUY, 8));
    CHECK_EQ(r.status, OrderStatus::CANCELLED);  // 3 leftover not rested
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(5));
    CHECK(!h.book().top_of_book().has_bid);  // nothing rests
}

TEST(market_on_empty_book_cancels) {
    Harness h;
    const SubmitResult r = h.submit(market(1, 1, Side::BUY, 5));
    CHECK_EQ(r.status, OrderStatus::CANCELLED);
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(0));
    CHECK(h.book().empty());
}

TEST(ioc_fills_then_cancels_remainder) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(ioc(2, 2, Side::BUY, 100, 8));
    CHECK_EQ(r.status, OrderStatus::CANCELLED);
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(5));
    CHECK(!h.book().top_of_book().has_bid);
}

TEST(ioc_no_match_cancels) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    const SubmitResult r = h.submit(ioc(2, 2, Side::BUY, 90, 5));  // 90 < 100
    CHECK_EQ(r.status, OrderStatus::CANCELLED);
    CHECK_EQ(r.filled_quantity, static_cast<uint64_t>(0));
    CHECK(!h.book().top_of_book().has_bid);
}

// ===========================================================================
// Cancel
// ===========================================================================

TEST(cancel_removes_resting_order) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));
    const SubmitResult r = h.submit(cancel(1, 1));
    CHECK_EQ(r.status, OrderStatus::CANCELLED);
    CHECK(!h.book().top_of_book().has_bid);
}

TEST(cancel_unknown_order_rejects_gracefully) {
    Harness h;
    const SubmitResult r = h.submit(cancel(9, 999));
    CHECK_EQ(r.rejectReason, RejectReason::UnknownOrderId);
}

TEST(cancel_already_filled_order_rejects) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    h.submit(limit(2, 2, Side::BUY, 100, 5));  // fully fills & removes maker
    const SubmitResult r = h.submit(cancel(1, 1));
    CHECK_EQ(r.rejectReason, RejectReason::UnknownOrderId);
}

// ===========================================================================
// Modify
// ===========================================================================

TEST(modify_quantity_down_retains_priority) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));  // id 1, earlier
    h.submit(limit(2, 2, Side::BUY, 100, 5));  // id 2, later
    h.submit(modify(1, 1, 100, 3));            // decrease, same price -> in place
    const SubmitResult r = h.submit(limit(3, 3, Side::SELL, 100, 3));
    CHECK_EQ(r.trades[0].maker_order_id, static_cast<uint64_t>(1));  // still first
    CHECK_EQ(r.trades[0].quantity, static_cast<uint64_t>(3));
}

TEST(modify_quantity_up_resets_priority) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));  // id 1
    h.submit(limit(2, 2, Side::BUY, 100, 5));  // id 2
    h.submit(modify(1, 1, 100, 8));            // increase -> loses time priority
    const SubmitResult r = h.submit(limit(3, 3, Side::SELL, 100, 5));
    CHECK_EQ(r.trades[0].maker_order_id, static_cast<uint64_t>(2));  // id 2 now first
}

TEST(modify_price_resets_priority) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));  // id 1, worse price
    h.submit(limit(2, 2, Side::BUY, 101, 5));  // id 2, best bid
    h.submit(modify(1, 1, 101, 5));  // reprice 100 -> 101: re-enters BEHIND id 2
    const SubmitResult r = h.submit(limit(3, 3, Side::SELL, 101, 5));
    CHECK_EQ(r.trades[0].maker_order_id, static_cast<uint64_t>(2));  // id 2 first
}

TEST(modify_price_can_cross_and_fill) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));  // resting ask
    h.submit(limit(2, 2, Side::BUY, 90, 5));    // rests, no cross
    const SubmitResult r = h.submit(modify(2, 2, 100, 5));  // reprice up -> crosses
    CHECK_EQ(r.status, OrderStatus::FILLED);
    CHECK_EQ(r.trades.size(), static_cast<size_t>(1));
    CHECK_EQ(r.trades[0].price, static_cast<uint64_t>(100));
}

TEST(modify_to_zero_quantity_is_cancel) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));
    const SubmitResult r = h.submit(modify(1, 1, 100, 0));
    CHECK_EQ(r.status, OrderStatus::CANCELLED);
    CHECK(!h.book().top_of_book().has_bid);
}

TEST(modify_unknown_order_rejects) {
    Harness h;
    const SubmitResult r = h.submit(modify(9, 999, 100, 5));
    CHECK_EQ(r.rejectReason, RejectReason::UnknownOrderId);
}

// ===========================================================================
// Self-trade prevention (cancel incoming)
// ===========================================================================

TEST(self_trade_is_prevented) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));       // client 1 resting ask
    const SubmitResult r = h.submit(limit(1, 2, Side::BUY, 100, 5));  // same client
    CHECK_EQ(r.status, OrderStatus::CANCELLED);      // incoming cancelled
    CHECK_EQ(r.trades.size(), static_cast<size_t>(0));
    const MarketData md = h.book().top_of_book();
    CHECK(md.has_ask);  // resting ask untouched
    CHECK_EQ(md.ask_quantity, static_cast<uint64_t>(5));
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST(duplicate_client_order_key_rejected) {
    Harness h;
    h.submit(limit(1, 1, Side::BUY, 100, 5));  // rests
    const SubmitResult r = h.submit(limit(1, 1, Side::BUY, 101, 5));  // same key
    CHECK_EQ(r.rejectReason, RejectReason::DuplicateOrderId);
}

// ===========================================================================
// OrderBook structure
// ===========================================================================

TEST(empty_book_has_no_top_of_book) {
    Harness h;
    const MarketData md = h.book().top_of_book();
    CHECK(!md.has_bid);
    CHECK(!md.has_ask);
}

TEST(top_of_book_aggregates_level_quantity) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 3));
    h.submit(limit(2, 2, Side::SELL, 100, 4));  // same price level
    const MarketData md = h.book().top_of_book();
    CHECK(md.has_ask);
    CHECK_EQ(md.ask_price, static_cast<uint64_t>(100));
    CHECK_EQ(md.ask_quantity, static_cast<uint64_t>(7));  // 3 + 4
}

TEST(remove_drops_empty_price_level) {
    Harness h;
    h.submit(limit(1, 1, Side::SELL, 100, 5));
    h.submit(cancel(1, 1));
    CHECK(h.book().asks().empty());  // level erased, not left empty
}

// ===========================================================================
// Exchange-level synchronous validation
// ===========================================================================

TEST(exchange_rejects_invalid_price) {
    Exchange ex({"AAA"}, "test_out", false);
    const RejectReason rr =
        ex.submit("AAA", limit(1, 1, Side::BUY, 0, 5));  // price 0
    CHECK_EQ(rr, RejectReason::InvalidPrice);
}

TEST(exchange_rejects_invalid_quantity) {
    Exchange ex({"AAA"}, "test_out", false);
    const RejectReason rr =
        ex.submit("AAA", limit(1, 1, Side::BUY, 100, 0));  // qty 0
    CHECK_EQ(rr, RejectReason::InvalidQuantity);
}

TEST(exchange_rejects_unknown_symbol) {
    Exchange ex({"AAA"}, "test_out", false);
    const RejectReason rr = ex.submit("ZZZ", limit(1, 1, Side::BUY, 100, 5));
    CHECK_EQ(rr, RejectReason::UnknownSymbol);
}
