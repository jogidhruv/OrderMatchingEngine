#pragma once
#include "Order.hpp"

namespace Engine {
    struct Order;

    struct OrderListIterator {
        Order* current;

        OrderListIterator& operator++() { current = current->next_in_list; return *this; }
        OrderListIterator& operator--() { current = current->prev_in_list; return *this; }
        bool operator==(const OrderListIterator& other) const {
            return current == other.current;
        }

        Order& operator*() const { return *current; }
    };
}
