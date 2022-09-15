#pragma once

#include <frp/Reference.h>
#include <frp/collections/Dictionary.h>

namespace frp {
    namespace collections {
        template<class DatagramPort> class DatagramPortManager;
        template<class DatagramPort>
        class DatagramPortManager<std::shared_ptr<DatagramPort> const> {
        protected:
            typedef std::shared_ptr<DatagramPort>                       DatagramPortPtr;

        private:
            typedef std::unordered_map<std::string, DatagramPortPtr>    DatagramPortTable;

        private:
            template<class TProtocol>
            inline static std::string                                   ToKey(const boost::asio::ip::basic_endpoint<TProtocol>& endpoint) noexcept {
                return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
            }

        protected:
            inline bool                                                 ReleaseDatagramPort(const boost::asio::ip::udp::endpoint& endpoint) noexcept {
                DatagramPortPtr datagramPort;
                if (!Dictionary::TryRemove(datagramPorts_, ToKey(endpoint), datagramPort)) {
                    return false;
                }

                datagramPort->Close();
                return true;
            }
            inline void                                                 ReleaseAllDatagramPort() noexcept {
                Dictionary::ReleaseAllPairs(datagramPorts_);
            }
            inline DatagramPortPtr                                      GetDatagramPort(const boost::asio::ip::udp::endpoint& endpoint) noexcept {
                DatagramPortPtr datagramPort;
                Dictionary::TryGetValue(datagramPorts_, ToKey(endpoint), datagramPort);
                return std::move(datagramPort);
            }

        protected:
            template<class Creator>
            inline DatagramPortPtr                                      AllocDatagramPort(const boost::asio::ip::udp::endpoint& endpoint, Creator&& creator) noexcept {
                std::string key = ToKey(endpoint);
                std::shared_ptr<DatagramPort> datagramPort;
                if (!Dictionary::TryGetValue(datagramPorts_, key, datagramPort)) {
                    datagramPort = creator(endpoint);
                    if (!datagramPort) {
                        return NULL;
                    }

                    bool success = datagramPort->Open() && Dictionary::TryAdd(datagramPorts_, key, datagramPort);
                    if (!success) {
                        datagramPort->Close();
                        return NULL;
                    }
                }
                return datagramPort;
            }

        private:
            DatagramPortTable                                           datagramPorts_;
        };
    }
}