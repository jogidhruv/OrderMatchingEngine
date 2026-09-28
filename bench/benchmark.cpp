// ---------------------------------------------------------------------------
// Multi-producer throughput & latency benchmark for the matching engine.
// ---------------------------------------------------------------------------
// Spawns N producer threads that concurrently submit a pre-generated, fixed-seed
// workload to a shared Exchange. Each symbol is matched on its own single thread,
// so matching stays deterministic while producers contend on the submit path.
//
// End-to-end latency is measured inside the engine: Exchange::submit() stamps
// each Command with a steady-clock timestamp at enqueue, and the matching thread
// records (dequeue_time - enqueue_time) once it has processed that command. The
// Exchange is constructed with record_latency = true and collect_latencies()
// returns one latency vector per symbol after stop().
//
// The sink is not exercised (no per-symbol logging): we measure the pure
// enqueue -> match path. Build in Release; Debug latency numbers are meaningless.
//
// Usage:
//   engine_bench [num_producers] [orders_per_producer] [num_symbols]
// Defaults: producers = min(hardware_concurrency, 8), orders = 200000, symbols = 4.

#include "engine/Exchange.hpp"
#include "engine/OrderTypes.hpp"
#include "engine/Types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <latch>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace Engine;
using Clock = std::chrono::steady_clock;

struct Config {
    std::size_t producers;
    std::size_t orders_per_producer;
    std::size_t symbols;
};

[[nodiscard]] Config parse_args(int argc, char** argv) {
    const unsigned hw = std::thread::hardware_concurrency();
    Config cfg;
    cfg.producers = std::min<std::size_t>(hw == 0 ? 4 : hw, 8);
    cfg.orders_per_producer = 200'0000;
    cfg.symbols = 8;

    auto parse = [](const char* s, std::size_t fallback) -> std::size_t {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s, &end, 10);
        return (end == s || v == 0) ? fallback : static_cast<std::size_t>(v);
    };
    if (argc > 1) cfg.producers = parse(argv[1], cfg.producers);
    if (argc > 2) cfg.orders_per_producer = parse(argv[2], cfg.orders_per_producer);
    if (argc > 3) cfg.symbols = parse(argv[3], cfg.symbols);
    if (cfg.producers == 0) cfg.producers = 1;
    if (cfg.symbols == 0) cfg.symbols = 1;
    return cfg;
}

[[nodiscard]] std::vector<std::string> make_symbols(std::size_t n) {
    static const char* kNames[] = {"AAA", "BBB", "CCC", "DDD", "EEE",
                                   "FFF", "GGG", "HHH"};
    std::vector<std::string> out;
    const std::size_t limit = std::min(n, std::size(kNames));
    for (std::size_t i = 0; i < limit; ++i) {
        out.emplace_back(kNames[i]);
    }
    // If more symbols were requested than names, synthesise the remainder.
    for (std::size_t i = limit; i < n; ++i) {
        out.emplace_back("S" + std::to_string(i));
    }
    return out;
}

// One pre-generated command destined for a specific symbol.
struct GenCommand {
    std::size_t  symbol_index;
    OrderRequest request;
};

