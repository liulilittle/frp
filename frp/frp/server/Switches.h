#pragma once

#include <frp/IDisposable.h>
#include <frp/threading/Hosting.h>
#include <frp/transmission/ITransmission.h>
#include <frp/configuration/AppConfiguration.h>

namespace frp {
    namespace net {
        class Socket;
    }

    namespace messages {
        class HandshakeRequest;
    }

    namespace server {
        class MappingEntry;

        class Switches : public IDisposable {
            friend class MappingEntry;

            typedef std::shared_ptr<MappingEntry>                           MappingEntryPtr;
            typedef std::unordered_map<int, MappingEntryPtr>                MappingEntryTable;
            typedef std::shared_ptr<boost::asio::deadline_timer>            TimeoutPtr;
            typedef std::unordered_map<void*, TimeoutPtr>                   TimeoutTable;

        public:
            typedef frp::configuration::MappingType                         MappingType;
            typedef frp::configuration::AppConfiguration                    AppConfiguration;
            typedef frp::configuration::MappingConfiguration                MappingConfiguration;

        public:
            Switches(const std::shared_ptr<frp::threading::Hosting>& hosting, const std::shared_ptr<AppConfiguration>& configuration) noexcept;

        public:
            inline const std::shared_ptr<AppConfiguration>&                 GetConfiguration() noexcept {
                return configuration_;
            }
            inline const std::shared_ptr<boost::asio::io_context>&          GetContext() noexcept {
                return context_;
            }
            inline const std::shared_ptr<frp::threading::Hosting>&          GetHosting() noexcept {
                return hosting_;
            }
            inline const boost::asio::ip::tcp::endpoint                     GetLocalEndPoint() noexcept {
                boost::system::error_code ec;
                return acceptor_.local_endpoint(ec);
            }
            bool                                                            Open() noexcept;
            void                                                            Close() noexcept;
            virtual void                                                    Dispose() noexcept override;

        protected:
            virtual bool                                                    HandshakeAsync(const std::shared_ptr<frp::transmission::ITransmission>& transmission) noexcept;
            virtual std::shared_ptr<frp::transmission::ITransmission>       CreateTransmission(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;
            virtual bool                                                    AddEntry(const std::shared_ptr<frp::transmission::ITransmission>& transmission, const std::shared_ptr<frp::messages::HandshakeRequest>& request) noexcept;
            virtual bool                                                    CloseEntry(MappingType type, int port) noexcept;

        private:
            bool                                                            HandshakeAsync(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;
            bool                                                            ClearTimeout(void* key) noexcept;
            bool                                                            AddTimeout(void* key, std::shared_ptr<boost::asio::deadline_timer>&& timeout) noexcept;

        private:
            std::atomic<bool>                                               disposed_;
            std::shared_ptr<AppConfiguration>                               configuration_;
            std::shared_ptr<frp::threading::Hosting>                        hosting_;
            std::shared_ptr<boost::asio::io_context>                        context_;
            boost::asio::ip::tcp::acceptor                                  acceptor_;
            TimeoutTable                                                    timeouts_;
            MappingEntryTable                                               entiress_[MappingType::MappingType_MaxType];
        };
    }
}