#pragma once

#include <frp/transmission/WebSocketTransmission.h>

namespace frp {
    namespace transmission {
        namespace templates {
            template<class T>
            class WebSocket : public IDisposable {
            public:
                typedef frp::transmission::ITransmission            ITransmission;
                typedef ITransmission::HandshakeAsyncCallback       HandshakeAsyncCallback;
                typedef ITransmission::HandshakeType                HandshakeType;
                typedef frp::transmission::WebSocketTransmission    WebSocketTransmission;

            public:
                inline WebSocket(
                    T&                                              websocket,
                    std::string&                                    host,
                    std::string&                                    path) noexcept 
                    : host_(host)
                    , path_(path)
                    , websocket_(websocket) {
                    websocket_.binary(true);
                }

            public:
                inline void                                         Close() noexcept {
                    Dispose();
                }
                inline bool                                         HandshakeAsync(HandshakeType type, const BOOST_ASIO_MOVE_ARG(HandshakeAsyncCallback) callback) noexcept {
                    if (!callback || host_.empty() || path_.empty()) {
                        return false;
                    }

                    const std::shared_ptr<Reference> reference = GetReference();
                    const HandshakeAsyncCallback callback_ = BOOST_ASIO_MOVE_CAST(HandshakeAsyncCallback)(constantof(callback));

                    if (type == HandshakeType::HandshakeType_Client) {
                        websocket_.async_handshake(host_, path_,
                            [reference, this, callback_](const boost::system::error_code& ec) noexcept {
                                bool success = ec ? false : true;
                                if (!success) {
                                    Close();
                                }

                                callback_(success);
                            });
                    }
                    else {
                        typedef boost::beast::http::request<boost::beast::http::dynamic_body> http_request;

                        // This buffer is used for reading and must be persisted
                        std::shared_ptr<boost::beast::flat_buffer> buffer = make_shared_object<boost::beast::flat_buffer>();

                        // Declare a container to hold the response
                        std::shared_ptr<http_request> req = make_shared_object<http_request>();

                        // Receive the HTTP response
                        boost::beast::http::async_read(websocket_.next_layer(), *buffer, *req,
                            [reference, this, buffer, req, callback_](boost::system::error_code ec, std::size_t sz) noexcept {
                                if (ec == boost::beast::http::error::end_of_stream) {
                                    ec = boost::beast::websocket::error::closed;
                                }

                                bool success = false;
                                do {
                                    if (ec) {
                                        break;
                                    }

                                    // Set suggested timeout settings for the websocket
                                    websocket_.set_option(
                                        boost::beast::websocket::stream_base::timeout::suggested(
                                            boost::beast::role_type::server));

                                    // Set a decorator to change the Server of the handshake
                                    websocket_.set_option(boost::beast::websocket::stream_base::decorator(
                                        [](boost::beast::websocket::response_type& res) noexcept {
                                            res.set(boost::beast::http::field::server, std::string(BOOST_BEAST_VERSION_STRING));
                                        }));

                                    success = WebSocketTransmission::CheckPath(path_, req->target());
                                    if (!success) {
                                        ec = boost::beast::websocket::error::closed;
                                    }
                                    else {
                                        websocket_.async_accept(*req,
                                            [reference, this, req, callback_](const boost::system::error_code& ec) noexcept {
                                                bool success = ec ? false : true;
                                                if (!success) {
                                                    Close();
                                                }
                                                callback_(success);
                                            });
                                    }
                                } while (0);

                                if (!success) {
                                    Close();
                                    callback_(success);
                                }
                            });
                    }
                    return true;
                }

            private:
                std::string&                                        host_;
                std::string&                                        path_;
                T&                                                  websocket_;
            };
        }
    }
}