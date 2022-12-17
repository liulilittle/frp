#include <frp/configuration/AppConfiguration.h>
#include <frp/configuration/Ini.h>
#include <frp/ssl/SSL.h>
#include <frp/net/IPEndPoint.h>
#include <frp/threading/Hosting.h>
#include <frp/cryptography/Encryptor.h>

namespace frp {
    namespace configuration {
        static bool AppConfiguration_VerityEncryptorConfiguration(const AppConfiguration& config) noexcept {
            const std::string& evp_method = config.Protocols.Encryptor.Method;
            const std::string& evp_passwd = config.Protocols.Encryptor.Password;

            if (evp_method.empty()) {
                return false;
            }

            if (evp_passwd.empty()) {
                return false;
            }
            return frp::cryptography::Encryptor::Support(evp_method);
        }

        static bool AppConfiguration_VeritySslConfiguration(const AppConfiguration& config, bool hostVerify) noexcept {
            typedef AppConfiguration::LoopbackMode LoopbackMode;

            const bool& ssl_verify_peer = config.Protocols.Ssl.VerifyPeer;
            const std::string& ssl_host = config.Protocols.Ssl.Host;
            const std::string& ssl_certificate_file = config.Protocols.Ssl.CertificateFile;
            const std::string& ssl_certificate_key_file = config.Protocols.Ssl.CertificateKeyFile;
            const std::string& ssl_certificate_chain_file = config.Protocols.Ssl.CertificateChainFile;
            const std::string& ssl_certificate_key_password = config.Protocols.Ssl.CertificateKeyPassword;
            const std::string& ssl_ciphersuites = config.Protocols.Ssl.Ciphersuites;

            if (hostVerify && ssl_host.empty()) {
                return false;
            }

            if (ssl_ciphersuites.empty()) {
                return false;
            }

            if (config.Mode == LoopbackMode::LoopbackMode_Server) {
                if (!frp::ssl::SSL::VerifySslCertificate(
                    ssl_certificate_file,
                    ssl_certificate_key_file,
                    ssl_certificate_chain_file)) {
                    return false;
                }
            }
            return true;
        }

        static bool AppConfiguration_VerityWebSocketConfiguration(const AppConfiguration& config) noexcept {
            const std::string& websocket_host = config.Protocols.WebSocket.Host;
            const std::string& websocket_path = config.Protocols.WebSocket.Path;
            if (websocket_host.empty()) {
                return false;
            }

            if (websocket_path.empty()) {
                return false;
            }

            if (websocket_path[0] != '/') {
                return false;
            }
            return true;
        }

        static bool AppConfiguration_LoadEncryptorConfiguration(std::shared_ptr<AppConfiguration>& configuration, Ini::Section& section) noexcept {
            std::string& evp_method = configuration->Protocols.Encryptor.Method;
            std::string& evp_passwd = configuration->Protocols.Encryptor.Password;

            evp_method = section["protocol.encryptor.method"];
            evp_passwd = section["protocol.encryptor.password"];

            if (evp_method.empty()) {
                return false;
            }

            if (evp_passwd.empty()) {
                return false;
            }
            return true;
        }

        static bool AppConfiguration_LoadSslConfiguration(std::shared_ptr<AppConfiguration>& configuration, Ini::Section& section) noexcept {
            typedef AppConfiguration::ProtocolType ProtocolType;

            bool& ssl_verify_peer = configuration->Protocols.Ssl.VerifyPeer;
            std::string& ssl_host = configuration->Protocols.Ssl.Host;
            std::string& ssl_certificate_file = configuration->Protocols.Ssl.CertificateFile;
            std::string& ssl_certificate_key_file = configuration->Protocols.Ssl.CertificateKeyFile;
            std::string& ssl_certificate_chain_file = configuration->Protocols.Ssl.CertificateChainFile;
            std::string& ssl_certificate_key_password = configuration->Protocols.Ssl.CertificateKeyPassword;
            std::string& ssl_ciphersuites = configuration->Protocols.Ssl.Ciphersuites;

            if (configuration->Protocol == ProtocolType::ProtocolType_SSL ||
                configuration->Protocol == ProtocolType::ProtocolType_WebSocket_SSL) {
                ssl_verify_peer = section.GetValue<bool>("protocol.ssl.verify-peer");
                ssl_host = section["protocol.ssl.host"];
                ssl_certificate_file = section["protocol.ssl.certificate-file"];
                ssl_certificate_key_file = section["protocol.ssl.certificate-key-file"];
                ssl_certificate_chain_file = section["protocol.ssl.certificate-chain-file"];
                ssl_certificate_key_password = section["protocol.ssl.certificate-key-password"];
                ssl_ciphersuites = section["protocol.ssl.ciphersuites"];
            }
            else {
                ssl_verify_peer = section.GetValue<bool>("protocol.tls.verify-peer");
                ssl_host = section["protocol.tls.host"];
                ssl_certificate_file = section["protocol.tls.certificate-file"];
                ssl_certificate_key_file = section["protocol.tls.certificate-key-file"];
                ssl_certificate_chain_file = section["protocol.tls.certificate-chain-file"];
                ssl_certificate_key_password = section["protocol.tls.certificate-key-password"];
                ssl_ciphersuites = section["protocol.tls.ciphersuites"];
            }

            if (ssl_ciphersuites.empty()) {
                ssl_ciphersuites = frp::ssl::SSL::GetSslCiphersuites();
            }

            return ssl_host.size() > 0;
        }

