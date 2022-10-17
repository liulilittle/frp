#include <frp/client/Router.h>
#include <frp/threading/Hosting.h>
#include <frp/threading/Timer.h>
#include <frp/io/MemoryStream.h>
#include <frp/net/Socket.h>
#include <frp/net/IPEndPoint.h>
#include <frp/collections/Dictionary.h>
#include <frp/messages/Packet.h>
#include <frp/messages/NetworkAddress.h>
#include <frp/messages/HandshakeRequest.h>
#include <frp/transmission/Transmission.h>
#include <frp/transmission/EncryptorTransmission.h>
#include <frp/transmission/SslSocketTransmission.h>
#include <frp/transmission/WebSocketTransmission.h>
#include <frp/transmission/SslWebSocketTransmission.h>

using frp::collections::Dictionary;
using frp::net::AddressFamily;
using frp::net::IPEndPoint;

namespace frp {
    namespace client {
        Router::Router(const std::shared_ptr<frp::threading::Hosting>& hosting, const std::shared_ptr<AppConfiguration>& configuration) noexcept
            : disposed_(false)
            , configuration_(configuration)
            , hosting_(hosting) {
            
        }

        void Router::Close() noexcept {
            if (!disposed_.exchange(true)) {
                Dictionary::ReleaseAllPairs(mappings_);
            }
        }

        void Router::Dispose() noexcept {
            Close();
        }

        bool Router::Open() noexcept {
            typedef frp::configuration::MappingConfigurationArrayList MappingConfigurationArrayList;

            /* Failed when the object was marked as released or a local mapping was created dynamically. */
            if (disposed_ || mappings_.size()) {
                return false;
            }

            /* Gets a reference to the dynamic mapping configuration Arraylist. */
            MappingConfigurationArrayList& mappings = configuration_->Mappings;

            /* Zero local dynamic mappings are not allowed to be created. */
            std::size_t mapping_length = mappings.size();
            if (!mapping_length) {
                return false;
            }

            /* Attempts to create a local dynamic mapping and returns immediately if it fails. */
            std::shared_ptr<Router> router = CastReference<Router>(GetReference());
            for (std::size_t index = 0; index < mapping_length; index++) {
                const std::shared_ptr<MappingEntry> mapping = make_shared_object<MappingEntry>(router, mappings[index]);
                if (!mapping) {
                    return false;
                }

                bool success = mapping->Open() && Dictionary::TryAdd(mappings_, mapping.get(), mapping);
                if (!success) {
                    return false;
                }
            }
            return mappings_.size() == mapping_length;
        }

        Router::MappingEntry::MappingEntry(const std::shared_ptr<Router>& router, const MappingConfiguration& mapping) noexcept
            : disposed_(false)
            , router_(router)
            , mapping_(mapping) {
            hosting_ = router->GetHosting();
            configuration_ = router->GetConfiguration();
        }

        bool Router::MappingEntry::Open() noexcept {
            typedef AppConfiguration::ProtocolType ProtocolType;

            if (mapping_.Type < MappingType::MappingType_None || mapping_.Type >= MappingType::MappingType_MaxType) {
                return false;
            }

            int concurrent = mapping_.Concurrent;
            if (concurrent < 1) {
                return false;
            }

            std::shared_ptr<frp::threading::Hosting> hosting = router_->GetHosting();
            if (!hosting) {
                return false;
            }

            std::shared_ptr<AppConfiguration> configuration = router_->GetConfiguration();
            if (!configuration) {
                return false;
            }

            for (int i = 0; i < concurrent; i++) {
                if (!ConnectTransmission(configuration, hosting)) {
                    return false;
                }
            }

            return Timeout();
        }

        bool Router::MappingEntry::Timeout() noexcept {
            if (disposed_) {
                return false;
            }

            std::shared_ptr<frp::threading::Hosting> hosting = router_->GetHosting();
            if (!hosting) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            timeout_ = frp::threading::SetTimeout(hosting,
                [reference, this](void*) noexcept {
                    TransmissionManager::WhileAllTransmission(
                        [this](TransmissionPtr& transmission) noexcept {
                            SendKeepAlivePacket(transmission);
                        });
                    Timeout();
                }, 30000);
            return NULL != timeout_;
        }

