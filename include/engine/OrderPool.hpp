#pragma once
#include <cassert>

#include "OrderTypes.hpp"

namespace Engine {

    struct Chunk {
        OrderTypes objects_[512]{};
        Chunk* next_ = nullptr;
    };

    class OrderPool {
    public:
        OrderPool() {
            grow();
        }

        ~OrderPool() {
            while (chunk_) {
                Chunk* temp = chunk_->next_;
                delete chunk_;
                chunk_ = temp;
            }
        }

        OrderTypes* take() {
            if (!free_) {
                grow();
            }

            OrderTypes* o = free_;
            free_ = free_->next_;
            return o;
        }

        void release(OrderTypes* o) {
            assert(o);
            // Reset the order fields
            o->next_in_list = nullptr;
            o->prev_in_list = nullptr;
            o->client_id = 0;
            o->id = 0;
            o->price = 0;
            o->quantity = 0;
            o->remaining = 0;
            o->client_order_id = 0;
            o->timestamp = 0;

            o->next_ = free_;
            free_ = o;
        }

    private:
        void grow() {
            Chunk* chunk = new Chunk();
            chunk->next_ = chunk_;
            chunk_ = chunk;
            for (OrderTypes& order : chunk->objects_) {
                order.next_ = free_;
                free_ = &order;
            }
        }

        OrderTypes* free_ = nullptr;
        Chunk* chunk_ = nullptr;
    };

}
