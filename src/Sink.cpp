#include "engine/Sink.hpp"

namespace Engine {

    Sink::Sink(const std::string& symbol, std::ostream& out, const size_t batch_size, const size_t capacity)
        : queue_(capacity),
          out_(out),
          symbol_(symbol),
          batch_size_(batch_size) {}

    void Sink::publish(MatchingResult result) {
        queue_.push(std::move(result));
    }

    void Sink::start() {
        if (is_started_) {
            return;
        }

        consumer_ = std::thread([this] { run(); });
        is_started_ = true;
    }

    void Sink::stop() {
        if (is_stopped_ || !is_started_) {
            return;
        }

        queue_.close();
        if (consumer_.joinable()) {
            consumer_.join();
        }
        is_started_ = false;
        is_stopped_ = true;
    }

    void Sink::run() {
        std::vector<MatchingResult> batch(batch_size_); // Runtime heap allocation once
        while (true) {
            const size_t n = queue_.drain(batch, batch_size_);
            if (n == 0) {
                break; // Stopped
            }

            for (std::size_t i = 0; i < n; ++i) {
                const MatchingResult& r = batch[i];

                if (r.result.rejectReason != RejectReason::None) {
                    out_ << "REJECT " << to_string(r.result.rejectReason) << ' ' << symbol_ << ' ' << r.result.clientId
                            << ' ' << r.result.clientOrderId << '\n';
                    continue;
                }

                // One execution-log line per fill.
                for (const Trade& t : r.result.trades) {
                    out_ << "TRADE " << symbol_ << ' ' << t.maker_order_id << ' '
                         << t.taker_order_id << ' ' << t.price << ' ' << t.quantity
                         << '\n';
                }

                // One market-data line per book change (top-of-book snapshot).
                out_ << "MarketData " << symbol_ << " bid ";
                if (r.data.has_bid) {
                    out_ << r.data.bid_price << ' ' << r.data.bid_quantity;
                } else {
                    out_ << "- -";
                }
                out_ << " ask ";
                if (r.data.has_ask) {
                    out_ << r.data.ask_price << ' ' << r.data.ask_quantity;
                } else {
                    out_ << "- -";
                }
                out_ << '\n';
            }

        }
    }
}
