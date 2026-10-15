#pragma once
#include <algorithm>
#include <bit>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace Engine {
    template <typename T, size_t capacity_> requires (std::is_default_constructible_v<T> && std::has_single_bit(capacity_))
    class BoundedBlockingQueue {
        struct alignas(64) Padded {
            T data;
        };
        std::vector<Padded> ring_;
        alignas(64) size_t head_{0};
        alignas(64) size_t tail_{0};
        alignas(64) std::atomic<size_t> size_{0};
        std::mutex mutex_;
        std::atomic<bool> closed_{false};

    public:
        explicit BoundedBlockingQueue() {
            ring_.resize(capacity_);
        }

        void push(T value) {
            if (closed_.load()) {
                return;
            }
            {
                std::unique_lock lock(mutex_);
                /*
                while (size_.load(std::memory_order_relaxed) >= capacity_ && !closed_.load(std::memory_order_relaxed)) {
                    std::this_thread::yield();
                }*/
                ring_[tail_++ & (capacity_ - 1)].data = value;
            }
            size_.fetch_add(1);
        }

        std::size_t drain(std::vector<T>& out, const std::size_t max_batch) {
            const size_t qs = size_.load(std::memory_order_relaxed);
            if (qs == 0) return 0;
            const std::size_t n = std::min(max_batch, qs);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = ring_[head_++ & (capacity_ - 1)].data;  // reuse caller's pre-sized slots
            }
            size_.fetch_sub(n);
            return n;
        }

        void close() {
            closed_.store(true);
        }

        [[nodiscard]] bool closed() const { return closed_.load(std::memory_order_relaxed); }

    };
}
