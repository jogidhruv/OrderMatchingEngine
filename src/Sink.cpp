#include <utility>

#include "engine/Sink.hpp"

namespace Engine {

    Sink::Sink(std::string symbol, std::ostream& out, const size_t capacity)
        : queue_(capacity),
          out_(out),
          symbol_(std::move(symbol)) {}

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

    void Sink::ProcessMR(const MatchingResult& mr) const {
        const auto& result = mr.result;
        const auto& data = mr.data;
        if (result.rejectReason != RejectReason::None) {
            out_ << "REJECT " << to_string(result.rejectReason) << ' ' << symbol_ << ' ' << result.clientId
                    << ' ' << result.clientOrderId << '\n';
            return;
        }

        // One execution-log line per fill.
        for (const Trade& t : result.trades) {
            out_ << "TRADE " << symbol_ << ' ' << t.maker_order_id << ' '
                 << t.taker_order_id << ' ' << t.price << ' ' << t.quantity
                 << '\n';
        }

        // One market-data line per book change (top-of-book snapshot).
        out_ << "MarketData " << symbol_ << " bid ";
        if (data.has_bid) {
            out_ << data.bid_price << ' ' << data.bid_quantity;
        } else {
            out_ << "- -";
        }
        out_ << " ask ";
        if (data.has_ask) {
            out_ << data.ask_price << ' ' << data.ask_quantity;
        } else {
            out_ << "- -";
        }
        out_ << '\n';

    }

    void Sink::run() {
        while (true) {
            if (queue_.is_closed()) {
                while (auto mr = queue_.pop()) {
                    ProcessMR(*mr);
                }
                break;
            }

            if (auto mr = queue_.pop()) {
                ProcessMR(*mr);
            }
        }
    }
}
