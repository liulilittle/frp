#include <frp/server/MappingEntry.h>
#include <frp/server/Switches.h>
#include <frp/server/Connection.h>
#include <frp/net/Socket.h>
#include <frp/net/IPEndPoint.h>
#include <frp/io/MemoryStream.h>
#include <frp/messages/Packet.h>
#include <frp/messages/NetworkAddress.h>
#include <frp/transmission/ITransmission.h>

namespace frp {
    namespace server {
        MappingEntry::MappingEntry(const std::shared_ptr<Switches>& switches, const std::string& name, frp::configuration::MappingType type, int port) noexcept
            : Name(name)
            , Type(type)
            , Port(port)
            , disposed_(false)
            , switches_(switches)
            , hosting_(switches->GetHosting())
            , context_(switches->GetContext())
            , buffer_(hosting_->GetBuffer())
            , socket_(*context_)
            , acceptor_(*context_) {

        }

        void MappingEntry::Close() noexcept {
            Dispose();
        }

        void MappingEntry::Dispose() noexcept {
            if (!disposed_.exchange(true)) {
                ConnectionManager::ReleaseAllConnection();
                TransmissionManager::ReleaseAllTransmission();

                frp::net::Socket::Closesocket(acceptor_);
                frp::net::Socket::Closesocket(socket_);

                switches_->CloseEntry(Type, Port);
            }
        }

        bool MappingEntry::Open() noexcept {
            const std::shared_ptr<Reference> reference = GetReference();
            const std::shared_ptr<frp::configuration::AppConfiguration>& configuration = switches_->GetConfiguration();

            boost::system::error_code ec;
            boost::asio::ip::address address = boost::asio::ip::address::from_string(configuration->IP, ec);
            if (ec) {
                address = boost::asio::ip::address_v6::any();
            }

            if (Type == frp::configuration::MappingType::MappingType_TCP) {
                std::shared_ptr<frp::configuration::AppConfiguration> configuration = switches_->GetConfiguration();
                if (!frp::net::Socket::OpenAcceptor(acceptor_,
                    address,
                    Port,
                    configuration->Backlog,
                    configuration->FastOpen,
                    configuration->Turbo.Lan)) {
                    return false;
                }

                int localPort = frp::net::Socket::LocalPort(acceptor_);
                if (localPort != Port) {
                    return false;
                }

                if (localPort <= frp::net::IPEndPoint::MinPort || localPort > frp::net::IPEndPoint::MaxPort) {
                    return false;
                }

                return frp::net::Socket::AcceptLoopbackAsync(hosting_, acceptor_,
                    [reference, this](const std::shared_ptr<boost::asio::io_context>& context, const frp::net::Socket::AsioTcpSocket& socket) noexcept {
                        const std::shared_ptr<frp::configuration::AppConfiguration>& configuration = switches_->GetConfiguration();
                        frp::net::Socket::AdjustSocketOptional(*socket, configuration->FastOpen, configuration->Turbo.Wan);
                        return AcceptConnection(socket);
                    });
            }
            elif(Type == frp::configuration::MappingType::MappingType_UDP) {
                if (!frp::net::Socket::OpenSocket(socket_, address, Port)) {
                    return false;
                }

                int localPort = frp::net::Socket::LocalPort(socket_);
                if (localPort != Port) {
                    return false;
                }

                if (localPort <= frp::net::IPEndPoint::MinPort || localPort > frp::net::IPEndPoint::MaxPort) {
                    return false;
                }

                return ForwardedToFrpClientAsync();
            }
            else {
                return false;
            }
        }

        bool MappingEntry::ForwardedToFrpClientAsync() noexcept {
            bool available = socket_.is_open();
            if (!available) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            socket_.async_receive_from(boost::asio::buffer(buffer_.get(), frp::threading::Hosting::BufferSize), endpoint_,
                [reference, this](const boost::system::error_code& ec, std::size_t sz) noexcept {
                    if (ec == boost::system::errc::operation_canceled) {
                        Close();
                        return;
                    }

                    int length = std::max<int>(ec ? -1 : sz, -1);
                    if (length > 0 && SendToFrpClientAsync(buffer_.get(), length, endpoint_)) {
                        ForwardedToFrpClientAsync();
                    }
                });
            return true;
        }

