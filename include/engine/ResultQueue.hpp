#pragma once
#include <vector>

#include <engine/BoundedBlockingQueue.hpp>
#include <engine/MatchEngine.hpp>

namespace Engine {
    // This is used by the Sink for logging
    struct MatchingResult {
        SubmitResult result;
        MarketData data;
    };

    using ResultQueue = BoundedBlockingQueue<MatchingResult>;
}
