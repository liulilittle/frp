#pragma once

#include <frp/configuration/MappingConfiguration.h>

namespace frp {
    namespace io {
        class BinaryReader;
        class Stream;
    }

    namespace messages {
        class HandshakeRequest final {
        public:
            frp::configuration::MappingType             Type;
            std::string                                 Name;
            int                                         RemotePort;

        public:
            std::shared_ptr<Byte>                       Serialize(int& length) noexcept;
            bool                                        Serialize(frp::io::Stream& stream) noexcept;

        public:
            static std::shared_ptr<HandshakeRequest>    Deserialize(const void* message, int length) noexcept;
            static std::shared_ptr<HandshakeRequest>    Deserialize(const std::shared_ptr<Byte> message, int offset, int length) noexcept;
            static std::shared_ptr<HandshakeRequest>    Deserialize(frp::io::Stream& stream) noexcept;
            static std::shared_ptr<HandshakeRequest>    Deserialize(frp::io::BinaryReader& reader) noexcept;
        };
    }
}