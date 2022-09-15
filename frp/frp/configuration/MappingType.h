#pragma once

#include <frp/stdafx.h>

namespace frp {
    namespace configuration {
        enum MappingType {
            MappingType_None,
            MappingType_TCP = MappingType_None,
            MappingType_UDP,
            MappingType_MaxType,
        };
    }
}