        void Router::MappingEntry::Close() noexcept {
            if (!disposed_.exchange(true)) {
                ConnectionManager::ReleaseAllConnection();
                DatagramPortManager::ReleaseAllDatagramPort();
                TransmissionManager::ReleaseAllTransmission();
                RestartTasksManger::CloseAllRestartTasks();
            }
            frp::threading::ClearTimeout(timeout_);
        }

        void Router::MappingEntry::Dispose() noexcept {
            Close();
        }

        bool Router::MappingEntry::Then(const TransmissionPtr& transmission, bool success) noexcept {
            if (!success) {
                CloseTransmission(transmission);
            }
            return success;
        }

#ifdef MAPPINGENTRY_LOGF
#error "Not allowed to define macros: MAPPINGENTRY_LOGF."
#else
#define MAPPINGENTRY_LOGF(CATEGORY) \
        LOG_INFO(CATEGORY ": name %s, type %s, port %d to server %s.", \
            mapping_.Name.data(), \
            mapping_.Type == MappingType::MappingType_TCP ? "tcp" : "udp", \
            mapping_.RemotePort, \
            frp::net::IPEndPoint(configuration_->IP.data(), configuration_->Port).ToString().data());

        bool Router::MappingEntry::AddTransmission(const TransmissionPtr& transmission) noexcept {
            if (transmission) {
                if (PacketInputAsync(transmission) && TransmissionManager::AddTransmission(transmission)) {
                    MAPPINGENTRY_LOGF("Connect mapping");
                    return true;
                }
            }
            return false;
        }

        bool Router::MappingEntry::CloseTransmission(const TransmissionPtr& transmission) noexcept {
            if (TransmissionManager::CloseTransmission(transmission.get())) {
                if (RestartTransmission()) {
                    MAPPINGENTRY_LOGF("Disconnect mapping");
                    return true;
                }
            }
            return false;
        }
#undef MAPPINGENTRY_LOGF
#endif

        bool Router::MappingEntry::ConnectTransmission(const std::shared_ptr<AppConfiguration>& configuration, const std::shared_ptr<frp::threading::Hosting>& hosting) noexcept {
            typedef AppConfiguration::ProtocolType ProtocolType;

            std::shared_ptr<boost::asio::io_context> context = hosting->GetContext();
            if (disposed_ || !context) {
                return false;
            }

            std::shared_ptr<boost::asio::ip::tcp::socket> socket = make_shared_object<boost::asio::ip::tcp::socket>(*context);
            if (!socket) {
                return false;
            }

            IPEndPoint remoteEP(configuration->IP.data(), configuration->Port);
            if (IPEndPoint::IsInvalid(remoteEP)) {
                return false;
            }

            if (remoteEP.Port <= IPEndPoint::MinPort || remoteEP.Port > IPEndPoint::MaxPort) {
                return false;
            }

            boost::system::error_code ec;
            if (remoteEP.GetAddressFamily() == AddressFamily::InterNetwork) {
                socket->open(boost::asio::ip::tcp::v4(), ec);
            }
            else {
                socket->open(boost::asio::ip::tcp::v6(), ec);
            }

            if (ec) {
                return false;
            }
            else {
                frp::net::Socket::AdjustSocketOptional(*socket, configuration->FastOpen, configuration->Turbo.Wan);
            }

            std::shared_ptr<Reference> reference = GetReference();
            socket->async_connect(IPEndPoint::ToEndPoint<boost::asio::ip::tcp>(remoteEP),
                [reference, this, context, socket](const boost::system::error_code& ec) noexcept {
                    bool success = ec ? false : true;
                    if (success) {
                        success = AcceptTransmission(context, socket);
                    }

                    if (!success) {
                        RestartTransmission();
                        frp::net::Socket::Closesocket(socket);
                    }
                });
            return true;
        }

