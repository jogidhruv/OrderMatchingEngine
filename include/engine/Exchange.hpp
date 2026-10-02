#pragma once
#include <vector>

#include "MatchEngine.hpp"
#include "SymbolWorker.hpp"

namespace Engine {
    class Exchange {
    public:
        explicit Exchange(const std::vector<std::string>& symbols, std::string outdir, bool record_latency = false);

        ~Exchange();

        // For clients to submit their requests to the exchange
        [[nodiscard]] RejectReason submit(const std::string& symbol, const OrderRequest& request);

        // Start all the symbol workers
        void start() const;

        // Stop the exchange
        void stop();

        // Collect latencies from every symbol for benchmark
        [[nodiscard]] std::vector<std::vector<std::uint64_t>> collect_latencies() const;

    private:
        // For sync validations; immediate reject
        [[nodiscard]] RejectReason validate(const OrderRequest& request, const std::string& symbol);

        // File streams for each symbol worker to log emit logs into different files
        std::unordered_map<std::string, std::ofstream> ofs_;
        std::unordered_map<std::string, std::unique_ptr<SymbolWorker>> symbol_workers_;
    };
}
