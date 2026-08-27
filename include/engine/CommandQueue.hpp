#pragma once
#include <cstdint>

#include <engine/BoundedBlockingQueue.hpp>
#include <engine/Order.hpp>

namespace Engine {

    // The request received from client along with the globally unique order ID
    // MPSC queue
    struct Command {
        uint64_t order_id{};  // Globally unique order ID
        OrderRequest request{};
        uint64_t enqueue_ns{0}; // Used to calculate latency
    };

    using CommandQueue = BoundedBlockingQueue<Command>;

}
