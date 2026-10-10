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
        std::atomic<bool> closed_{false};
        std::mutex mutex_;

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

                ring_[tail_].data= value;
                tail_ = (tail_ + 1) % capacity_;
            }
            size_.fetch_add(1);
        }

        std::size_t drain(std::vector<T>& out, const std::size_t max_batch) {
            const size_t qs = size_.load();
            if (qs == 0) return 0;
            const std::size_t n = std::min(max_batch, qs);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = ring_[(head_ + i) % capacity_].data;  // reuse caller's pre-sized slots
                //head_ = (head_ + 1) % capacity_;
            }
            head_ = (head_ + n - 1) % capacity_;
            size_.fetch_sub(n);
            return n;
        }

        void close() {
            closed_.store(true);
        }

        [[nodiscard]] bool closed() const { return closed_.load(std::memory_order_relaxed); }

    };
}
