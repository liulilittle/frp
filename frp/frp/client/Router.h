#pragma once

#include <frp/IDisposable.h>
#include <frp/threading/Hosting.h>
#include <frp/transmission/ITransmission.h>
#include <frp/configuration/AppConfiguration.h>
#include <frp/collections/RestartTasksManger.h>
#include <frp/collections/TransmissionManager.h>
#include <frp/collections/DatagramPortManager.h>

namespace frp {
    namespace net {
        class Socket;
    }

    namespace messages {
        class Packet;
        class HandshakeRequest;
    }

    namespace client {
        class Router : public IDisposable {
        public:
            typedef frp::configuration::MappingType                         MappingType;
            typedef frp::configuration::AppConfiguration                    AppConfiguration;
            typedef frp::configuration::MappingConfiguration                MappingConfiguration;
            typedef std::shared_ptr<frp::transmission::ITransmission>       TransmissionPtr;

        public:
            class DatagramPort;
            class Connection;
            class MappingEntry;
            typedef frp::collections::TransmissionManager<Connection>                           TransmissionManager;
            typedef frp::collections::DatagramPortManager<std::shared_ptr<DatagramPort> const>  DatagramPortManager;
            typedef frp::collections::RestartTasksManger                                        RestartTasksManger;

        public:
            class DatagramPort final : public IDisposable {
                friend class MappingEntry;

                /* DNS query only-port 53 timeout is 3 seconds. */
                static const int                                            DynamicNamespaceQueryTimeout = 3;
                static const int                                            DynamicNamespaceQueryPort = 53;

            public:
                DatagramPort(const std::shared_ptr<MappingEntry>& entry, boost::asio::ip::udp::endpoint remoteEP) noexcept;

            public:
                bool                                                        Open() noexcept;
                void                                                        Close() noexcept;
                virtual void                                                Dispose() noexcept override;
                bool                                                        SendToLocalClientAsync(const void* buffer, int offset, int length) noexcept;

            private:
                bool                                                        Then(const TransmissionPtr& transmission, bool success) noexcept;
                void                                                        ClearTimeout() noexcept;
                bool                                                        Timeout() noexcept;
                bool                                                        ForwardedToFrpServerAsync() noexcept;
                bool                                                        SendToFrpServerAsync(const void* buffer, int length) noexcept;

            private:
                std::atomic<bool>                                           disposed_;
                UInt64                                                      last_;
                std::atomic<int>                                            onlydns_;
                std::shared_ptr<MappingEntry>                               entry_;
                std::shared_ptr<frp::threading::Hosting>                    hosting_;
                std::shared_ptr<boost::asio::io_context>                    context_;
                std::shared_ptr<AppConfiguration>                           configuration_;
                std::shared_ptr<Byte>                                       buffer_;
                boost::asio::ip::udp::socket                                socket_;
                boost::asio::ip::udp::endpoint                              endpoint_;
                boost::asio::ip::udp::endpoint                              localEP_;
                boost::asio::ip::udp::endpoint                              remoteEP_;
                std::shared_ptr<boost::asio::deadline_timer>                timeout_;
            };
            class Connection final : public IDisposable {
                friend class MappingEntry;
                struct message {
                    std::shared_ptr<Byte>                                   packet;
                    int                                                     packet_size;
                };
                typedef std::shared_ptr<message>                            pmessage;
                typedef std::list<pmessage>                                 message_queue;

            public:
                typedef frp::transmission::ITransmission                    ITransmission;
                typedef ITransmission::WriteAsyncCallback                   WriteAsyncCallback;

            public:
                const int                                                   Id;
                const int                                                   BufferSize = 4096;

            public:
                Connection(
                    const std::shared_ptr<MappingEntry>&                    entry, 
                    const TransmissionPtr&                                  transmission,
                    int                                                     id, 
                    const boost::asio::ip::tcp::endpoint&                   remoteEP) noexcept;

            public:
                bool                                                        AcceptFrpConnectionAsync() noexcept;
                enum {
                    CONNECTION_STATUS_UNOPEN,
                    CONNECTION_STATUS_OPENING,
                    CONNECTION_STATUS_OPEN_OK,
                    CONNECTION_STATUS_CLOSE,
                };
                void                                                        Close() noexcept;
                virtual void                                                Dispose() noexcept override;

            private:
                bool                                                        Then(bool success) noexcept;
                void                                                        ClearTimeout() noexcept;
                bool                                                        ConnectToLocalClientOnOK() noexcept;
                bool                                                        ForwardedToFrpServerAsync() noexcept;

