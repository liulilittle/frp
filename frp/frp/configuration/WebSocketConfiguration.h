#pragma once

#include <frp/stdafx.h>

namespace frp {
    namespace configuration {
        struct WebSocketConfiguration {
            std::string                         Host;
            std::string                         Path;
        };
    }
}