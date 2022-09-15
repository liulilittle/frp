#pragma once

#include <frp/stdafx.h>
#include <frp/net/IPEndPoint.h>
#include <frp/messages/PacketCommands.h>
#include <frp/messages/NetworkAddress.h>

namespace frp {
    namespace io {
        class BinaryReader;
        class Stream;
    }

    namespace messages {
        class Packet final {
        public:
            frp::messages::PacketCommands   Command;
            int                             Id;
            int                             Offset;
            int                             Length;
            std::shared_ptr<Byte>           Buffer;

        public:
            std::shared_ptr<Byte>           Serialize(int& length) noexcept;
            bool                            Serialize(frp::io::Stream& stream) noexcept;

        public:
            static std::shared_ptr<Packet>  Deserialize(const void* message, int length) noexcept;
            static std::shared_ptr<Packet>  Deserialize(const std::shared_ptr<Byte>& message, int offset, int length) noexcept;

        public:
            template<class TProtocol>
            static bool                     UnpackWriteTo(Packet& packet, boost::asio::ip::basic_endpoint<TProtocol>& remoteEP) noexcept {
                typedef frp::net::IPEndPoint IPEndPoint;

                if (packet.Offset < 0 || packet.Length < 1) {
                    return false;
                }

                const Byte* buffer = packet.Buffer.get();
                if (NULL == buffer) {
                    return false;
                }

                int next;
                remoteEP = NetworkAddress::ToAddress<TProtocol>(buffer + packet.Offset, packet.Length, next);

                const int remotePort = remoteEP.port();
                if (remotePort <= IPEndPoint::MinPort || remotePort > IPEndPoint::MaxPort) {
                    return false;
                }

                const boost::asio::ip::address address = remoteEP.address();
                if (address.is_unspecified() || address.is_multicast()) {
                    return false;
                }

                packet.Offset += next;
                packet.Length -= next;
                return packet.Length > ~0;
            }
            static bool                     PackWriteTo(frp::io::Stream& stream, const void* buffer, int offset, int length, const boost::asio::ip::udp::endpoint& endpoint) noexcept;
        };
    }
}