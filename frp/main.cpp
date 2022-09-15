#include <frp/configuration/AppConfiguration.h>
#include <frp/io/File.h>
#include <frp/net/IPEndPoint.h>
#include <frp/client/Router.h>
#include <frp/server/Switches.h>
#include <frp/threading/Hosting.h>
#include <frp/cryptography/Encryptor.h>

#ifndef FRP_VERSION
#define FRP_VERSION ("1.0.0.0")
#endif

#ifndef FRP_APPNAME
#define FRP_APPNAME BOOST_BEAST_VERSION_STRING
#endif

using frp::Reference;
using frp::configuration::AppConfiguration;
using frp::net::IPEndPoint;
using frp::io::File;
using frp::io::FileAccess;
using frp::threading::Hosting;

static std::shared_ptr<AppConfiguration>
LoadAppConfiguration(int argc, const char* argv[]) noexcept {
    std::string config_paths[] = { 
        frp::GetCommandArgument("-c", argc, argv),
        frp::GetCommandArgument("--c", argc, argv),
        frp::GetCommandArgument("-conf", argc, argv),
        frp::GetCommandArgument("--conf", argc, argv),
        frp::GetCommandArgument("-config", argc, argv),
        frp::GetCommandArgument("--config", argc, argv),
    };

    const int max_default_file = 10;
    const char* default_files[max_default_file] = {
        config_paths[0].data(), 
        config_paths[1].data(),
        config_paths[2].data(),
        config_paths[3].data(),
        config_paths[4].data(),
        config_paths[5].data(),
        "frp.ini",
        "frpd.ini",
        "frpc.ini", 
        "frps.ini",
    };
    for (int i = 0; i < max_default_file; i++) {
        const char* config_path = default_files[i];
        if (!File::CanAccess(config_path, FileAccess::Read)) {
            continue;
        }

        std::shared_ptr<AppConfiguration> configuration = AppConfiguration::LoadIniFile(config_path);
        if (configuration) {
            return configuration;
        }
    }
    return NULL;
}

int main(int argc, const char* argv[]) noexcept {
#ifdef _WIN32
    SetConsoleTitle(TEXT(FRP_APPNAME));
#endif

    std::shared_ptr<AppConfiguration> configuration = NULL;
    if (!frp::IsInputHelpCommand(argc, argv)) {
        configuration = LoadAppConfiguration(argc, argv);
    }

    if (!configuration) {
        std::string messages_ = "Copyright (C) 2017 ~ 2022 SupersocksR ORG. All rights reserved.\r\n";
        messages_ += "FRP(X) %s Version\r\n\r\n";
        messages_ += "Cwd:\r\n    " + frp::GetCurrentDirectoryPath() + "\r\n";
        messages_ += "Usage:\r\n";
        messages_ += "    .%s%s -c [config.ini] \r\n";
        messages_ += "Contact us:\r\n";
        messages_ += "    https://t.me/supersocksr_group \r\n";

#ifdef _WIN32
        fprintf(stdout, messages_.data(), FRP_VERSION, "\\", frp::GetExecutionFileName().data());
        system("pause");
#else
        fprintf(stdout, messages_.data(), FRP_VERSION, "/", frp::GetExecutionFileName().data());
#endif
        return 0;
    }

#ifndef _WIN32
    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
#endif

    frp::SetProcessPriorityToMaxLevel();
    frp::SetThreadPriorityToMaxLevel();
    frp::cryptography::Encryptor::Initialize(); /* Prepare the OpenSSL cryptography library environment. */

    std::shared_ptr<Hosting> hosting = Reference::NewReference<Hosting>();
    hosting->Run(
        [configuration, hosting]() noexcept {
            auto protocol = [](AppConfiguration* config) noexcept {
                switch (config->Protocol)
                {
                case AppConfiguration::ProtocolType_SSL:
                    return "ssl";
                case AppConfiguration::ProtocolType_TLS:
                    return "tls";
                case AppConfiguration::ProtocolType_Encryptor:
                    return "encryptor";
                case AppConfiguration::ProtocolType_WebSocket:
                    return "websocket";
                case AppConfiguration::ProtocolType_WebSocket_SSL:
                    return "websocket+ssl";
                case AppConfiguration::ProtocolType_WebSocket_TLS:
                    return "websocket+tls";
                default:
                    return "tcp";
                };
            };
            if (configuration->Mode == AppConfiguration::LoopbackMode_Server) {
                std::shared_ptr<frp::server::Switches> server = Reference::NewReference<frp::server::Switches>(hosting, configuration);
                if (server->Open()) {
#ifdef _WIN32
                    SetConsoleTitle(TEXT(FRP_APPNAME " -- server"));
#endif
                    fprintf(stdout, "%s\r\nLoopback:\r\n", "Application started. Press Ctrl+C to shut down.");
                    fprintf(stdout, "Mode                  : %s\r\n", "server");
                    fprintf(stdout, "Process               : %d\r\n", frp::GetCurrentProcessId());
                    fprintf(stdout, "Protocol              : %s\r\n", protocol(configuration.get()));
                    fprintf(stdout, "Cwd                   : %s\r\n", frp::GetCurrentDirectoryPath().data());
                    fprintf(stdout, "TCP/IP                : %s\r\n", IPEndPoint::ToEndPoint(server->GetLocalEndPoint()).ToString().data());
                }
                else {
                    exit(0);
                }
            }
            else {
                std::shared_ptr<frp::client::Router> client = Reference::NewReference<frp::client::Router>(hosting, configuration);
                if (client->Open()) {
#ifdef _WIN32
                    SetConsoleTitle(TEXT(FRP_APPNAME " -- client"));
#endif
                    fprintf(stdout, "%s\r\nLoopback:\r\n", "Application started. Press Ctrl+C to shut down.");
                    fprintf(stdout, "Mode                  : %s\r\n", "client");
                    fprintf(stdout, "Process               : %d\r\n", frp::GetCurrentProcessId());
                    fprintf(stdout, "Protocol              : %s\r\n", protocol(configuration.get()));
                    fprintf(stdout, "Cwd                   : %s\r\n", frp::GetCurrentDirectoryPath().data());
                    fprintf(stdout, "TCP/IP                : %s\r\n", IPEndPoint(configuration->IP.data(), configuration->Port).ToString().data());
                }
                else {
                    exit(0);
                }
            }
        });
    return 0;
}