// Builds a reproducible workload for one producer. Each producer owns a disjoint
// client-id range (producer_id * kClientStride) so no self-trade prevention fires
// across producers, and its own client-order-id sequence. Orders are mostly
// crossing LIMITs in a tight band so trades actually execute, with a sprinkling
// of MARKET/IOC and cancels/modifies of the producer's own earlier orders.
[[nodiscard]] std::vector<GenCommand> generate(std::size_t producer_id,
                                               std::size_t count,
                                               std::size_t num_symbols) {
    constexpr std::uint64_t kBasePrice = 10'000;
    constexpr std::uint64_t kBand = 20;  // price band half-width

    std::mt19937_64 rng(0x9E3779B97F4A7C15ull ^ (producer_id + 1));
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<std::uint64_t> price_off(0, 2 * kBand);
    std::uniform_int_distribution<std::uint64_t> qty(1, 50);
    std::uniform_int_distribution<std::size_t> sym(0, num_symbols - 1);

    const std::uint64_t client_id = producer_id + 1;

    std::vector<GenCommand> cmds;
    cmds.reserve(count);

    std::uint64_t next_coid = 1;
    // Track this producer's live client-order-ids per symbol for cancel/modify.
    std::vector<std::vector<std::uint64_t>> live(num_symbols);

    for (std::size_t i = 0; i < count; ++i) {
        GenCommand gc;
        gc.symbol_index = sym(rng);
        OrderRequest& r = gc.request;
        r.clientId = client_id;
        r.side = (pick(rng) < 50) ? Side::BUY : Side::SELL;

        const int roll = pick(rng);
        auto& symbol_live = live[gc.symbol_index];

        if (roll < 8 && !symbol_live.empty()) {
            // CANCEL an earlier order from this producer on this symbol.
            const std::size_t idx =
                std::uniform_int_distribution<std::size_t>(0, symbol_live.size() - 1)(rng);
            r.clientOrderId = symbol_live[idx];
            symbol_live[idx] = symbol_live.back();
            symbol_live.pop_back();
            r.type = OrderType::Cancel;
            r.price = 0;
            r.quantity = 0;
        } else if (roll < 14 && !symbol_live.empty()) {
            // MODIFY an earlier order (new price in band, new quantity).
            const std::size_t idx =
                std::uniform_int_distribution<std::size_t>(0, symbol_live.size() - 1)(rng);
            r.clientOrderId = symbol_live[idx];
            r.type = OrderType::Modify;
            r.price = kBasePrice - kBand + price_off(rng);
            r.quantity = qty(rng);
        } else if (roll < 22) {
            // MARKET order: consumes liquidity, never rests.
            r.clientOrderId = next_coid++;
            r.type = OrderType::Market;
            r.price = 0;
            r.quantity = qty(rng);
        } else if (roll < 30) {
            // IOC order: executes then cancels any remainder.
            r.clientOrderId = next_coid++;
            r.type = OrderType::IOC;
            r.price = kBasePrice - kBand + price_off(rng);
            r.quantity = qty(rng);
        } else {
            // LIMIT order: the bulk of the flow; rests if it doesn't fully fill.
            r.clientOrderId = next_coid++;
            r.type = OrderType::Limit;
            r.price = kBasePrice - kBand + price_off(rng);
            r.quantity = qty(rng);
            symbol_live.push_back(r.clientOrderId);
        }
        cmds.push_back(gc);
    }
    return cmds;
}

struct Percentiles {
    std::uint64_t count;
    double        mean;
    std::uint64_t p50, p75, p90, p99, max;
};

// Computes percentiles from a merged, then sorted, latency sample set.
[[nodiscard]] Percentiles summarise(std::vector<std::uint64_t>& all) {
    Percentiles p{};
    p.count = all.size();
    if (all.empty()) {
        return p;
    }
    std::sort(all.begin(), all.end());
    const long double sum =
        std::accumulate(all.begin(), all.end(), static_cast<long double>(0));
    p.mean = static_cast<double>(sum / static_cast<long double>(all.size()));

    auto nth = [&](double q) -> std::uint64_t {
        // Nearest-rank percentile.
        std::size_t idx = static_cast<std::size_t>(q * static_cast<double>(all.size()));
        if (idx >= all.size()) idx = all.size() - 1;
        return all[idx];
    };
    p.p50 = nth(0.50);
    p.p75 = nth(0.75);
    p.p90 = nth(0.90);
    p.p99 = nth(0.99);
    p.max = all.back();
    return p;
}

// Runs one full pass: builds an Exchange, releases all producers behind a gate,
// times the submit-to-drain window, and returns (elapsed, merged latencies).
struct RunResult {
    double                     seconds;
    std::size_t                total_submitted;
    std::vector<std::uint64_t> latencies;
};

