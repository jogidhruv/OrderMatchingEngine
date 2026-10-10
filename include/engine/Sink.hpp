#pragma once
#include <fstream>
#include <string>
#include <thread>

#include "MatchEngineTypes.hpp"
#include "SPSCQueue.hpp"

namespace Engine {
    using ResultQueue = SPSCQueue<MatchingResult>;

    class Sink {
    public:
        explicit Sink(std::string  symbol, const std::string& outdir, size_t capacity = 1024);
        ~Sink() {
            if (out_.is_open()) {
                out_.close();
            }
        }

        void start();
        void stop();

        // The SymbolWorker will publish the MatchingResult to the queue for logging
        void publish(MatchingResult result);

    private:
        void run();  // Runner thread which drains the ResultQueue to log the results
        void ProcessMR(const MatchingResult& result);

        ResultQueue queue_;
        std::ofstream out_;
        std::string symbol_;
        std::thread consumer_;
        bool is_started_{false};
        bool is_stopped_{false};
    };
}
