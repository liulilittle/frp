#pragma once

#include <frp/IDisposable.h>
#include <frp/threading/Hosting.h>
#include <frp/collections/TransmissionManager.h>
#include <frp/configuration/MappingConfiguration.h>

namespace frp {
    namespace transmission {
        class ITransmission;
    }

    namespace messages {
        class Packet;
    }

    namespace server {
        class Switches;
        class Connection;

        class MappingEntry final : public IDisposable, public frp::collections::TransmissionManager<Connection> {
            friend class Connection;

        protected:
            typedef frp::configuration::MappingType                     MappingType;

        public:
            const std::string                                           Name;
            const frp::configuration::MappingType                       Type;
            const int                                                   Port;

        public:
            MappingEntry(const std::shared_ptr<Switches>&               switches, 
                const std::string&                                      name, 
                frp::configuration::MappingType                         type, 
                int                                                     port) noexcept;

        public:
            inline const std::shared_ptr<Switches>&                     GetSwitches() noexcept {
                return switches_;
            }
            void                                                        Close() noexcept;
            virtual void                                                Dispose() noexcept override;
            bool                                                        Open() noexcept;

        private:
            bool                                                        ForwardedToFrpClientAsync() noexcept;
            bool                                                        PacketInputAsync(const TransmissionPtr& transmission) noexcept;
            bool                                                        SendToFrpClientAsync(const void* buffer, int length, const boost::asio::ip::udp::endpoint& endpoint) noexcept;

        private:
            bool                                                        Then(const TransmissionPtr& transmission, bool success) noexcept;
            bool                                                        OnPacketInput(const TransmissionPtr& transmission, const std::shared_ptr<frp::messages::Packet>& packet) noexcept;

        private:
            bool                                                        OnHandleConnectOK(const TransmissionPtr& transmission, int id) noexcept;
            bool                                                        OnHandleDisconnect(const TransmissionPtr& transmission, int id) noexcept;
            bool                                                        OnHandleWrite(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept;
            bool                                                        OnHandleWriteTo(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept;

        private:
            bool                                                        AcceptConnection(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;

        public:
            int                                                         AddTransmission(const TransmissionPtr& transmission) noexcept;
            void                                                        CloseTransmission(const TransmissionPtr& transmission) noexcept;

        private:
            std::atomic<bool>                                           disposed_;
            std::shared_ptr<Switches>                                   switches_;
            std::shared_ptr<frp::threading::Hosting>                    hosting_;
            std::shared_ptr<boost::asio::io_context>                    context_;
            std::shared_ptr<Byte>                                       buffer_;
            boost::asio::ip::tcp::acceptor                              acceptor_;
            boost::asio::ip::udp::socket                                socket_;
            boost::asio::ip::udp::endpoint                              endpoint_;
        };
    }
}