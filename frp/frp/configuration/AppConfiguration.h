#pragma once

#include <frp/configuration/SslConfiguration.h>
#include <frp/configuration/MappingConfiguration.h>
#include <frp/configuration/WebSocketConfiguration.h>

namespace frp {
    namespace configuration {
        class AppConfiguration final {
        public:
            enum LoopbackMode {
                LoopbackMode_None,
                LoopbackMode_Client = LoopbackMode_None,
                LoopbackMode_Server,
                LoopbackMode_MaxType,
            };
            LoopbackMode                                Mode = LoopbackMode::LoopbackMode_Client;
            std::string                                 IP;
            int                                         Port = 0;
            int                                         Alignment = 0;
            int                                         Backlog = 511;
            bool                                        FastOpen = false;
            bool                                        Turbo = false;
            struct {
                int                                     Timeout = 10;
            }                                           Connect;
            struct {
                int                                     Timeout = 5;
            }                                           Handshake;
            struct {
                int                                     Timeout = 72;
            }                                           Inactive;
            enum ProtocolType {
                ProtocolType_None,
                ProtocolType_TCP = LoopbackMode_None,
                ProtocolType_SSL,
                ProtocolType_TLS,
                ProtocolType_Encryptor,
                ProtocolType_WebSocket,
                ProtocolType_WebSocket_SSL,
                ProtocolType_WebSocket_TLS,
                ProtocolType_MaxType,
            };
            ProtocolType                                Protocol = ProtocolType::ProtocolType_TCP;
            struct {
                WebSocketConfiguration                  WebSocket;
                SslConfiguration                        Ssl;
                struct {
                    std::string                         Method;
                    std::string                         Password;
                }                                       Encryptor;
            }                                           Protocols;
            MappingConfigurationArrayList               Mappings;

        public:
            static bool                                 IsInvalid(const std::shared_ptr<AppConfiguration>& config) noexcept;
            static bool                                 IsInvalid(const AppConfiguration& config) noexcept;
            static std::shared_ptr<AppConfiguration>    LoadIniFile(const std::string& iniFile) noexcept;
        };
    }
}