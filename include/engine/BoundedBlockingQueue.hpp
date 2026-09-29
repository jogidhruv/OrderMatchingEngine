#pragma once
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace Engine {
    template <typename T>
    class BoundedBlockingQueue {
    public:
        explicit BoundedBlockingQueue(size_t capacity)
            : capacity_(capacity) {
            ring_.reserve(capacity_);
        }

        void push(T value) {
            std::unique_lock lock(mutex_);
            not_full_.wait(lock, [this] { return size_ < capacity_ || closed_; });
            if (closed_) {
                return;
            }
            ++size_;
            ring_[tail_] = std::move(value);
            tail_ = (tail_ + 1) % capacity_;
        }

        std::size_t drain(std::vector<T>& out, const std::size_t max_batch) {
            const std::size_t n = std::min(max_batch, size_);
            bool was_full;
            {
                std::unique_lock lock(mutex_);
                if (size_ == 0) {
                    return 0;  // closed or empty
                }
                was_full = (size_ == capacity_);
                for (std::size_t i = 0; i < n; ++i) {
                    out[i] = std::move(ring_[head_]);  // reuse caller's pre-sized slots
                    head_ = (head_ + 1) % capacity_;
                }
                size_ -= n;
            }

            if (was_full) {
                not_full_.notify_all(); // Notify the producer threads since now queue has free slots
            }

            return n;
        }

        void close() {
            {
                std::scoped_lock lock{mutex_};
                closed_ = true;
            }
            not_full_.notify_all();
        }

        [[nodiscard]] bool closed() const { return closed_; }

    private:
        std::vector<T> ring_;
        size_t capacity_;
        size_t head_{0};
        size_t tail_{0};
        size_t size_{0};

        // Synchronization to avoid queue overflow and consumer trying to read empty queue issues.
        std::mutex mutex_;
        std::condition_variable not_full_;
        bool closed_{false};
    };
}
