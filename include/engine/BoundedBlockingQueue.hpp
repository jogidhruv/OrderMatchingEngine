#pragma once
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace Engine {
    template <typename T> requires std::is_default_constructible_v<T>
    class BoundedBlockingQueue {
        struct alignas(64) Padded {
            T data;
        };
        std::vector<Padded> ring_;
        size_t capacity_;
        alignas(64) size_t head_{0};
        alignas(64) size_t tail_{0};
        alignas(64) std::atomic<size_t> size_{0};

        // Synchronization to avoid queue overflow and consumer trying to read empty queue issues.
        std::mutex mutex_;
        std::atomic<bool> closed_{false};
    public:
        explicit BoundedBlockingQueue(size_t capacity)
            : capacity_(capacity) {
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

                ring_[tail_].data = std::move(value);
                tail_ = (tail_ + 1) % capacity_;
            }
            size_.fetch_add(1, std::memory_order_release);
        }

        std::size_t drain(std::vector<T>& out, const std::size_t max_batch) {
            const size_t qs = size_.load(std::memory_order_relaxed);
            if (qs == 0) return 0;
            const std::size_t n = std::min(max_batch, qs);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = std::move(ring_[head_].data);  // reuse caller's pre-sized slots
                head_ = (head_ + 1) % capacity_;
            }
            size_.fetch_sub(n, std::memory_order_release);
            return n;
        }

        void close() {
            closed_.store(true);
        }

        [[nodiscard]] bool closed() const { return closed_.load(std::memory_order_relaxed); }

    };
}
