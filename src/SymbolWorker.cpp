#include "engine/Clock.hpp"
#include "engine/SymbolWorker.hpp"

namespace Engine {
    SymbolWorker::SymbolWorker(std::string symbol, std::ostream* out, bool record_latency, size_t capacity, size_t max_batch)
        : max_batch_(max_batch),
          queue_(capacity),
          record_latency_(record_latency),
          symbol_(std::move(symbol)) {
        //latencies_.reserve(2000000);
        if (out != nullptr) {
            sink_ = std::make_unique<Sink>(symbol_, *out, capacity * 5); // Bigger capacity for Sink Queue
        }
    }

    SymbolWorker::~SymbolWorker() {
        stop();
    }

    void SymbolWorker::start() {
        if (started_) {
            return;
        }

        started_ = true;
        if (sink_) {
            sink_->start();
        }
        consumer_ = std::thread([this] { run(); });
    }

    void SymbolWorker::stop() {
        if (stopped_) {
            return;
        }

        stopped_ = true;
        queue_.close();
        if (consumer_.joinable()) {
            consumer_.join();
        }

        if (sink_) {
            sink_->stop();
        }
    }

    void SymbolWorker::post(const Command &command) {
        queue_.push(command);
        //while (!queue_.push_nolock(command));
    }

    void SymbolWorker::run() {
        vector<Command> batch(max_batch_); // One time allocation; reused afterwards

        while (true) {
            const size_t n = queue_.drain(batch, max_batch_);
            if (n == 0) {
                if (queue_.closed()) {
                    if (!post_close) {
                        post_close = true;
                        continue;   // Run OLT
                    }
                    break;
                }
                // Let producers push for a while
                std::this_thread::yield();
                continue;
            }
            /*
            if (n == 0) {
                break; // Queue is closed
            }*/

            for (size_t i = 0; i < n; ++i) {
                auto submitResult = engine_.submit(batch[i].request);

                // Record the latency. This will indicate the request latency from when it got into Exchange
                // to it got executed by the single consumer thread.
                if (record_latency_) {
                    latencies_.push_back(now_ns() - batch[i].enqueue_ns);
                }

                if (sink_) {
                    MatchingResult result;
                    result.result = std::move(submitResult);
                    if (result.result.rejectReason == RejectReason::None) {
                        result.data = engine_.book().top_of_book();
                    }

                    sink_->publish(std::move(result));
                }
            }
        }
    }

    const std::vector<uint64_t>& SymbolWorker::get_latencies() const {
        return latencies_;
    }
}
