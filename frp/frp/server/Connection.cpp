#include <frp/server/Connection.h>
#include <frp/server/MappingEntry.h>
#include <frp/server/Switches.h>
#include <frp/threading/Hosting.h>
#include <frp/threading/Timer.h>
#include <frp/net/Socket.h>
#include <frp/messages/Packet.h>
#include <frp/messages/NetworkAddress.h>

namespace frp {
    namespace server {
        Connection::Connection(
            const std::shared_ptr<MappingEntry>&                    entry,
            const std::shared_ptr<boost::asio::ip::tcp::socket>&    socket,
            const std::shared_ptr<ITransmission>&                   transmission,
            int                                                     id) noexcept
            : Id(id)
            , entry_(entry)
            , transmission_(transmission)
            , status_(CONNECTION_STATUS_UNOPEN)
            , socket_(socket)
            , writing_(false) {
            const std::shared_ptr<Switches> switches = entry->GetSwitches();
            configuration_ = switches->GetConfiguration();
            const_cast<int&>(BufferSize) = configuration_->Alignment;
        }

        void Connection::Dispose() noexcept {
            Close();
        }

        void Connection::Close() noexcept {
            /* Sync connection status. */
            int status = status_.exchange(CONNECTION_STATUS_CLOSE);
            if (status == CONNECTION_STATUS_UNOPEN) { // 未打开连接状态
                return;
            }

            /* Release manage resources. */
            ClearTimeout();
            messages_.clear();
            frp::net::Socket::Closesocket(socket_);

            /* Wait frp client closesocket async. */
            if (status != CONNECTION_STATUS_CLOSE) { 
                const std::shared_ptr<Reference> reference = GetReference();
                SendCommandToFrpClientAsync(frp::messages::PacketCommands::PacketCommands_Disconnect, 
                    [reference, this](bool success) noexcept {
                        entry_->ReleaseConnection(transmission_.get(), Id);
                        if (!success) { 
                            entry_->CloseTransmission(transmission_);
                        }
                    });
            }
        }
          
        bool Connection::Then(bool success) noexcept {
            if (!success) {
                Close();
                entry_->CloseTransmission(transmission_);
            }
            return success;
        }

        void Connection::ClearTimeout() noexcept {
            frp::threading::ClearTimeout(timeout_);
        }

        bool Connection::ConnectToFrpClientAsync(const boost::asio::ip::tcp::endpoint& remoteEP) noexcept {
            typedef frp::net::IPEndPoint IPEndPoint;

            int location = CONNECTION_STATUS_UNOPEN;
            if (!status_.compare_exchange_weak(location, CONNECTION_STATUS_OPENING)) { // 当前连接的状态被切换
                return false;
            }

            frp::messages::NetworkAddressV4 addressV4;
            frp::messages::NetworkAddressV6 addressV6;

            frp::messages::Packet packet;
            packet.Command = frp::messages::PacketCommands::PacketCommands_Connect;
            packet.Id = Id;
            packet.Offset = 0;
            frp::messages::NetworkAddress::ToAddress(packet, addressV4, addressV6,
                IPEndPoint::ToEndPoint<boost::asio::ip::udp>(IPEndPoint::V6ToV4(IPEndPoint::ToEndPoint(remoteEP))));

            const std::shared_ptr<Reference> reference = GetReference();
            timeout_ = frp::threading::Hosting::Timeout(transmission_->GetContext(),
                [reference, this]() noexcept {
                    Close();
                }, (UInt64)configuration_->Connect.Timeout * 1000);

            return SendToFrpClientAsync(packet,
                [reference, this](bool success) noexcept {
                    Then(success);
                });
        }

        bool Connection::ConnectToFrpClientOnOK() noexcept {
            ClearTimeout();

            int location = CONNECTION_STATUS_OPENING;
            if (!status_.compare_exchange_weak(location, CONNECTION_STATUS_OPEN_OK)) {
                return false;
            }

            buffer_ = make_shared_alloc<Byte>(BufferSize);
            if (!buffer_) {
                return false;
            }

            return ForwardedToFrpClientAsync();
        }

