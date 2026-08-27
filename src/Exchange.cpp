#include "engine/Clock.hpp"
#include "engine/Exchange.hpp"
#include "engine/OrderPool.hpp"

#include <filesystem>
#include <fstream>
#include <ranges>

namespace Engine {
    Exchange::~Exchange() = default;

    Exchange::Exchange(const std::vector<std::string>& symbols, std::string outdir, bool record_latency) {
        filesystem::create_directories(outdir);
        for (const auto& symbol : symbols) {
            if (record_latency) {
                // Workaround for benchmark
                std::ofstream* out = nullptr;
                symbol_workers_.try_emplace(symbol, std::make_unique<SymbolWorker>(symbol, out, record_latency));
                continue;
            }

            auto filename = outdir + "/" + symbol + ".log";
            std::ofstream ofs(filename, std::ios::out | std::ios::trunc);
            ofs_.try_emplace(symbol, std::move(ofs));
            std::ofstream* out = &ofs_.at(symbol);
            symbol_workers_.try_emplace(symbol, std::make_unique<SymbolWorker>(symbol, out, record_latency));
        }
    }

    RejectReason Exchange::validate(const OrderRequest &request, const std::string& symbol) {
        if (const auto it = symbol_workers_.find(symbol); it == symbol_workers_.end()) {
            return RejectReason::UnknownSymbol;
        }

        switch (request.type) {
            case OrderType::Limit:
            case OrderType::IOC:
                if (request.quantity <= 0) return RejectReason::InvalidQuantity;
                if (request.price <= 0) return RejectReason::InvalidPrice;
                return RejectReason::None;
            case OrderType::Modify:
                if (request.price <= 0) return RejectReason::InvalidPrice;
                return RejectReason::None;
            case OrderType::Market:
                if (request.quantity <= 0) return RejectReason::InvalidQuantity;
                return RejectReason::None;
            case OrderType::Cancel:
                return RejectReason::None;
        }

        return RejectReason::InvalidOrderType;
    }

    void Exchange::start() const {
        // Start each worker consumer
        for (const auto& entry : symbol_workers_) {
            entry.second->start();
        }
    }

    void Exchange::stop() {
        for (const auto& entry : symbol_workers_) {
            entry.second->stop();
        }

        for (auto& [symbol, ofs] : ofs_) {
            if (ofs.is_open()) {
                ofs.close();
            }
        }
    }

    RejectReason Exchange::submit(const std::string& symbol, const OrderRequest& request) {
        const RejectReason rr = validate(request, symbol);
        if (rr != RejectReason::None) {
            return rr;
        }

        const auto order_id = ids_.next(); // Generate globally unique order ID
        symbol_workers_.at(symbol)->post({ .order_id = order_id, .request = request, .enqueue_ns = now_ns() });
        return rr;
    }

    std::vector<std::vector<std::uint64_t> > Exchange::collect_latencies() const {
        std::vector<std::vector<std::uint64_t>> latencies;
        latencies.reserve(symbol_workers_.size());
        for (const auto &val: symbol_workers_ | views::values) {
            latencies.push_back(val->get_latencies());
        }

        return latencies;
    }
}
