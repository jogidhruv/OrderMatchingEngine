# pragma once

#include<engine/Types.hpp>
#include<atomic>

using namespace std;

namespace Engine
{

class OrderIdGenerator
{

public:

    // Globally unique order ID generator
    explicit OrderIdGenerator(uint64_t first = 1) : next_(first) {}

    [[nodiscard]] uint64_t next() noexcept;

private:

    atomic<uint64_t> next_;
};

}