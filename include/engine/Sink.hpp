#pragma once
#include <string>
#include <thread>

#include "ResultQueue.hpp"

namespace Engine {
    class Sink {
    public:
        explicit Sink(const std::string& symbol, std::ostream& out, const size_t batch_size = 256, const size_t capacity = 1024);

        void start();
        void stop();

        // The SymbolWorker will publish the MatchingResult to the queue for logging
        void publish(MatchingResult result);

    private:
        void run();  // Runner thread which drains the ResultQueue to log the results

        ResultQueue queue_;
        std::ostream& out_;
        std::string symbol_;
        size_t batch_size_;
        std::thread consumer_;
        bool is_started_{false};
        bool is_stopped_{false};
    };
}
