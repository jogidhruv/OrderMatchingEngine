#pragma once
#include "OrderListIterator.hpp"

namespace Engine {
    struct OrderTypes;

    class OrderList {
    public:
        OrderList() = default;

        void insert(OrderTypes* order);
        void remove(OrderTypes* order);
        [[nodiscard]] OrderListIterator begin() const {return { head_ }; }
        [[nodiscard]] OrderListIterator end() const { return { nullptr }; }
        [[nodiscard]] bool empty() const;
        [[nodiscard]] OrderTypes* front() const;

    private:
        OrderTypes* head_ = nullptr;
        OrderTypes* tail_ = nullptr;
    };

}