[[nodiscard]] RunResult run_pass(const Config& cfg,
                                 const std::vector<std::string>& symbols,
                                 const std::vector<std::vector<GenCommand>>& work) {
    Exchange exchange(symbols, "bench_out", /*record_latency=*/true);
    exchange.start();

    std::latch go{1};
    std::atomic<std::size_t> submitted{0};
    std::vector<std::thread> producers;
    producers.reserve(cfg.producers);

    for (std::size_t p = 0; p < cfg.producers; ++p) {
        producers.emplace_back([&, p] {
            const std::vector<GenCommand>& mine = work[p];
            go.wait();
            std::size_t local = 0;
            for (const GenCommand& gc : mine) {
                auto _ = exchange.submit(symbols[gc.symbol_index], gc.request);
                ++local;
            }
            submitted.fetch_add(local, std::memory_order_relaxed);
        });
    }

    const auto t0 = Clock::now();
    go.count_down();
    for (auto& t : producers) {
        t.join();
    }
    // stop() drains every symbol queue and joins the matching threads, so all
    // work is fully processed (and all latencies recorded) once it returns.
    exchange.stop();
    const auto t1 = Clock::now();

    RunResult rr;
    rr.seconds = std::chrono::duration<double>(t1 - t0).count();
    rr.total_submitted = submitted.load(std::memory_order_relaxed);

    const std::vector<std::vector<std::uint64_t>> per_symbol =
        exchange.collect_latencies();
    std::size_t total = 0;
    for (const auto& v : per_symbol) total += v.size();
    rr.latencies.reserve(total);
    for (const auto& v : per_symbol) {
        rr.latencies.insert(rr.latencies.end(), v.begin(), v.end());
    }
    return rr;
}

void print_report(const Config& cfg, const RunResult& run) {
    std::vector<std::uint64_t> lat = run.latencies;
    const Percentiles p = summarise(lat);
    const double throughput =
        run.seconds > 0 ? static_cast<double>(run.total_submitted) / run.seconds : 0.0;

    auto us = [](std::uint64_t ns) { return static_cast<double>(ns) / 1000.0; };

    std::cout << "==============================================\n"
              << "  Matching Engine Benchmark\n"
              << "==============================================\n"
              << "Producers          : " << cfg.producers << '\n'
              << "Orders / producer  : " << cfg.orders_per_producer << '\n'
              << "Symbols            : " << cfg.symbols << '\n'
              << "Total submitted    : " << run.total_submitted << '\n'
              << "Latency samples    : " << p.count << '\n'
              << "Elapsed            : " << run.seconds << " s\n"
              << "----------------------------------------------\n"
              << "Throughput         : " << static_cast<std::uint64_t>(throughput)
              << " orders/sec\n"
              << "----------------------------------------------\n"
              << "Latency (end-to-end, enqueue -> processed)\n"
              << "  Mean             : " << us(static_cast<std::uint64_t>(p.mean)) << " us\n"
              << "  P50              : " << us(p.p50) << " us\n"
              << "  P75              : " << us(p.p75) << " us\n"
              << "  P90              : " << us(p.p90) << " us\n"
              << "  P99              : " << us(p.p99) << " us\n"
              << "  Max              : " << us(p.max) << " us\n"
              << "==============================================\n";
}

}  // namespace

int main(int argc, char** argv) {
    const Config cfg = parse_args(argc, argv);
    const std::vector<std::string> symbols = make_symbols(cfg.symbols);

    // Pre-generate every producer's workload up front so RNG cost is outside the
    // timed region. Each producer's stream is reproducible from a fixed seed.
    std::vector<std::vector<GenCommand>> work(cfg.producers);
    for (std::size_t p = 0; p < cfg.producers; ++p) {
        work[p] = generate(p, cfg.orders_per_producer, symbols.size());
    }

    // Warmup pass (discarded): pages in memory and warms caches / branch history.
    {
        Config warm = cfg;
        warm.orders_per_producer =
            std::min<std::size_t>(cfg.orders_per_producer, 20'000);
        std::vector<std::vector<GenCommand>> warm_work(cfg.producers);
        for (std::size_t p = 0; p < cfg.producers; ++p) {
            warm_work[p] = generate(p, warm.orders_per_producer, symbols.size());
        }
        (void)run_pass(warm, symbols, warm_work);
    }

    const RunResult run = run_pass(cfg, symbols, work);
    print_report(cfg, run);
    return 0;
}
