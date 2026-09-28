#pragma once
#include <string>
#include <thread>

#include "MatchEngineTypes.hpp"
#include "SPSCQueue.hpp"

namespace Engine {
    using ResultQueue = SPSCQueue<MatchingResult>;

    class Sink {
    public:
        explicit Sink(std::string  symbol, std::ostream& out, size_t capacity = 1024);

        void start();
        void stop();

        // The SymbolWorker will publish the MatchingResult to the queue for logging
        void publish(MatchingResult result);

    private:
        void run();  // Runner thread which drains the ResultQueue to log the results
        void ProcessMR(const MatchingResult& result) const;

        ResultQueue queue_;
        std::ostream& out_;
        std::string symbol_;
        std::thread consumer_;
        bool is_started_{false};
        bool is_stopped_{false};
    };
}
