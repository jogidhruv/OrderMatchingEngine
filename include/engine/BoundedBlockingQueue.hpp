#pragma once
#include <algorithm>
#include <condition_variable>
#include <vector>

namespace Engine {
    template <typename T>
    class BoundedBlockingQueue {
    public:
        explicit BoundedBlockingQueue(size_t capacity)
            : ring_(capacity), capacity_(capacity) {}

        void push(T&& value) {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_.wait(lock, [this] { return size_ < capacity_ || closed_; });
            if (closed_) {
                return;
            }
            const bool was_empty = (size_ == 0);
            ring_[tail_] = std::move(value);  // move the trades buffer, no copy
            tail_ = (tail_ + 1) % capacity_;
            size_++;
            if (was_empty) {
                lock.unlock();
                not_empty_.notify_all();  // wake the single writer
            }
        }

        std::size_t drain(std::vector<T>& out, const std::size_t max_batch) {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this] { return size_ > 0 || closed_; });
            if (size_ == 0) {
                return 0;  // closed && empty
            }
            const bool was_full = (size_ == capacity_);
            const std::size_t n = (max_batch < size_) ? max_batch : size_;
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = std::move(ring_[head_]);  // reuse caller's pre-sized slots
                head_ = (head_ + 1) % capacity_;
            }
            size_ -= n;
            if (was_full) {
                lock.unlock();
                not_full_.notify_all();  // freed slots -> wake the producer(s)
            }
            return n;
        }


        void close() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            not_empty_.notify_all();
            not_full_.notify_all();
        }

    private:
        std::vector<T> ring_;
        size_t capacity_;
        size_t head_{0};
        size_t tail_{0};
        size_t size_{0};

        // Synchronization to avoid queue overflow and Sink trying to read empty queue issues.
        std::mutex mutex_;
        std::condition_variable not_empty_;
        std::condition_variable not_full_;
        bool closed_{false};
    };
}
