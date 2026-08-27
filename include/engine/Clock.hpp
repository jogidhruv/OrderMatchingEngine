#pragma once
#include <chrono>
#include <cstdint>

namespace Engine {
    // Used to record the timestamp when the request arrived
    // Later used to compute the request latency after engine processed it for benchmark program
    inline std::uint64_t now_ns() {
        const auto duration = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    }
}