        bool Router::MappingEntry::RestartTransmission() noexcept {
            const std::shared_ptr<Reference> reference = GetReference();
            return !disposed_ && AddRestartTask(reference, hosting_,
                [reference, this]() noexcept {
                    if (!disposed_) {
                        if (!ConnectTransmission(configuration_, hosting_)) {
                            RestartTransmission();
                        }
                    }
                }, (UInt64)mapping_.Reconnect * 1000);
        }

        bool Router::MappingEntry::AcceptTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            if (!context) {
                return false;
            }

            const TransmissionPtr transmission = router_->CreateTransmission(context, socket);
            if (!transmission) {
                return false;
            }

            const std::shared_ptr<Reference> reference = GetReference();
            return transmission->HandshakeAsync(frp::transmission::ITransmission::HandshakeType_Client, /* In order to extend the transport layer medium. */
                [reference, this, transmission](bool handshaked) noexcept {
                    handshaked = handshaked && HandshakeTransmission(transmission);
                    if (!handshaked) { /* This fails. You need to manually invoke the reconstruction of the transport layer; otherwise, the link will be completely lost. */
                        transmission->Close();
                        RestartTransmission();
                    }
                });
        }

        bool Router::MappingEntry::HandshakeTransmission(const TransmissionPtr& transmission) {
            frp::messages::HandshakeRequest request;
            request.Name = mapping_.Name;
            request.Type = mapping_.Type;
            request.RemotePort = mapping_.RemotePort;

            int length;
            std::shared_ptr<Byte> packet = request.Serialize(length);
            if (!packet || length < 1) {
                return false;
            }

            const std::shared_ptr<Reference> sreference = GetReference();
            const TransmissionPtr stransmission = transmission;

            if (!stransmission->WriteAsync(packet, 0, length,
                [sreference, this, stransmission](bool success) noexcept {
                    Then(stransmission, success);
                })) {
                return false;
            }

            return AddTransmission(stransmission);
        }

        bool Router::MappingEntry::PacketInputAsync(const TransmissionPtr& transmission) noexcept {
            const TransmissionPtr stransmission = transmission;
            const std::shared_ptr<Reference> sreference = GetReference();
            return stransmission->ReadAsync(
                [sreference, this, stransmission](const std::shared_ptr<Byte>& buffer, int length) noexcept {
                    bool success = false;
                    if (length > 0) {
                        std::shared_ptr<frp::messages::Packet> packet = frp::messages::Packet::Deserialize(buffer, 0, length);
                        if (packet) {
                            success = OnPacketInput(stransmission, packet) && PacketInputAsync(stransmission);
                        }
                    }
                    Then(stransmission, success);
                });
        }

        bool Router::MappingEntry::OnPacketInput(const TransmissionPtr& transmission, const std::shared_ptr<frp::messages::Packet>& packet) noexcept {
            frp::messages::PacketCommands command = packet->Command;
            switch (command)
            {
            case frp::messages::PacketCommands_Connect:
                OnHandleConnect(transmission, *packet);
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
                break;
            default:
                return false;
            }
            return true;
        }

