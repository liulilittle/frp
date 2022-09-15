#pragma once

#include <frp/threading/Hosting.h>
#include <frp/transmission/ITransmission.h>

namespace frp {
    namespace transmission {
        class Transmission : public ITransmission {
        protected:
            static const int ETRANSMISSION_TSS                      = 2;
            static const int ETRANSMISSION_MSS                      = frp::threading::Hosting::BufferSize;
            struct message {
                std::shared_ptr<Byte>                               packet;
                int                                                 packet_size;
                WriteAsyncCallback                                  callback;
            };
            typedef std::shared_ptr<message>                        pmessage;
            typedef std::list<pmessage>                             message_queue;

        public:
            Transmission(const std::shared_ptr<frp::threading::Hosting>& hosting, const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept;

        public:
            inline const std::shared_ptr<frp::threading::Hosting>&  GetHosting() noexcept {
                return hosting_;
            }
            virtual std::shared_ptr<boost::asio::io_context>        GetContext() noexcept override;
            virtual bool                                            HandshakeAsync(HandshakeType type, const BOOST_ASIO_MOVE_ARG(HandshakeAsyncCallback) callback) noexcept override;
            virtual bool                                            ReadAsync(const BOOST_ASIO_MOVE_ARG(ReadAsyncCallback) callback) noexcept override;
            virtual bool                                            WriteAsync(const std::shared_ptr<Byte>& buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept override;
            virtual void                                            Dispose() noexcept override;
            virtual frp::net::IPEndPoint                            GetLocalEndPoint() noexcept override;
            virtual frp::net::IPEndPoint                            GetRemoteEndPoint() noexcept override;

        protected:
            inline std::shared_ptr<Byte>&                           GetBuffer() noexcept {
                return buffer_;
            }
            inline std::shared_ptr<Byte>&                           SetBuffer(const std::shared_ptr<Byte>& buffer) noexcept {
                return (buffer_ = buffer);
            }
            inline std::shared_ptr<boost::asio::ip::tcp::socket>&   GetSocket() noexcept {
                return socket_;
            }

        protected:
            void                                                    OnAddWriteAsync(const BOOST_ASIO_MOVE_ARG(pmessage) message) noexcept;
            bool                                                    OnAsyncWrite(bool internall) noexcept;
            virtual bool                                            OnWriteAsync(const BOOST_ASIO_MOVE_ARG(pmessage) message) noexcept;
            
        protected:
            template<typename AsynchronousStream>
            bool                                                    Unpack(AsynchronousStream& stream, const BOOST_ASIO_MOVE_ARG(ReadAsyncCallback) callback) noexcept {
                if (!callback) {
                    return false;
                }

                const std::shared_ptr<Reference> reference_ = GetReference();
                const ReadAsyncCallback callback_ = BOOST_ASIO_MOVE_CAST(ReadAsyncCallback)(constantof(callback));
                const static auto trigger = [](Transmission* transmission, int length, const ReadAsyncCallback& callback) noexcept {
                    if (length < 1) {
                        transmission->Close();
                    }
                    callback(NULL, length);
                };

                AsynchronousStream* const stream_ = addressof(stream);
                boost::asio::async_read(*stream_, boost::asio::buffer(buffer_.get(), ETRANSMISSION_TSS),
                    [reference_, this, callback_, stream_](const boost::system::error_code& ec, std::size_t sz) noexcept {
                        int length = std::max<int>(-1, ec ? -1 : sz);
                        if (length < 1) {
                            trigger(this, length, callback_);
                            return;
                        }

                        Byte* p = buffer_.get();
                        length = p[0] << 8 | p[1];
                        if (length < 1) {
                            trigger(this, -1, callback_);
                            return;
                        }

                        boost::asio::async_read(*stream_, boost::asio::buffer(buffer_.get(), length),
                            [reference_, this, callback_](const boost::system::error_code& ec, std::size_t sz) noexcept {
                                int length = std::max<int>(-1, ec ? -1 : sz);
                                if (length < 1) {
                                    trigger(this, length, callback_);
                                    return;
                                }

                                callback_(buffer_, length);
                            });
                    });
                return true;
            }
            static pmessage                                         Pack(const void* buffer, int offset, int length, const BOOST_ASIO_MOVE_ARG(WriteAsyncCallback) callback) noexcept;

        private:
            std::atomic<bool>                                       disposed_;
            std::shared_ptr<frp::threading::Hosting>                hosting_;
            std::shared_ptr<Byte>                                   buffer_;
            std::shared_ptr<boost::asio::io_context>                context_;
            std::shared_ptr<boost::asio::ip::tcp::socket>           socket_;
            std::atomic<bool>                                       writing_;
            message_queue                                           messages_;
            frp::net::IPEndPoint                                    localEP_;
            frp::net::IPEndPoint                                    remoteEP_;
        };
    }
}