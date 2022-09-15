#include <frp/configuration/SslConfiguration.h>

namespace frp {
    namespace configuration {
        void SslConfiguration::ReleaseAllPairs() noexcept {
            Host.clear();
            VerifyPeer = true;
            CertificateFile.clear();
            CertificateKeyFile.clear();
            CertificateChainFile.clear();
            CertificateKeyPassword.clear();
            Ciphersuites.clear();
        }
    }
}