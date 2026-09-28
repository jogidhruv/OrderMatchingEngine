#pragma once
#include "OrderTypes.hpp"

namespace Engine {
    struct OrderTypes;

    struct OrderListIterator {
        OrderTypes* current;

        OrderListIterator& operator++() { current = current->next_in_list; return *this; }
        OrderListIterator& operator--() { current = current->prev_in_list; return *this; }
        bool operator==(const OrderListIterator& other) const {
            return current == other.current;
        }

        OrderTypes& operator*() const { return *current; }
    };
}
