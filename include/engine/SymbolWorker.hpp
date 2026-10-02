#pragma once
#include <thread>

#include "BoundedBlockingQueue.hpp"
#include "MatchEngine.hpp"
#include "Sink.hpp"

namespace Engine {
    using CommandQueue = BoundedBlockingQueue<Command>;

    class SymbolWorker {
    public:
        explicit SymbolWorker(std::string symbol, std::ostream* out, bool record_latency, size_t capacity = 4096, size_t max_batch = 256);
        ~SymbolWorker();

        void start();
        void stop();

        // Exchange will post the command
        void post(const Command &command);

        // Used for benchmark program
        [[nodiscard]] const std::vector<uint64_t>& get_latencies() const;

    private:
        void run(); // Consumer loop; Single consumer thread for executing the commands from command queue

        MatchingEngine engine_;
        size_t max_batch_;
        CommandQueue queue_;
        bool record_latency_;  // To record latencies only for benchmark workloads
        std::vector<uint64_t> latencies_;  // Store latencies for benchmark program
        std::unique_ptr<Sink> sink_;
        bool stopped_{false};
        bool started_{false};
        bool post_close{false};
        std::string symbol_;
        std::thread consumer_;
    };
}
