#include <frp/server/Switches.h>
#include <frp/server/MappingEntry.h>
#include <frp/messages/HandshakeRequest.h>
#include <frp/net/Ipep.h>
#include <frp/net/Socket.h>
#include <frp/net/IPEndPoint.h>
#include <frp/threading/Timer.h>
#include <frp/threading/Hosting.h>
#include <frp/transmission/Transmission.h>
#include <frp/transmission/EncryptorTransmission.h>
#include <frp/transmission/SslSocketTransmission.h>
#include <frp/transmission/WebSocketTransmission.h>
#include <frp/transmission/SslWebSocketTransmission.h>

using frp::net::IPEndPoint;
using frp::net::Ipep;
using frp::collections::Dictionary;

namespace frp {
    namespace server {
        Switches::Switches(const std::shared_ptr<frp::threading::Hosting>& hosting, const std::shared_ptr<AppConfiguration>& configuration) noexcept
            : disposed_(false)
            , configuration_(configuration)
            , hosting_(hosting)
            , context_(hosting->GetContext())
            , acceptor_(*context_) {
            boost::system::error_code ec;
            int& port = configuration_->Port;
            if (frp::net::Socket::OpenAcceptor(acceptor_,
                boost::asio::ip::address::from_string(configuration->IP, ec),
                port,
                configuration_->Backlog,
                configuration_->FastOpen,
                configuration_->Turbo.Lan)) {
                port = frp::net::Socket::LocalPort(acceptor_);
            }
            else {
                port = IPEndPoint::MinPort;
            }
        }

        void Switches::Dispose() noexcept {
            if (!disposed_.exchange(true)) {
                /* Close the TCP socket acceptor function to prevent the system from continuously processing connections. */
                frp::net::Socket::Closesocket(acceptor_);

                /* Clear all timeouts. */
                Dictionary::ReleaseAllPairs(timeouts_,
                    [](TimeoutPtr& timeout) noexcept {
                        frp::threading::Hosting::Cancel(timeout);
                    });

                /* Close all mappings. */
                for (int i = AppConfiguration::LoopbackMode_None; i < AppConfiguration::LoopbackMode_MaxType; i++) {
                    Dictionary::ReleaseAllPairs(entiress_[i]);
                }
            }
        }

        bool Switches::Open() noexcept {
            typedef AppConfiguration::ProtocolType ProtocolType;

            /* Try to open the remote switch, but only if the object instance has not been marked free. */
            if (disposed_) {
                return false;
            }

            int port = configuration_->Port;
            if (port <= IPEndPoint::MinPort || port > IPEndPoint::MaxPort) {
                return false;
            }

            /* Open an frp/server based on the TCP/IP network layer protocol. */
            if (!acceptor_.is_open()) {
                return false;
            }

            const std::shared_ptr<Reference> reference = GetReference();
            return frp::net::Socket::AcceptLoopbackAsync(hosting_, acceptor_,
                [reference, this](const std::shared_ptr<boost::asio::io_context>& context, const frp::net::Socket::AsioTcpSocket& socket) noexcept {
                    frp::net::Socket::AdjustSocketOptional(*socket, configuration_->FastOpen, configuration_->Turbo.Wan);
                    return HandshakeAsync(context, socket);
                });
        }

        bool Switches::HandshakeAsync(const std::shared_ptr<frp::transmission::ITransmission>& transmission) noexcept {
            const std::shared_ptr<Reference> reference = GetReference();
            if (!AddTimeout(transmission.get(), frp::threading::SetTimeout(hosting_,
                [reference, this, transmission](void*) noexcept {
                    transmission->Close();
                    ClearTimeout(transmission.get());
                }, (UInt64)configuration_->Handshake.Timeout * 1000))) {
                return false;
            }

            return transmission->HandshakeAsync(frp::transmission::ITransmission::HandshakeType_Server, /* In order to extend the transport layer medium. */
                [reference, this, transmission](bool handshaked) noexcept {
                    handshaked = handshaked && transmission->ReadAsync(
                        [reference, this, transmission](const std::shared_ptr<Byte>& buffer, int length) noexcept {
                            bool success = false;
                            if (length > 0) {
                                std::shared_ptr<frp::messages::HandshakeRequest> request = frp::messages::HandshakeRequest::Deserialize(buffer.get(), length);
                                if (request) {
                                    success = AddEntry(transmission, request);
                                }
                            }

                            if (!success) {
                                transmission->Close();
                            }
                            ClearTimeout(transmission.get());
                        });

                    /* Transmission handshake failed. */
                    if (!handshaked) {
                        transmission->Close();
                        ClearTimeout(transmission.get());
                    }
                });
        }

