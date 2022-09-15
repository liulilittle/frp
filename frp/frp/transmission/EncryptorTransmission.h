#pragma once

#include <frp/transmission/Transmission.h>
#include <frp/cryptography/Encryptor.h>

namespace frp {
    namespace transmission {
        class EncryptorTransmission : public Transmission {
        public:
            EncryptorTransmission(
                const std::shared_ptr<frp::threading::Hosting>&             hosting, 
                const std::shared_ptr<boost::asio::io_context>&             context, 
                const std::shared_ptr<boost::asio::ip::tcp::socket>&        socket,
                const std::string&                                          method,
                const std::string&                                          password) noexcept;

        public:
            virtual bool                                                    WriteAsync(const std::shared_ptr<Byte>& buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept override;
            virtual bool                                                    ReadAsync(const BOOST_ASIO_MOVE_ARG(ReadAsyncCallback) callback) noexcept override;

        private:
            std::atomic<bool>                                               disposed_;
            frp::cryptography::Encryptor                                    encryptor_;
        };
    }
}