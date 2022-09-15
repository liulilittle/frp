#pragma once

#include <frp/stdafx.h>

namespace frp {
    namespace messages {
#pragma pack(push, 1)
        enum AddressFamily {
            InterNetwork,
            InterNetworkV6,
        };

        struct NetworkAddressV4 {
            Byte                                                                af;
            UInt16                                                              port;
            union {
                UInt32                                                          dword;
                Byte                                                            bytes[4];
            }                                                                   address;
        };

        struct NetworkAddressV6 {
            Byte                                                                af;
            UInt16                                                              port;
            Byte                                                                address[16];
        };
#pragma pack(pop)
        class Packet;

        class NetworkAddress final {
        public:
            template<class TProtocol>
            inline static boost::asio::ip::basic_endpoint<TProtocol>            ToAddress(const void* address, int length, int& next) noexcept {
                typedef boost::asio::ip::basic_endpoint<TProtocol> protocol_endpoint;

                next = 0;
                if (!address || length < sizeof(NetworkAddressV4)) {
                    return protocol_endpoint(boost::asio::ip::address_v4::any(), 0);
                }

                NetworkAddressV4* addressV4 = (NetworkAddressV4*)address;
                if (addressV4->af == AddressFamily::InterNetwork) {
                    next = sizeof(*addressV4);
                    boost::asio::ip::address_v4 in4(ntohl(addressV4->address.dword));
                    return protocol_endpoint(in4, ntohs(addressV4->port));
                }
                elif(addressV4->af == AddressFamily::InterNetworkV6) {
                    NetworkAddressV6* addressV6 = (NetworkAddressV6*)address;
                    if (length < sizeof(*addressV6)) {
                        return protocol_endpoint(boost::asio::ip::address_v4::any(), 0);
                    }
                    else {
                        next = sizeof(*addressV6);
                    }

                    boost::asio::ip::address_v6::bytes_type address_bytes;
                    memcpy(address_bytes.data(), addressV6->address, address_bytes.size());

                    boost::asio::ip::address_v6 in6(address_bytes);
                    return protocol_endpoint(in6, ntohs(addressV6->port));
                }
                else {
                    return protocol_endpoint(boost::asio::ip::address_v4::any(), 0);
                }
            }
            
        public:
            template<class TProtocol>
            inline static boost::asio::ip::basic_endpoint<TProtocol>            ToAddress(const void* address, int length) {
                int next;
                return ToAddress<TProtocol>(address, length, next);
            }

        public:
            static void                                                         ToAddress(
                Packet&                                                         packet, 
                NetworkAddressV4&                                               addressV4, 
                NetworkAddressV6&                                               addressV6, 
                const boost::asio::ip::udp::endpoint&                           remoteEP) noexcept;
            static std::string                                                  ToAddress(const boost::asio::ip::udp::endpoint& remoteEP) noexcept;
        };
    }
}