        bool Switches::HandshakeAsync(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            const std::shared_ptr<frp::transmission::ITransmission> transmission = CreateTransmission(context, socket);
            if (!transmission) {
                return false;
            }
            return HandshakeAsync(transmission);
        }

        bool Switches::AddTimeout(void* key, std::shared_ptr<boost::asio::deadline_timer>&& timeout) noexcept {
            if (!key || !timeout) {
                return false;
            }

            bool success = Dictionary::TryAdd(timeouts_, key, timeout);
            if (!success) {
                frp::threading::ClearTimeout(timeout);
            }
            return success;
        }

        bool Switches::ClearTimeout(void* key) noexcept {
            if (!key) {
                return false;
            }

            TimeoutPtr timeout;
            Dictionary::TryRemove(timeouts_, key, timeout);
            return frp::threading::ClearTimeout(timeout);
        }

        std::shared_ptr<frp::transmission::ITransmission> Switches::CreateTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            if (configuration_->Protocol == AppConfiguration::ProtocolType_SSL ||
                configuration_->Protocol == AppConfiguration::ProtocolType_TLS) {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::SslSocketTransmission>(hosting_, context, socket,
                    configuration_->Protocols.Ssl.Host,
                    configuration_->Protocols.Ssl.CertificateFile,
                    configuration_->Protocols.Ssl.CertificateKeyFile,
                    configuration_->Protocols.Ssl.CertificateChainFile,
                    configuration_->Protocols.Ssl.CertificateKeyPassword,
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
                    configuration_->Protocols.WebSocket.Host,
                    configuration_->Protocols.WebSocket.Path,
                    configuration_->Protocols.Ssl.CertificateFile,
                    configuration_->Protocols.Ssl.CertificateKeyFile,
                    configuration_->Protocols.Ssl.CertificateChainFile,
                    configuration_->Protocols.Ssl.CertificateKeyPassword,
                    configuration_->Protocols.Ssl.Ciphersuites);
            }
            else {
                return NewReference2<frp::transmission::ITransmission, frp::transmission::Transmission>(hosting_, context, socket);
            }
        }

        bool Switches::CloseEntry(MappingType type, int port) noexcept {
            if ((int)type < MappingType::MappingType_None || (int)type >= MappingType::MappingType_MaxType) {
                return false;
            }

            std::shared_ptr<MappingEntry> entry;
            if (!Dictionary::TryRemove(entiress_[type], port, entry)) {
                return false;
            }

            entry->Close();
            return true;
        }

        bool Switches::AddEntry(const std::shared_ptr<frp::transmission::ITransmission>& transmission, const std::shared_ptr<frp::messages::HandshakeRequest>& request) noexcept {
            if ((int)request->Type < MappingType::MappingType_None || (int)request->Type >= MappingType::MappingType_MaxType) {
                return false;
            }

            if (request->RemotePort <= IPEndPoint::MinPort || request->RemotePort > IPEndPoint::MaxPort) {
                return false;
            }

            std::shared_ptr<Switches> switches = CastReference<Switches>(GetReference());
            if (!switches) {
                return false;
            }

            std::shared_ptr<frp::server::MappingEntry> entry;
            MappingEntryTable& entires = entiress_[(int)request->Type];
            if (!Dictionary::TryGetValue(entires, request->RemotePort, entry)) {
                entry = NewReference<frp::server::MappingEntry>(switches, request->Name, request->Type, request->RemotePort);
                if (!entry) {
                    return false;
                }

                if (!entry->Open() || !Dictionary::TryAdd(entires, request->RemotePort, entry)) {
                    entry->Close();
                    return false;
                }
            }

            int status = entry->AddTransmission(transmission);
            if (!status) {
                entry->Close();
                return false;
            }
            else {
                return status > 0;
            }
        }
    }
}