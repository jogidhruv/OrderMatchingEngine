#pragma once
#include <optional>
#include <vector>

namespace Engine {
    template <typename T>
    class SPSCQueue {
        std::vector<T> ring_;
        std::atomic<size_t> tail_{1};
        std::atomic<size_t> head_{0};
        size_t capacity_;
        std::atomic<bool> closed_{false};

    public:
        explicit SPSCQueue(const size_t capacity) : capacity_(capacity) {
            ring_.reserve(capacity_);
        }

        bool push(T value) {
            const auto tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire)) {
                return false; // We drop the element since Queue got full
            }

            ring_[tail] = std::move(value);
            tail_.fetch_add(1, std::memory_order_release);
            return true;
        }

        std::optional<T> pop() {
            const auto head = (head_.load(std::memory_order_relaxed) + 1) % capacity_;
            if (head == tail_.load(std::memory_order_acquire)) {
                return {}; // Queue is empty. Nothing to pop
            }
            auto ret = std::optional<T>{std::move(ring_[head])};
            head_.fetch_add(1, std::memory_order_release);
            return ret;
        }

        [[nodiscard]] bool is_closed() const {
            return closed_.load(std::memory_order_relaxed);
        }

        void close() {
            closed_.store(true, std::memory_order_relaxed);
        }
    };
}
