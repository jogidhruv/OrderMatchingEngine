// ---------------------------------------------------------------------------
// End-to-end integration tests.
// ---------------------------------------------------------------------------
// These drive the full asynchronous pipeline that the domain tests skip:
// Exchange::submit (success path) -> OrderIdGenerator -> SymbolWorker matching
// thread -> BoundedBlockingQueue push/drain/close -> Sink writer thread -> log
// file. We submit valid orders, stop() the exchange (which joins both threads
// and flushes the per-symbol log), then read the log back and assert on its
// contents. Because commands are posted sequentially into a FIFO queue and each
// symbol has a single matching thread, the output is fully deterministic.

#include "test_framework.hpp"

#include "engine/Exchange.hpp"
#include "engine/OrderTypes.hpp"
#include "engine/Types.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Engine;

namespace {

OrderRequest limit(uint64_t cid, uint64_t coid, Side s, uint64_t px, uint64_t qty) {
    return OrderRequest{cid, coid, px, qty, OrderType::Limit, s};
}

// Read every line of a log file (newline-stripped) into a vector.
std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

// True if `needle` appears exactly as one of the lines.
bool has_line(const std::vector<std::string>& lines, const std::string& needle) {
    for (const std::string& l : lines) {
        if (l == needle) {
            return true;
        }
    }
    return false;
}

// True if the given lines appear in the file in the specified relative order
// (other lines may be interleaved). Empty -> true.
bool has_lines_in_order(const std::vector<std::string>& lines,
                        const std::vector<std::string>& expected_order) {
    std::size_t next = 0;
    for (const std::string& l : lines) {
        if (next < expected_order.size() && l == expected_order[next]) {
            ++next;
        }
    }
    return next == expected_order.size();
}

// True if any line begins with `prefix`.
bool any_line_starts_with(const std::vector<std::string>& lines,
                          const std::string& prefix) {
    for (const std::string& l : lines) {
        if (l.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ===========================================================================
// Full pipeline: submit -> match -> log
// ===========================================================================

TEST(exchange_end_to_end_trade_and_market_data) {
    const std::string outdir = "test_out_e2e";
    {
        Exchange ex({"AAA"}, outdir, false);
        ex.start();
        // Order id 1: resting sell at 100x5.
        CHECK_EQ(ex.submit("AAA", limit(1, 1, Side::SELL, 100, 5)),
                 RejectReason::None);
        // Order id 2: buy at 100x5 fully crosses -> trade at maker's price.
        CHECK_EQ(ex.submit("AAA", limit(2, 2, Side::BUY, 100, 5)),
                 RejectReason::None);
        ex.stop();  // joins matching + sink threads, flushes + closes AAA.log
    }

    const std::vector<std::string> lines = read_lines(outdir + "/AAA.log");

    // The resting sell publishes a top-of-book snapshot (ask 100x5, no bid).
    CHECK(has_line(lines, "MarketData AAA bid - - ask 100 5"));
    // The crossing buy prints one execution: maker id 1, taker id 2, resting px.
    CHECK(has_line(lines, "TRADE AAA 1 2 100 5"));
    // After the full fill the book is empty on both sides.
    CHECK(has_line(lines, "MarketData AAA bid - - ask - -"));

    // And they must occur in that causal order.
    CHECK(has_lines_in_order(lines, {"MarketData AAA bid - - ask 100 5",
                                     "TRADE AAA 1 2 100 5",
                                     "MarketData AAA bid - - ask - -"}));

    //std::filesystem::remove_all(outdir);
}

// ===========================================================================
// Multi-symbol: independent books, no cross-symbol matching
// ===========================================================================

TEST(exchange_multi_symbol_books_are_independent) {
    const std::string outdir = "test_out_multi";
    {
        Exchange ex({"AAA", "BBB"}, outdir, false);
        ex.start();
        // A resting sell on AAA and a marketable-looking buy on BBB at the same
        // price. If the books were shared these would trade; they must not.
        CHECK_EQ(ex.submit("AAA", limit(1, 1, Side::SELL, 100, 5)),
                 RejectReason::None);
        CHECK_EQ(ex.submit("BBB", limit(2, 2, Side::BUY, 100, 5)),
                 RejectReason::None);
        ex.stop();
    }

    const std::vector<std::string> aaa = read_lines(outdir + "/AAA.log");
    const std::vector<std::string> bbb = read_lines(outdir + "/BBB.log");

    // Each order simply rests on its own book; neither symbol sees a trade.
    CHECK(has_line(aaa, "MarketData AAA bid - - ask 100 5"));
    CHECK(has_line(bbb, "MarketData BBB bid 100 5 ask - -"));
    CHECK(!any_line_starts_with(aaa, "TRADE"));
    CHECK(!any_line_starts_with(bbb, "TRADE"));

    //std::filesystem::remove_all(outdir);
}
