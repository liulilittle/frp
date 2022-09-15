#pragma once

#include <frp/configuration/MappingType.h>

namespace frp {
    namespace configuration {
        struct MappingConfiguration {
            MappingType                             Type = MappingType::MappingType_TCP;
            std::string                             Name;
            std::string                             LocalIP;
            int                                     Reconnect = 1;
            int                                     Concurrent = 1;
            int                                     LocalPort = 0;
            int                                     RemotePort = 0;
        };

        typedef std::vector<MappingConfiguration>   MappingConfigurationArrayList;
    }
}