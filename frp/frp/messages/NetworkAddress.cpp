#include <frp/messages/NetworkAddress.h>
#include <frp/messages/Packet.h>

namespace frp {
    namespace messages {
        void NetworkAddress::ToAddress(
            Packet&                                 packet, 
            NetworkAddressV4&                       addressV4, 
            NetworkAddressV6&                       addressV6, 
            const boost::asio::ip::udp::endpoint&   remoteEP) noexcept {
            packet.Offset = 0;
            boost::asio::ip::address address = remoteEP.address();
            if (address.is_v4()) {
                addressV4.af = frp::messages::AddressFamily::InterNetwork;
                addressV4.port = htons(remoteEP.port());

                boost::asio::ip::address_v4::bytes_type address_bytes = address.to_v4().to_bytes();
                memcpy(addressV4.address.bytes, address_bytes.data(), sizeof(addressV4.address));

                packet.Length = sizeof(addressV4);
                packet.Buffer = std::shared_ptr<Byte>((Byte*)&addressV4, [](const void*) noexcept {});
            }
            elif(address.is_v6()) {
                addressV6.af = frp::messages::AddressFamily::InterNetworkV6;
                addressV6.port = htons(remoteEP.port());

                boost::asio::ip::address_v6::bytes_type address_bytes = address.to_v6().to_bytes();
                memcpy(addressV6.address, address_bytes.data(), sizeof(addressV6.address));

                packet.Length = sizeof(addressV6);
                packet.Buffer = std::shared_ptr<Byte>((Byte*)&addressV6, [](const void*) noexcept {});
            }
            else {
                packet.Length = 0;
            }
        }

        std::string NetworkAddress::ToAddress(const boost::asio::ip::udp::endpoint& remoteEP) noexcept {
            Packet packet;
            NetworkAddressV4 addressV4;
            NetworkAddressV6 addressV6;

            ToAddress(packet, addressV4, addressV6, remoteEP);
            if (packet.Length < 1) {
                return std::string();
            }
            else {
                return std::string((char*)packet.Buffer.get() + packet.Offset, packet.Length);
            }
        }
    }
}