        bool Connection::ForwardedToFrpClientAsync() noexcept {
            int status = status_;
            if (status != CONNECTION_STATUS_OPEN_OK) {
                return false;
            }

            bool connectOK = socket_->is_open();
            if (!connectOK) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            socket_->async_read_some(boost::asio::buffer(buffer_.get(), BufferSize),
                [reference, this](const boost::system::error_code& ec, std::size_t sz) noexcept {
                    bool success = false;
                    int length = std::max<int>(-1, ec ? -1 : sz);
                    if (length > 0) {
                        success = SendToFrpClientAsync(buffer_.get(), 0, length,
                            [reference, this](bool success) noexcept {
                                if (Then(success)) {
                                    ForwardedToFrpClientAsync();
                                }
                            });
                    }

                    if (!success) {
                        Close();
                    }
                });
            return true;
        }

        bool Connection::SendCommandToFrpClientAsync(int command, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
            int status = status_;
            if (status == CONNECTION_STATUS_UNOPEN) {
                return false;
            }

            frp::messages::Packet packet;
            packet.Command = (frp::messages::PacketCommands)command;
            packet.Id = Id;
            packet.Offset = 0;
            packet.Length = 0;

            return SendToFrpClientAsync(packet, forward0f(callback));
        }

        bool Connection::OnAsyncWrite(bool internall) noexcept {
            if (!internall) {
                if (writing_.exchange(true)) { // 正在队列写数据且不是内部调用则返回真
                    return true;
                }
            }

            const message_queue::iterator tail = messages_.begin();
            const message_queue::iterator endl = messages_.end();
            if (tail == endl) { // 当前消息队列是空得
                writing_.exchange(false);
                return false;
            }

            const std::shared_ptr<Reference> reference = GetReference();
            const pmessage message = std::move(*tail);

            messages_.erase(tail); // 从消息队列中删除这条消息
            boost::asio::async_write(*socket_, boost::asio::buffer(message->packet.get(), message->packet_size),
                [reference, this, message](const boost::system::error_code& ec, size_t sz) noexcept {
                    if (ec) {
                        Close();
                    }
                    OnAsyncWrite(true);
                });
            return true;
        }

        bool Connection::SendToPulicNeworkUserClient(const void* buffer, int offset, int length) noexcept {
            if (!buffer || offset < 0 || length < 1) {
                return false;
            }

            std::shared_ptr<Byte> messages = make_shared_alloc<Byte>(length);
            memcpy(messages.get(), ((Byte*)buffer) + offset, length);

            pmessage message_ = make_shared_object<message>();
            message_->packet = messages;
            message_->packet_size = length;

            messages_.push_back(message_);
            return OnAsyncWrite(false);
        }

        bool Connection::SendToFrpClientAsync(const void* buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
            if (!buffer || offset < 0 || length < 1) {
                return false;
            }

            int status = status_;
            if (status == CONNECTION_STATUS_UNOPEN) {
                return false;
            }

            frp::messages::Packet packet;
            packet.Command = frp::messages::PacketCommands::PacketCommands_Write;
            packet.Id = Id;
            packet.Length = length;
            packet.Offset = offset;
            packet.Buffer = std::shared_ptr<Byte>((Byte*)buffer, [](const void*) noexcept {});

            return SendToFrpClientAsync(packet, forward0f(callback));
        }

        bool Connection::SendToFrpClientAsync(const frp::messages::Packet& packet, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
            int status = status_;
            if (status == CONNECTION_STATUS_UNOPEN) {
                return false;
            }

            int messages_size;
            std::shared_ptr<Byte> message_ = constantof(packet).Serialize(messages_size);
            if (!message_ || messages_size < 1) {
                return false;
            }

            return Then(transmission_->WriteAsync(message_, 0, messages_size, forward0f(callback)));
        }
    }
}