        static bool AppConfiguration_LoadWebSocketConfiguration(std::shared_ptr<AppConfiguration>& configuration, Ini::Section& section) noexcept {
            typedef AppConfiguration::ProtocolType ProtocolType;
            typedef AppConfiguration::LoopbackMode LoopbackMode;

            std::string& websocket_host = configuration->Protocols.WebSocket.Host;
            std::string& websocket_path = configuration->Protocols.WebSocket.Path;

            websocket_host = section["protocol.websocket.host"];
            websocket_path = section["protocol.websocket.path"];

            if (websocket_host.empty()) {
                return false;
            }
            elif(websocket_path.empty()) {
                websocket_path = "/";
            }
            elif(websocket_path[0] != '/') {
                return false;
            }

            AppConfiguration_LoadSslConfiguration(configuration, section);
            if (AppConfiguration_VeritySslConfiguration(*configuration, false)) {
                configuration->Protocols.Ssl.Host.clear();
            }
            else {
                configuration->Protocols.Ssl.ReleaseAllPairs();
                configuration->Protocol = ProtocolType::ProtocolType_WebSocket;
            }
            return true;
        }

        std::shared_ptr<AppConfiguration> AppConfiguration::LoadIniFile(const std::string& iniFile) noexcept {
            typedef frp::configuration::Ini Ini;
            typedef frp::net::IPEndPoint    IPEndPoint;

            std::shared_ptr<Ini> config = Ini::LoadFile(iniFile);
            if (!config) {
                return NULL;
            }

            std::shared_ptr<AppConfiguration> configuration = make_shared_object<AppConfiguration>();
            if (!configuration) {
                return NULL;
            }

            Ini& ini = *config;
            /* Reading app section */
            {
                Ini::Section& section = ini["app"];
                configuration->Mode = LoopbackMode::LoopbackMode_Client;
                configuration->Alignment = section.GetValue<int>("alignment");
                configuration->Backlog = section.GetValue<int>("backlog");
                configuration->IP = section["ip"];
                configuration->Port = section.GetValue<int>("port");
                configuration->FastOpen = section.GetValue<bool>("fast-open");
                configuration->Turbo.Lan = section.GetValue<bool>("turbo.lan");
                configuration->Turbo.Wan = section.GetValue<bool>("turbo.wan");
                configuration->Connect.Timeout = section.GetValue<int>("connect.timeout");
                configuration->Inactive.Timeout = section.GetValue<int>("inactive.timeout");
                configuration->Handshake.Timeout = section.GetValue<int>("handshake.timeout");

                IPEndPoint ip(configuration->IP.data(), configuration->Port);
                if (IPEndPoint::IsInvalid(ip)) {
                    configuration->IP = boost::asio::ip::address_v6::any().to_string();
                }
                else {
                    configuration->IP = ip.ToAddressString();
                }

                int& connectTimeout = configuration->Connect.Timeout;
                if (connectTimeout < 1) {
                    connectTimeout = 10;
                }

                int& inactiveTimeout = configuration->Inactive.Timeout;
                if (inactiveTimeout < 1) {
                    inactiveTimeout = 72;
                }

                int& handshakeTimeout = configuration->Connect.Timeout;
                if (handshakeTimeout < 1) {
                    handshakeTimeout = 5;
                }

                int& alignment = configuration->Alignment;
                if (alignment < (UINT8_MAX << 1)) {
                    alignment = (UINT8_MAX << 1);
                }

                int& globalPort = configuration->Port;
                if (globalPort < IPEndPoint::MinPort || globalPort > IPEndPoint::MaxPort) {
                    globalPort = IPEndPoint::MinPort;
                }

                std::string mode = section["mode"];
                if (mode.size()) {
                    int ch = tolower(mode[0]);
                    if (ch == 's') {
                        configuration->Mode = LoopbackMode::LoopbackMode_Server;
                    }
                    elif(ch >= '0' && ch <= '9') {
                        configuration->Mode = (LoopbackMode)(ch - '0');
                        if (configuration->Mode < LoopbackMode::LoopbackMode_None || configuration->Mode >= LoopbackMode::LoopbackMode_MaxType) {
                            return NULL;
                        }
                    }
                }

                std::string protocol = section["protocol"];
                std::size_t protocol_size = protocol.size();
                if (protocol_size) {
                    const std::size_t MAX_PCH_COUNT = 2;

                    /* Defines and formats an array of characters for comparison. */
                    int pch[MAX_PCH_COUNT];
                    memset(pch, 0, sizeof(pch));

                    /* The padding protocol is used to compare character arrays. */
                    for (int i = 0, loop = std::min<std::size_t>(protocol_size, MAX_PCH_COUNT); i < loop; i++) {
                        pch[i] = tolower(protocol[i]);
                    }

                    if (pch[0] == 'w') { // websocket
                        std::string subprotocol = ToLower(protocol);
                        if (subprotocol.rfind("tls") != std::string::npos) {
                            configuration->Protocol = ProtocolType::ProtocolType_WebSocket_TLS;
                        }
                        elif(subprotocol.rfind("ssl") != std::string::npos) {
                            configuration->Protocol = ProtocolType::ProtocolType_WebSocket_SSL;
                        }
                        else {
                            configuration->Protocol = ProtocolType::ProtocolType_WebSocket;
                        }
                    }
                    elif(pch[0] == 'e') { // EVP
                        configuration->Protocol = ProtocolType::ProtocolType_Encryptor;
                    }
                    elif(pch[0] == 's') { // SSL
                        configuration->Protocol = ProtocolType::ProtocolType_SSL;
                    }
                    elif(pch[0] == 't' && pch[1] == 'l') { // TLS
                        configuration->Protocol = ProtocolType::ProtocolType_TLS;
                    }
                    elif(pch[0] >= '0' && pch[0] <= '9') { // number optional
                        configuration->Protocol = (ProtocolType)(pch[0] - '0');
                        if (configuration->Protocol < ProtocolType::ProtocolType_None || configuration->Protocol >= ProtocolType::ProtocolType_MaxType) {
                            return NULL;
                        }
                    }
                }

                /* Loading protocol websocket settings. */
                if (configuration->Protocol == ProtocolType::ProtocolType_WebSocket ||
                    configuration->Protocol == ProtocolType::ProtocolType_WebSocket_SSL ||
                    configuration->Protocol == ProtocolType::ProtocolType_WebSocket_TLS) {
                    if (!AppConfiguration_LoadWebSocketConfiguration(configuration, section)) {
                        return NULL;
                    }
                }

                /* Loading protocol ssl/tls settings. */
                if (configuration->Protocol == ProtocolType::ProtocolType_SSL ||
                    configuration->Protocol == ProtocolType::ProtocolType_TLS) {
                    if (!AppConfiguration_LoadSslConfiguration(configuration, section)) {
                        return NULL;
                    }
                }

                /* Loading protocol evp settings. */
                if (configuration->Protocol == ProtocolType::ProtocolType_Encryptor) {
                    if (!AppConfiguration_LoadEncryptorConfiguration(configuration, section)) {
                        return NULL;
                    }
                }

                /* Remove app sections. */
                ini.Remove(section.Name);
            }
            /* Loading all mappings. */
            if (ini.Count()) {
                Ini::iterator tail = ini.begin();
                Ini::iterator endl = ini.end();
                for (; tail != endl; tail++) {
                    Ini::Section& section = tail->second;
                    if (section.Count() < 1) {
                        continue;
                    }

                    std::string type = section["type"];
                    std::string localIP = section["local-ip"];
                    if (type.empty()) {
                        continue;
                    }

                    int concurrent = section.GetValue<int>("concurrent");
                    int reconnect = section.GetValue<int>("reconnect");
                    int localPort = section.GetValue<int>("local-port");
                    int remotePort = section.GetValue<int>("remote-port");
                    if (remotePort <= IPEndPoint::MinPort || remotePort > IPEndPoint::MaxPort) {
                        continue;
                    }

                    reconnect = std::max(1, reconnect);
                    concurrent = std::max(1, concurrent);

                    IPEndPoint localEP(localIP.data(), localPort);
                    if (IPEndPoint::IsInvalid(localEP)) {
                        continue;
                    }
                    elif(localPort <= IPEndPoint::MinPort || localPort > IPEndPoint::MaxPort) {
                        continue;
                    }

                    MappingConfiguration mapping;
                    mapping.Name = section.Name;
                    mapping.Type = MappingType::MappingType_TCP;
                    mapping.LocalIP = localEP.ToAddressString();
                    mapping.LocalPort = localPort;
                    mapping.RemotePort = remotePort;
                    mapping.Reconnect = reconnect;
                    mapping.Concurrent = concurrent;

                    if (type.size()) {
                        int ch = tolower(type[0]);
                        if ((ch == 'u') || ((ch >= '0' && ch <= '9') && (ch - '0'))) {
                            mapping.Type = MappingType::MappingType_UDP;
                        }
                    }
                    configuration->Mappings.push_back(std::move(mapping));
                }
            }
            return IsInvalid(configuration) ? NULL : std::move(configuration);
        }

