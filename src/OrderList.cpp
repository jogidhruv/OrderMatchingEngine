#include <engine/OrderList.hpp>

#include "engine/Order.hpp"

namespace Engine {
    void OrderList::insert(Order* order) {
        if (head_ == nullptr) {
            head_ = order;
            tail_ = order;
            order->next_in_list = nullptr;
            order->prev_in_list = nullptr;
            return;
        }

        tail_->next_in_list = order;
        order->prev_in_list = tail_;
        order->next_in_list = nullptr;
        tail_ = order;
    }

    void OrderList::remove(Order* order) {
        if (order->prev_in_list == nullptr && order->next_in_list == nullptr) {
            head_ = nullptr;
            tail_ = nullptr;
        }
        else if (order->prev_in_list == nullptr) {
            head_ = order->next_in_list;
            head_->prev_in_list = nullptr;
        }
        else if (order->next_in_list == nullptr) {
            tail_ = order->prev_in_list;
            tail_->next_in_list = nullptr;
        }
        else {
            Order* prev = order->prev_in_list;
            Order* next = order->next_in_list;
            prev->next_in_list = next;
            next->prev_in_list = prev;
        }
    }

    Order *OrderList::front() const {
        return head_;
    }

    bool OrderList::empty() const {
        return head_ == nullptr;
    }
}