            private:
                bool                                                        OnAsyncWrite(bool internall) noexcept;
                bool                                                        SendToLocalClientAsync(const void* buffer, int offset, int length) noexcept;

            private:
                bool                                                        SendToFrpServerAsync(const frp::messages::Packet& packet, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;
                bool                                                        SendToFrpServerAsync(const void* buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;
                bool                                                        SendCommandToFrpServerAsync(int command, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;

            private:
                std::shared_ptr<Byte>                                       buffer_;
                std::atomic<int>                                            status_;
                std::shared_ptr<MappingEntry>                               entry_;
                std::shared_ptr<frp::threading::Hosting>                    hosting_;
                std::shared_ptr<AppConfiguration>                           configuration_;
                TransmissionPtr                                             transmission_;
                std::shared_ptr<boost::asio::io_context>                    context_;
                boost::asio::ip::tcp::socket                                socket_;
                boost::asio::ip::tcp::endpoint                              localEP_;
                boost::asio::ip::tcp::endpoint                              remoteEP_;
                std::atomic<bool>                                           writing_;
                message_queue                                               messages_;
                std::shared_ptr<boost::asio::deadline_timer>                timeout_;
            };
            class MappingEntry final : public IDisposable, public TransmissionManager, public DatagramPortManager, public RestartTasksManger {
                friend class DatagramPort;
                friend class Connection;

            public:
                MappingEntry(const std::shared_ptr<Router>& router, const MappingConfiguration& mapping) noexcept;

            public:
                const std::shared_ptr<Router>&                              GetRouter() noexcept {
                    return router_;
                }
                const MappingConfiguration&                                 GetMappingConfiguration() noexcept {
                    return mapping_;
                }
                inline const std::shared_ptr<AppConfiguration>&             GetConfiguration() noexcept {
                    return configuration_;
                }
                inline const std::shared_ptr<frp::threading::Hosting>&      GetHosting() noexcept {
                    return hosting_;
                }
                bool                                                        Open() noexcept;
                void                                                        Close() noexcept;
                virtual void                                                Dispose() noexcept override;

            private:
                bool                                                        RestartTransmission() noexcept;
                bool                                                        HandshakeTransmission(const TransmissionPtr& transmission);
                bool                                                        ConnectTransmission(const std::shared_ptr<AppConfiguration>& configuration, const std::shared_ptr<frp::threading::Hosting>& hosting) noexcept;
                bool                                                        AcceptTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;

            private:
                bool                                                        Then(const TransmissionPtr& transmission, bool success) noexcept;
                bool                                                        PacketInputAsync(const TransmissionPtr& transmission) noexcept;
                bool                                                        OnPacketInput(const TransmissionPtr& transmission, const std::shared_ptr<frp::messages::Packet>& packet) noexcept;

            private:
                bool                                                        OnHandleConnect(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept;
                bool                                                        OnHandleDisconnect(const TransmissionPtr& transmission, int id) noexcept;
                bool                                                        OnHandleWrite(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept;
                bool                                                        OnHandleWriteTo(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept;

            public:
                bool                                                        AddTransmission(const TransmissionPtr& transmission) noexcept;
                bool                                                        CloseTransmission(const TransmissionPtr& transmission) noexcept;

            private:
                std::atomic<bool>                                           disposed_;
                std::shared_ptr<Router>                                     router_;
                MappingConfiguration                                        mapping_;
                std::shared_ptr<AppConfiguration>                           configuration_;
                std::shared_ptr<frp::threading::Hosting>                    hosting_;
            };
            typedef std::shared_ptr<MappingEntry>                           MappingEntryPtr;
            typedef std::unordered_map<void*, MappingEntryPtr>              MappingEntryTable;

        public:
            Router(const std::shared_ptr<frp::threading::Hosting>& hosting, const std::shared_ptr<AppConfiguration>& configuration) noexcept;

        public:
            inline const std::shared_ptr<AppConfiguration>&                 GetConfiguration() noexcept {
                return configuration_;
            }
            inline const std::shared_ptr<frp::threading::Hosting>&          GetHosting() noexcept {
                return hosting_;
            }

        public:
            virtual void                                                    Dispose() noexcept override;
            virtual bool                                                    Open() noexcept;

        protected:
            virtual TransmissionPtr                                         CreateTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;

        private:
            bool                                                            CloseEntry(MappingEntry* entry) noexcept;
            void                                                            CloseAllEntries() noexcept;

        private:
            std::atomic<bool>                                               disposed_;
            std::shared_ptr<AppConfiguration>                               configuration_;
            std::shared_ptr<frp::threading::Hosting>                        hosting_;
            MappingEntryTable                                               mappings_;
        };
    }
}