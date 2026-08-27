# pragma once

#include <cstdint>
#include <string>

namespace Engine
{

    enum class Side : std::uint8_t {
        BUY = 1,
        SELL = 2,
    };

    enum class OrderType : std::uint8_t {
        Limit = 1,
        Market = 2,
        IOC = 3,
        Cancel = 4,
        Modify = 5,
    };

    // Possible reasons a request could have been rejected sync or async
    enum class RejectReason : std::uint8_t {
        None = 0,
        UnknownSymbol = 1,
        InvalidPrice = 2,
        InvalidQuantity = 3,
        InvalidOrderType = 4,
        DuplicateOrderId = 5,
        UnknownOrderId = 6,
    };

    // Conversion used by logging mechanism in Sink
    inline std::string to_string(RejectReason reason) {
        switch (reason) {
            case RejectReason::None: return "None";
            case RejectReason::UnknownSymbol: return "UnknownSymbol";
            case RejectReason::InvalidPrice: return "InvalidPrice";
            case RejectReason::InvalidQuantity: return "InvalidQuantity";
            case RejectReason::InvalidOrderType: return "InvalidOrderType";
            case RejectReason::DuplicateOrderId: return "DuplicateOrderId";
            case RejectReason::UnknownOrderId: return "UnknownOrderId";
            default: return "Unknown";
        }
    }

}