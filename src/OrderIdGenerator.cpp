#include <engine/OrderIdGenerator.hpp>

namespace Engine
{

uint64_t OrderIdGenerator::next() noexcept
{
    return next_.fetch_add(1, memory_order_relaxed);
}

}