#pragma once

#include <frp/stdafx.h>

namespace frp {
    namespace configuration {
        class SslConfiguration {
        public:
            std::string                         Host;
            bool                                VerifyPeer = true;
            std::string                         CertificateFile;
            std::string                         CertificateKeyFile;
            std::string                         CertificateChainFile;
            std::string                         CertificateKeyPassword;
            std::string                         Ciphersuites;

        public:
            void                                ReleaseAllPairs() noexcept;
        };
    }
}