        bool MappingEntry::SendToFrpClientAsync(const void* buffer, int length, const boost::asio::ip::udp::endpoint& endpoint) noexcept {
            frp::io::MemoryStream messages;
            if (!frp::messages::Packet::PackWriteTo(messages, buffer, 0, length, endpoint)) {
                return false;
            }

            const TransmissionPtr transmission = GetTransmission();
            if (!transmission) {
                return false;
            }

            const int packet_size = messages.GetPosition();
            const std::shared_ptr<Byte> packet = messages.GetBuffer();

            const std::shared_ptr<Reference> reference = GetReference();
            return Then(transmission,
                transmission->WriteAsync(packet, 0, packet_size,
                    [reference, this, transmission](bool success) noexcept {
                        Then(transmission, success);
                    }));
        }

        bool MappingEntry::Then(const TransmissionPtr& transmission, bool success) noexcept {
            if (!success) {
                CloseTransmission(transmission);
            }
            return success;
        }

        bool MappingEntry::OnPacketInput(const TransmissionPtr& transmission, const std::shared_ptr<frp::messages::Packet>& packet) noexcept {
            frp::messages::PacketCommands command = packet->Command;
            switch (command)
            {
            case frp::messages::PacketCommands_ConnectOK:
                OnHandleConnectOK(transmission, packet->Id);
                break;
            case frp::messages::PacketCommands_Disconnect:
                OnHandleDisconnect(transmission, packet->Id);
                break;
            case frp::messages::PacketCommands_Write:
                OnHandleWrite(transmission, *packet);
                break;
            case frp::messages::PacketCommands_WriteTo:
                OnHandleWriteTo(transmission, *packet);
                break;
            case frp::messages::PacketCommands_Heartbeat:
                OnHandleHeartbeat(transmission);
                break;
            default:
                return false;
            }
            return true;
        }

        bool MappingEntry::OnHandleHeartbeat(const TransmissionPtr& transmission) noexcept { /* Keep-Alives. */
            frp::messages::Packet packet;
            packet.Command = frp::messages::PacketCommands::PacketCommands_Heartbeat;
            packet.Id = 0;
            packet.Offset = 0;
            packet.Length = 0;

            int messages_size;
            std::shared_ptr<Byte> message_ = packet.Serialize(messages_size);
            if (!message_ || messages_size < 1) {
                return false;
            }

            const TransmissionPtr transmission_ = transmission;
            const std::shared_ptr<Reference> reference_ = GetReference();

            return Then(transmission_, transmission_->WriteAsync(message_, 0, messages_size,
                [transmission_, reference_, this](bool success) noexcept {
                    Then(transmission_, success);
                }));
        }

        bool MappingEntry::OnHandleConnectOK(const TransmissionPtr& transmission, int id) noexcept {
            ConnectionPtr connection = GetConnection(transmission.get(), id);
            if (!connection) {
                return false;
            }

            bool connectOK = connection->ConnectToFrpClientOnOK();
            if (!connectOK) {
                connection->Close();
            }
            return connectOK;
        }

        bool MappingEntry::OnHandleDisconnect(const TransmissionPtr& transmission, int id) noexcept {
            return ReleaseConnection(transmission.get(), id);
        }