        bool AppConfiguration::IsInvalid(const std::shared_ptr<AppConfiguration>& config) noexcept {
            if (NULL == config) {
                return true;
            }
            return IsInvalid(*config);
        }

        bool AppConfiguration::IsInvalid(const AppConfiguration& config) noexcept {
            typedef frp::net::IPEndPoint                IPEndPoint;

            if (config.Protocol < ProtocolType::ProtocolType_None || config.Protocol >= ProtocolType::ProtocolType_MaxType) {
                return true;
            }
            elif(config.Mode < LoopbackMode::LoopbackMode_None || config.Mode >= LoopbackMode::LoopbackMode_MaxType) {
                return true;
            }
            elif(config.Port <= IPEndPoint::MinPort || config.Port > IPEndPoint::MaxPort) {
                return true;
            }
            elif(config.Mode == LoopbackMode::LoopbackMode_Client) {
                if (IPEndPoint(config.IP.data(), config.Port).IsBroadcast()) {
                    return false;
                }
            }
            else {
                if (IPEndPoint::IsInvalid(IPEndPoint(config.IP.data(), config.Port))) {
                    return false;
                }
            }

            if (config.Protocol == ProtocolType::ProtocolType_WebSocket ||
                config.Protocol == ProtocolType::ProtocolType_WebSocket_SSL) {
                if (!AppConfiguration_VerityWebSocketConfiguration(config)) {
                    return true;
                }
            }
            elif(config.Protocol == ProtocolType::ProtocolType_SSL ||
                config.Protocol == ProtocolType::ProtocolType_TLS) {
                if (!AppConfiguration_VeritySslConfiguration(config, true)) {
                    return true;
                }
            }
            elif(config.Protocol == ProtocolType::ProtocolType_Encryptor) {
                if (!AppConfiguration_VerityEncryptorConfiguration(config)) {
                    return true;
                }
            }

            if (config.Backlog < 1) {
                return true;
            }

            if (config.Alignment < (UINT8_MAX << 1) || config.Alignment > frp::threading::Hosting::BufferSize) {
                return false;
            }

            if (config.Connect.Timeout < 1) {
                return false;
            }

            if (config.Inactive.Timeout < 1) {
                return false;
            }

            if (config.Mode == LoopbackMode::LoopbackMode_Server) {
                if (config.Handshake.Timeout < 1) {
                    return false;
                }
            }

            MappingConfigurationArrayList::const_iterator tail = config.Mappings.begin();
            MappingConfigurationArrayList::const_iterator endl = config.Mappings.end();
            if (tail == endl) {
                return config.Mode != LoopbackMode::LoopbackMode_Server;
            }

            for (; tail != endl; ++tail) {
                const MappingConfiguration& mapping = *tail;
                if (mapping.Name.empty()) {
                    return true;
                }

                if (mapping.Reconnect < 1) {
                    return true;
                }

                if (mapping.Concurrent < 1) {
                    return true;
                }

                if (mapping.Type < MappingType::MappingType_None || mapping.Type >= MappingType::MappingType_MaxType) {
                    return true;
                }

                if (mapping.RemotePort <= IPEndPoint::MinPort || mapping.RemotePort > IPEndPoint::MaxPort) {
                    return true;
                }

                if (mapping.LocalPort <= IPEndPoint::MinPort || mapping.LocalPort > IPEndPoint::MaxPort) {
                    return true;
                }

                if (IPEndPoint::IsInvalid(IPEndPoint(mapping.LocalIP.data(), mapping.LocalPort))) {
                    return true;
                }
            }
            return false;
        }
    }
}