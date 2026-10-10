#include "engine/Clock.hpp"
#include "engine/Exchange.hpp"

#include <filesystem>
#include <ranges>

namespace Engine {
    Exchange::Exchange(const std::vector<std::string>& symbols, std::string outdir, bool record_latency) {
        filesystem::create_directories(outdir);
        for (const auto& symbol : symbols) {
            symbol_workers_.try_emplace(symbol, symbol, outdir, record_latency);
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

    void Exchange::start() {
        // Start each worker consumer
        for (auto &val: symbol_workers_ | views::values) {
            val.start();
        }
    }

    void Exchange::stop() {
        for (auto &val: symbol_workers_ | views::values) {
            val.stop();
        }
    }

    RejectReason Exchange::submit(const std::string& symbol, const OrderRequest& request) {
        const RejectReason rr = validate(request, symbol);
        if (rr != RejectReason::None) {
            return rr;
        }
        symbol_workers_.at(symbol).post({ .request = request, .enqueue_ns = now_ns() });
        return rr;
    }

    std::vector<std::vector<std::uint64_t> > Exchange::collect_latencies() const {
        std::vector<std::vector<std::uint64_t>> latencies;
        latencies.reserve(symbol_workers_.size());
        for (const auto &val: symbol_workers_ | views::values) {
            latencies.push_back(val.get_latencies());
        }

        return latencies;
    }
}