        bool MappingEntry::OnHandleWrite(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept {
            ConnectionPtr connection = GetConnection(transmission.get(), packet.Id);
            if (!connection) {
                return false;
            }

            bool success = connection->SendToPulicNeworkUserClient(packet.Buffer.get(), packet.Offset, packet.Length);
            if (success) {
                return true;
            }
            return ReleaseConnection(transmission.get(), packet.Id);
        }

        bool MappingEntry::OnHandleWriteTo(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept {
            boost::asio::ip::udp::endpoint destinationEP;
            if (!frp::messages::Packet::UnpackWriteTo(packet, destinationEP)) {
                return false;
            }

            boost::system::error_code ec;
            socket_.send_to(boost::asio::buffer(packet.Buffer.get() + packet.Offset, packet.Length), destinationEP, 0, ec);
            return ec ? false : true;
        }

        bool MappingEntry::PacketInputAsync(const TransmissionPtr& transmission) noexcept {
            const std::shared_ptr<Reference> reference_ = GetReference();
            const TransmissionPtr transmission_ = transmission;

            return transmission_->ReadAsync(
                [reference_, this, transmission_](const std::shared_ptr<Byte>& buffer, int length) noexcept {
                    bool success = false;
                    std::shared_ptr<frp::messages::Packet> packet = frp::messages::Packet::Deserialize(buffer, 0, length);
                    if (packet) {
                        success = OnPacketInput(transmission_, packet) && PacketInputAsync(transmission_);
                    }
                    Then(transmission_, success);
                });
        }

        bool MappingEntry::AcceptConnection(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            const bool available = socket->is_open();
            if (!available) {
                return false;
            }

            boost::system::error_code ec;
            boost::asio::ip::tcp::endpoint remoteEP = socket->remote_endpoint(ec);
            if (ec) {
                return false;
            }

            const int remotePort = remoteEP.port();
            if (remotePort <= frp::net::IPEndPoint::MinPort || remotePort > frp::net::IPEndPoint::MaxPort) {
                return false;
            }

            const boost::asio::ip::address address = remoteEP.address();
            if (address.is_unspecified() || address.is_multicast()) {
                return false;
            }

            const TransmissionPtr transmission = GetBestTransmission();
            const ConnectionPtr connection = NewReference<Connection>(CastReference<MappingEntry>(GetReference()), socket, transmission, NewConnectionId());
            if (!connection) {
                return false;
            }

            const bool success = connection->ConnectToFrpClientAsync(remoteEP) && AddConnection(transmission.get(), connection->Id, connection);
            if (!success) {
                connection->Close();
            }
            return success;
        }

#ifdef MAPPINGENTRY_LOGF
#error "Not allowed to define macros: MAPPINGENTRY_LOGF."
#else
#define MAPPINGENTRY_LOGF(CATEGORY) \
        LOG_INFO(CATEGORY ": name %s, type %s, bind %s from ip %s.", \
            Name.data(), \
            Type == MappingType::MappingType_TCP ? "tcp" : "udp", \
            frp::net::IPEndPoint(configuration->IP.data(), Port).ToString().data(), \
            transmission->GetRemoteEndPoint().ToString().data());

        int MappingEntry::AddTransmission(const TransmissionPtr& transmission) noexcept {
            if (transmission) {
                if (PacketInputAsync(transmission) && TransmissionManager::AddTransmission(transmission)) {
                    const std::shared_ptr<frp::configuration::AppConfiguration>& configuration = switches_->GetConfiguration();
                    if (TransmissionManager::GetTransmissionCount() > 1) {
                        MAPPINGENTRY_LOGF("Accept mapping");
                    }
                    else {
                        MAPPINGENTRY_LOGF("Create mapping");
                    }
                }
            }
            return TransmissionManager::GetTransmissionCount();
        }

        void MappingEntry::CloseTransmission(const TransmissionPtr& transmission) noexcept {
            if (TransmissionManager::CloseTransmission(transmission.get())) {
                const std::shared_ptr<frp::configuration::AppConfiguration>& configuration = switches_->GetConfiguration();
                if (TransmissionManager::GetTransmissionCount()) {
                    MAPPINGENTRY_LOGF("Disconnect mapping");
                }
                else {
                    MAPPINGENTRY_LOGF("Close mapping");
                }
            }

            if (TransmissionManager::GetTransmissionCount() < 1) {
                Close();
            }
        }
#undef MAPPINGENTRY_LOGF
#endif
    }
}