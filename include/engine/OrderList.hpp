#pragma once
#include "OrderListIterator.hpp"

namespace Engine {
    struct Order;

    class OrderList {
    public:
        OrderList() = default;

        void insert(Order* order);
        void remove(Order* order);
        [[nodiscard]] OrderListIterator begin() const {return { head_ }; }
        [[nodiscard]] OrderListIterator end() const { return { nullptr }; }
        [[nodiscard]] bool empty() const;
        [[nodiscard]] Order* front() const;

    private:
        Order* head_ = nullptr;
        Order* tail_ = nullptr;
    };

}