        bool Router::MappingEntry::OnHandleConnect(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept {
            boost::asio::ip::tcp::endpoint remoteEP;
            if (!frp::messages::Packet::UnpackWriteTo(packet, remoteEP)) {
                return false;
            }

            const std::shared_ptr<MappingEntry> entry = CastReference<MappingEntry>(GetReference());
            const ConnectionPtr connection = NewReference<Connection>(entry, transmission, packet.Id, remoteEP);
            if (!connection) {
                return false;
            }

            const bool success = connection->AcceptFrpConnectionAsync() && AddConnection(transmission.get(), connection->Id, connection);
            if (!success) {
                connection->Close();
            }
            return success;
        }

        bool Router::MappingEntry::OnHandleDisconnect(const TransmissionPtr& transmission, int id) noexcept {
            return ReleaseConnection(transmission.get(), id);
        }

        bool Router::MappingEntry::OnHandleWrite(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept {
            ConnectionPtr connection = GetConnection(transmission.get(), packet.Id);
            if (!connection) {
                return false;
            }

            bool success = connection->SendToLocalClientAsync(packet.Buffer.get(), packet.Offset, packet.Length);
            if (success) {
                return true;
            }
            return ReleaseConnection(transmission.get(), packet.Id);
        }

        bool Router::MappingEntry::OnHandleWriteTo(const TransmissionPtr& transmission, frp::messages::Packet& packet) noexcept {
            boost::asio::ip::udp::endpoint remoteEP;
            if (!frp::messages::Packet::UnpackWriteTo(packet, remoteEP)) {
                return false;
            }

            DatagramPortPtr datagramPort = AllocDatagramPort(remoteEP,
                [this](const boost::asio::ip::udp::endpoint& remoteEP) noexcept {
                    return NewReference<DatagramPort>(CastReference<MappingEntry>(GetReference()), remoteEP);
                });
            if (!datagramPort) {
                return false;
            }

            return datagramPort->SendToLocalClientAsync(packet.Buffer.get(), packet.Offset, packet.Length);
        }

        bool Router::MappingEntry::SendKeepAlivePacket(const TransmissionPtr& transmission) noexcept {
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

            const TransmissionPtr stransmission = transmission;
            const std::shared_ptr<Reference> sreference = GetReference();

            return Then(stransmission, stransmission->WriteAsync(message_, 0, messages_size,
                [stransmission, sreference, this](bool success) noexcept {
                    Then(stransmission, success);
                }));
        }

        Router::TransmissionPtr Router::CreateTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            if (configuration_->Protocol == AppConfiguration::ProtocolType_SSL ||
                configuration_->Protocol == AppConfiguration::ProtocolType_TLS) {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::SslSocketTransmission>(hosting_, context, socket,
                    configuration_->Protocols.Ssl.VerifyPeer,
                    configuration_->Protocols.Ssl.Host,
                    configuration_->Protocols.Ssl.Ciphersuites);
            }
            elif(configuration_->Protocol == AppConfiguration::ProtocolType_Encryptor) {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::EncryptorTransmission>(hosting_, context, socket,
                    configuration_->Protocols.Encryptor.Method,
                    configuration_->Protocols.Encryptor.Password);
            }
            elif(configuration_->Protocol == AppConfiguration::ProtocolType_WebSocket) {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::WebSocketTransmission>(hosting_, context, socket,
                    configuration_->Protocols.WebSocket.Host,
                    configuration_->Protocols.WebSocket.Path);
            }
            elif(configuration_->Protocol == AppConfiguration::ProtocolType_WebSocket_SSL ||
                configuration_->Protocol == AppConfiguration::ProtocolType_WebSocket_TLS) {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::SslWebSocketTransmission>(hosting_, context, socket,
                    configuration_->Protocols.Ssl.VerifyPeer,
                    configuration_->Protocols.WebSocket.Host,
                    configuration_->Protocols.WebSocket.Path,
                    configuration_->Protocols.Ssl.Ciphersuites);
            }
            else {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::Transmission>(hosting_, context, socket);
            }
        }

        Router::Connection::Connection(
            const std::shared_ptr<MappingEntry>&  entry,
            const TransmissionPtr&                transmission,
            int                                   id,
            const boost::asio::ip::tcp::endpoint& remoteEP) noexcept
            : Id(id)
            , status_(CONNECTION_STATUS_UNOPEN)
            , entry_(entry)
            , hosting_(entry->GetHosting())
            , configuration_(entry->GetConfiguration())
            , transmission_(transmission)
            , context_(transmission->GetContext())
            , socket_(*context_)
            , remoteEP_(remoteEP)
            , writing_(false) {
            const_cast<int&>(BufferSize) = configuration_->Alignment;
        }

        bool Router::Connection::AcceptFrpConnectionAsync() noexcept {
            if (socket_.is_open()) {
                return false;
            }

            const MappingConfiguration& mapping = entry_->GetMappingConfiguration();
            const IPEndPoint remoteEP(mapping.LocalIP.data(), mapping.LocalPort);
            if (IPEndPoint::IsInvalid(remoteEP)) {
                return false;
            }

            int location = CONNECTION_STATUS_UNOPEN;
            if (!status_.compare_exchange_weak(location, CONNECTION_STATUS_OPENING)) { // 当前连接的状态被切换
                return false;
            }

            boost::system::error_code ec;
            if (remoteEP.GetAddressFamily() == AddressFamily::InterNetwork) {
                socket_.open(boost::asio::ip::tcp::v4(), ec);
            }
            else {
                socket_.open(boost::asio::ip::tcp::v6(), ec);
            }

            if (ec) {
                return false;
            }
            else {
                frp::net::Socket::AdjustSocketOptional(socket_, configuration_->FastOpen, configuration_->Turbo.Wan);
            }

            const std::shared_ptr<Reference> reference = GetReference();
            timeout_ = frp::threading::SetTimeout(hosting_,
                [reference, this](void*) noexcept {
                    Close();
                }, (UInt64)configuration_->Connect.Timeout * 1000);

            socket_.async_connect(IPEndPoint::ToEndPoint<boost::asio::ip::tcp>(remoteEP),
                [reference, this](const boost::system::error_code& ec) noexcept {
                    bool success = ec ? false : true;
                    if (success) {
                        success = ConnectToLocalClientOnOK();
                    }

                    if (!success) {
                        Close();
                    }
                });
            return true;
        }

        bool Router::Connection::ConnectToLocalClientOnOK() noexcept {
            ClearTimeout();

            int location = CONNECTION_STATUS_OPENING;
            if (!status_.compare_exchange_weak(location, CONNECTION_STATUS_OPEN_OK)) {
                return false;
            }

            boost::system::error_code ec;
            localEP_ = socket_.local_endpoint(ec);
            if (ec) {
                return false;
            }

            buffer_ = make_shared_alloc<Byte>(BufferSize);
            if (!buffer_) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            if (!SendCommandToFrpServerAsync(frp::messages::PacketCommands::PacketCommands_ConnectOK,
                [reference, this](bool success) noexcept {
                    Then(success);
                })) {
                return false;
            }

            return ForwardedToFrpServerAsync();
        }

        bool Router::Connection::ForwardedToFrpServerAsync() noexcept {
            int status = status_;
            if (status != CONNECTION_STATUS_OPEN_OK) {
                return false;
            }

            bool connectOK = socket_.is_open();
            if (!connectOK) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            socket_.async_read_some(boost::asio::buffer(buffer_.get(), BufferSize),
                [reference, this](const boost::system::error_code& ec, std::size_t sz) noexcept {
                    int length = std::max<int>(-1, ec ? -1 : sz);
                    if (length < 1) {
                        Close();
                    }
                    else {
                        SendToFrpServerAsync(buffer_.get(), 0, length,
                            [reference, this](bool success) noexcept {
                                if (Then(success)) {
                                    ForwardedToFrpServerAsync();
                                }
                            });
                    }
                });
            return true;
        }

        bool Router::Connection::OnAsyncWrite(bool internall) noexcept {
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
            boost::asio::async_write(socket_, boost::asio::buffer(message->packet.get(), message->packet_size),
                [reference, this, message](const boost::system::error_code& ec, size_t sz) noexcept {
                    if (ec) {
                        Close();
                    }
                    OnAsyncWrite(true);
                });
            return true;
        }

        bool Router::Connection::SendToLocalClientAsync(const void* buffer, int offset, int length) noexcept {
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

        void Router::Connection::Close() noexcept {
            /* Sync connection status. */
            const int status = status_.exchange(CONNECTION_STATUS_CLOSE);
            if (status == CONNECTION_STATUS_UNOPEN) { // 未打开连接状态
                return;
            }

            /* Release manage resources. */
            ClearTimeout();
            messages_.clear();
            frp::net::Socket::Closesocket(socket_);

            /* Wait frp server closesocket async. */
            if (status != CONNECTION_STATUS_CLOSE) {
                const std::shared_ptr<Reference> reference = GetReference();
                SendCommandToFrpServerAsync(frp::messages::PacketCommands::PacketCommands_Disconnect,
                    [reference, this](bool) noexcept {
                        entry_->ReleaseConnection(transmission_.get(), Id);
                    });
            }
        }

        bool Router::Connection::Then(bool success) noexcept {
            if (!success) {
                Close();
                entry_->CloseTransmission(transmission_);
            }
            return success;
        }
        
        void Router::Connection::ClearTimeout() noexcept {
            std::shared_ptr<boost::asio::deadline_timer> timeout = std::move(timeout_);
            if (timeout) {
                frp::threading::Hosting::Cancel(timeout);
                timeout.reset();
                timeout_.reset();
            }
        }

        void Router::Connection::Dispose() noexcept {
            Close();
        }

        bool Router::Connection::SendCommandToFrpServerAsync(int command, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
            int status = status_;
            if (status == CONNECTION_STATUS_UNOPEN) {
                return false;
            }

            frp::messages::Packet packet;
            packet.Command = (frp::messages::PacketCommands)command;
            packet.Id = Id;
            packet.Offset = 0;
            packet.Length = 0;

            return SendToFrpServerAsync(packet, forward0f(callback));
        }

        bool Router::Connection::SendToFrpServerAsync(const void* buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
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

            return SendToFrpServerAsync(packet, forward0f(callback));
        }

        bool Router::Connection::SendToFrpServerAsync(const frp::messages::Packet& packet, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept {
            const int status = status_;
            if (status == CONNECTION_STATUS_UNOPEN) {
                return false;
            }

            int messages_size;
            const std::shared_ptr<Byte> message_ = constantof(packet).Serialize(messages_size);
            if (!message_ || messages_size < 1) {
                return false;
            }

            const std::shared_ptr<Reference> reference = GetReference();
            const WriteAsyncCallback scallback = BOOST_ASIO_MOVE_CAST(WriteAsyncCallback)(constantof(callback));
            return Then(transmission_->WriteAsync(message_, 0, messages_size,
                [reference, this, scallback](bool success) noexcept {
                    if (!success) {
                        entry_->CloseTransmission(transmission_);
                    }

                    if (scallback) {
                        scallback(success);
                    }
                }));
        }
    
        Router::DatagramPort::DatagramPort(const std::shared_ptr<MappingEntry>& entry, boost::asio::ip::udp::endpoint remoteEP) noexcept
            : disposed_(false)
            , onlydns_(0)
            , entry_(entry)
            , hosting_(entry->GetHosting())
            , context_(hosting_->GetContext())
            , configuration_(entry->GetConfiguration())
            , buffer_(hosting_->GetBuffer())
            , socket_(*context_)
            , remoteEP_(remoteEP) {
            const MappingConfiguration& mapping = entry->GetMappingConfiguration();

            boost::system::error_code ec;
            boost::asio::ip::address address = boost::asio::ip::address::from_string(mapping.LocalIP, ec);
            if (ec) {
                address = boost::asio::ip::address_v6::any();
            }

            last_ = hosting_->CurrentMillisec();
            localEP_ = boost::asio::ip::udp::endpoint(address, mapping.LocalPort);
        }

        bool Router::DatagramPort::Open() noexcept {
            bool available = socket_.is_open();
            if (available) {
                return false;
            }

            boost::asio::ip::address address = localEP_.address();
            if (address.is_multicast() || address.is_unspecified()) {
                return false;
            }

            if (address.is_v4()) {
                if (!address.is_loopback()) {
                    address = boost::asio::ip::address_v4::any();
                }
            }
            elif(address.is_v6()) {
                if (!address.is_loopback()) {
                    address = boost::asio::ip::address_v6::any();
                }
            }
            else {
                return false;
            }

            if (!frp::net::Socket::OpenSocket(socket_, address, 0)) {
                return false;
            }

            return Timeout() && ForwardedToFrpServerAsync();
        }

        bool Router::DatagramPort::Then(const TransmissionPtr& transmission, bool success) noexcept {
            if (!success) {
                Close();
                entry_->CloseTransmission(transmission);
            }
            return success;
        }

        void Router::DatagramPort::Close() noexcept {
            if (!disposed_.exchange(true)) {
                frp::net::Socket::Closesocket(socket_);
                entry_->ReleaseDatagramPort(remoteEP_);
            }
            ClearTimeout();
        }

        void Router::DatagramPort::Dispose() noexcept {
            Close();
        }

        void Router::DatagramPort::ClearTimeout() noexcept {
            frp::threading::ClearTimeout(timeout_);
        }

        bool Router::DatagramPort::Timeout() noexcept {
            /* The current timeout timer is cleared first, and the object is not processed if it has been marked free. */
            ClearTimeout();
            if (disposed_) {
                return false;
            }

            /* Set the next asynchronous timeout to wait for processing. */
            std::shared_ptr<Reference> reference = GetReference();
            timeout_ = frp::threading::SetTimeout(hosting_,
                [reference, this](void*) noexcept {
                    UInt64 now = hosting_->CurrentMillisec();
                    UInt64 last = last_;
                    if (last > now) {
                        Close();
                        return;
                    }

                    UInt64 diff = now - last;
                    UInt64 timeout = onlydns_ == 1 ? 
                        DynamicNamespaceQueryTimeout : 
                        configuration_->Inactive.Timeout;

                    timeout *= 1000;
                    if (diff > timeout) {
                        Close();
                        return;
                    }
                    
                    Timeout();
                }, 1000);
            return NULL != timeout_;
        }

        bool Router::DatagramPort::SendToLocalClientAsync(const void* buffer, int offset, int length) noexcept {
            if (!buffer || offset < 0 || length < 1) {
                return false;
            }

            bool available = socket_.is_open();
            if (!available) {
                return false;
            }

            boost::system::error_code ec;
            socket_.send_to(boost::asio::buffer((Byte*)buffer + offset, length), localEP_, 0, ec);

            bool success = ec ? false : true;
            if (success) {
                const MappingConfiguration& mapping = entry_->GetMappingConfiguration();
                if (mapping.LocalPort == DynamicNamespaceQueryPort) {
                    int localtion = 0;
                    onlydns_.compare_exchange_weak(localtion, 1);
                }
                else {
                    onlydns_.exchange(2);
                }
                last_ = hosting_->CurrentMillisec();
            }
            return success;
        }

        bool Router::DatagramPort::ForwardedToFrpServerAsync() noexcept {
            bool available = socket_.is_open();
            if (!available) {
                return false;
            }

            std::shared_ptr<Reference> reference = GetReference();
            socket_.async_receive_from(boost::asio::buffer(buffer_.get(), frp::threading::Hosting::BufferSize), endpoint_,
                [reference, this](const boost::system::error_code& ec, std::size_t sz) noexcept {
                    bool success = true;
                    do {
                        /* Cancellation of the current asynchronous operation means that the dynamic port mapping has been marked free. */
                        if (ec == boost::system::errc::operation_canceled) {
                            success = false;
                            break;
                        }

                        /* No data was received or other socket error occurred. */
                        int length = std::max<int>(ec ? -1 : sz, -1);
                        if (length < 1) {
                            break;
                        }

                        /* An attempt to asynchronously send to the frp server failed. */
                        success = SendToFrpServerAsync(buffer_.get(), length);
                        if (!success) {
                            break;
                        }

                        /* An attempt to continue pulling up asynchronously waiting for forwarding to the frp server failed. */
                        success = ForwardedToFrpServerAsync();
                    } while (0); 

                    /* If the current traffic forwarding operation fails, you need to disable dynamic port mapping. */
                    if (!success) {
                        Close();
                    }
                });
            return true;
        }

        bool Router::DatagramPort::SendToFrpServerAsync(const void* buffer, int length) noexcept {
            const TransmissionPtr transmission = entry_->GetTransmission();
            if (!transmission) {
                return false;
            }

            frp::io::MemoryStream messages;
            if (!frp::messages::Packet::PackWriteTo(messages, buffer, 0, length, remoteEP_)) {
                return false;
            }

            const std::shared_ptr<Reference> reference = GetReference();
            return Then(transmission,
                transmission->WriteAsync(messages.GetBuffer(), 0, messages.GetPosition(),
                    [reference, this, transmission](bool success) noexcept {
                        if (Then(transmission, success)) {
                            last_ = hosting_->CurrentMillisec();
                        }
                    }));
        }
    }
}