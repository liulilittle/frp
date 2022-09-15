#pragma once

#include <frp/transmission/ITransmission.h>
#include <frp/configuration/AppConfiguration.h>

namespace frp {
    namespace messages {
        class Packet;
    }

    namespace server {
        class MappingEntry;

        class Connection final : public IDisposable {
            friend class MappingEntry;
            struct message {
                std::shared_ptr<Byte>                                           packet;
                int                                                             packet_size;
            };
            typedef std::shared_ptr<message>                                    pmessage;
            typedef std::list<pmessage>                                         message_queue;

        public:
            typedef frp::transmission::ITransmission                            ITransmission;
            typedef ITransmission::WriteAsyncCallback                           WriteAsyncCallback;
                                                                                
        public:                                                                 
            const int                                                           Id;
            const int                                                           BufferSize = 4096;
                                                                                
        public:                                                                 
            Connection(                                                         
                const std::shared_ptr<MappingEntry>&                            entry,
                const std::shared_ptr<boost::asio::ip::tcp::socket>&            socket,
                const std::shared_ptr<ITransmission>&                           transmission,
                int                                                             id) noexcept;
                                                        
        public:
            virtual void                                                        Dispose() noexcept override;
            void                                                                Close() noexcept;
            bool                                                                ConnectToFrpClientAsync(const boost::asio::ip::tcp::endpoint& remoteEP) noexcept;
            bool                                                                SendToPulicNeworkUserClient(const void* buffer, int offset, int length) noexcept;

        private:
            bool                                                                OnAsyncWrite(bool internall) noexcept;
            void                                                                ClearTimeout() noexcept;
            bool                                                                Then(bool success) noexcept;

        private:
            bool                                                                ConnectToFrpClientOnOK() noexcept;
            bool                                                                ForwardedToFrpClientAsync() noexcept;
            enum {
                CONNECTION_STATUS_UNOPEN,
                CONNECTION_STATUS_OPENING,
                CONNECTION_STATUS_OPEN_OK,
                CONNECTION_STATUS_CLOSE,
            };
            bool                                                                SendToFrpClientAsync(const frp::messages::Packet& packet, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;
            bool                                                                SendToFrpClientAsync(const void* buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;
            bool                                                                SendCommandToFrpClientAsync(int command, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;

        private:                                                                
            std::shared_ptr<MappingEntry>                                       entry_;
            std::shared_ptr<ITransmission>                                      transmission_;
            std::atomic<int>                                                    status_;
            std::shared_ptr<Byte>                                               buffer_;
            std::shared_ptr<boost::asio::ip::tcp::socket>                       socket_;
            std::shared_ptr<boost::asio::io_context>                            context_;
            std::shared_ptr<frp::configuration::AppConfiguration>               configuration_;
            std::shared_ptr<boost::asio::deadline_timer>                        timeout_;
            std::atomic<bool>                                                   writing_;
            message_queue                                                       messages_;
        };
    }
}