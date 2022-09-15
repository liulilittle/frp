#include <frp/cryptography/Encryptor.h>

namespace frp {
    namespace cryptography {
        Encryptor::Encryptor(bool stream, const std::string& method, const std::string& password) noexcept
            : _cipher(NULL)
            , _stream(stream)
            , _method(method)
            , _password(password) {
            initKey(method, password);
            if (!stream) {
                initCipher(_encryptCTX, 1, 1);
                initCipher(_decryptCTX, 0, 1);
            }
        }

        void Encryptor::initKey(const std::string& method, const std::string password) {
            _cipher = EVP_get_cipherbyname(method.data());
            if (NULL == _cipher) {
                throw std::runtime_error("Such encryption cipher methods are not supported");
            }

            _iv = make_shared_alloc<Byte>(EVP_CIPHER_iv_length(_cipher)); /* RAND_bytes(iv.get(), ivLen); */
            if (NULL == _iv) {
                throw std::runtime_error("Unable to alloc iv block memory.");
            }

            _key = make_shared_alloc<Byte>(EVP_CIPHER_key_length(_cipher));
            if (NULL == _key) {
                throw std::runtime_error("Unable to alloc key block memory.");
            }

            if (EVP_BytesToKey(_cipher, EVP_md5(), NULL, (Byte*)password.data(), (int)password.length(), 1, _key.get(), _iv.get()) < 1) {
                throw std::runtime_error("Bytes to key calculations cannot be performed using cipher with md5(md) key password iv key etc");
            }
        }

        bool Encryptor::initCipher(std::shared_ptr<EVP_CIPHER_CTX>& context, int enc, int raise) {
            bool exception = false;
            do {
                if (NULL == context.get()) {
                    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
                    context = std::shared_ptr<EVP_CIPHER_CTX>(ctx,
                        [](EVP_CIPHER_CTX* context) noexcept {
                            EVP_CIPHER_CTX_cleanup(context);
                            EVP_CIPHER_CTX_free(context);
                        });
                    EVP_CIPHER_CTX_init(context.get());
                    if ((exception = EVP_CipherInit_ex(context.get(), _cipher, NULL, NULL, NULL, enc) < 1)) {
                        break;
                    }
                    if ((exception = EVP_CIPHER_CTX_set_key_length(context.get(), EVP_CIPHER_key_length(_cipher)) < 1)) {
                        break;
                    }
                    if ((exception = EVP_CIPHER_CTX_set_padding(context.get(), 1) < 1)) {
                        break;
                    }
                }
            } while (0);
            if (exception) {
                if (raise) {
                    context = NULL;
                    throw std::runtime_error("There was a problem initializing the cipher that caused an exception to be thrown");
                }
                return false;
            }
            return true;
        }

        std::shared_ptr<Byte> Encryptor::Decrypt(Byte* data, int datalen, int& outlen) noexcept {
            outlen = 0;
            if (datalen < 0 || (NULL == data && datalen != 0)) {
                outlen = ~0;
                return NULL;
            }

            if (datalen == 0) {
                return NULL;
            }

            std::shared_ptr<EVP_CIPHER_CTX>& context = _decryptCTX;
            if (_stream) {
                if (NULL == context) {
                    if (!initCipher(context, 0, 0)) {
                        return NULL;
                    }

                    int ivlen = EVP_CIPHER_iv_length(_cipher);
                    if (datalen < ivlen) {
                        return NULL;
                    }

                    _iv = make_shared_alloc<Byte>(ivlen);
                    if (NULL == _iv) {
                        return NULL;
                    }

                    memcpy(_iv.get(), data, ivlen);
                    data += ivlen;
                    datalen -= ivlen;
                    if (datalen < 1) {
                        return NULL;
                    }

                    // INIT-CTX
                    if (EVP_CipherInit_ex(context.get(), _cipher, NULL, _key.get(), _iv.get(), 0) < 1) {
                        return NULL;
                    }
                }
            }
            elif(EVP_CipherInit_ex(context.get(), _cipher, NULL, _key.get(), _iv.get(), 0) < 1) { // INIT-CTX
                return NULL;
            }

            // DECR-DATA
            int feedbacklen = datalen + EVP_CIPHER_block_size(_cipher);
            std::shared_ptr<Byte> cipherText = make_shared_alloc<Byte>(feedbacklen);
            if (NULL == cipherText) {
                return NULL;
            }

            if (EVP_CipherUpdate(context.get(),
                cipherText.get(), &feedbacklen, data, datalen) < 1) {
                feedbacklen = ~0;
                return NULL;
            }

            outlen = feedbacklen;
            return cipherText;
        }

        std::shared_ptr<Byte> Encryptor::Encrypt(Byte* data, int datalen, int& outlen) noexcept {
            outlen = 0;
            if (datalen < 0 || (NULL == data && datalen != 0)) {
                outlen = ~0;
                return NULL;
            }

            if (datalen == 0) {
                return NULL;
            }

            std::shared_ptr<EVP_CIPHER_CTX>& context = _encryptCTX;
            if (_stream) {
                if (NULL == context) {
                    if (!initCipher(context, 1, 0)) {
                        return NULL;
                    }

                    int ivlen = EVP_CIPHER_iv_length(_cipher);
                    if (ivlen < 1) {
                        return NULL;
                    }

                    _iv = make_shared_alloc<Byte>(ivlen);
                    if (NULL == _iv) {
                        return NULL;
                    }

                    if (RAND_bytes(_iv.get(), ivlen) < 1) {
                        return NULL;
                    }

                    // INIT-CTX
                    if (EVP_CipherInit_ex(context.get(), _cipher, NULL, _key.get(), _iv.get(), 1) < 1) {
                        return NULL;
                    }

                    int messages_size;
                    std::shared_ptr<Byte> messages = Encrypt(data, datalen, messages_size);
                    if (NULL == messages) {
                        return NULL;
                    }

                    int totlen = ivlen + messages_size;
                    std::shared_ptr<Byte> content = make_shared_object<Byte>(totlen);
                    if (NULL == content) {
                        return NULL;
                    }
                    else {
                        memcpy(content.get(), _iv.get(), ivlen);
                        memcpy(content.get() + ivlen, messages.get(), messages_size);
                    }

                    outlen = totlen;
                    return content;
                }
            }
            elif(EVP_CipherInit_ex(context.get(), _cipher, NULL, _key.get(), _iv.get(), 1) < 1) { // INIT-CTX
                return NULL;
            }

            // ENCR-DATA
            int feedbacklen = datalen + EVP_CIPHER_block_size(_cipher);
            std::shared_ptr<Byte> cipherText = make_shared_alloc<Byte>(feedbacklen);
            if (NULL == cipherText) {
                return NULL;
            }

            if (EVP_CipherUpdate(context.get(),
                cipherText.get(), &feedbacklen, data, datalen) < 1) {
                outlen = ~0;
                return NULL;
            }

            outlen = feedbacklen;
            return cipherText;
        }

        void Encryptor::Initialize() noexcept {
            /* initialize OpenSSL */
            OpenSSL_add_all_ciphers();
            OpenSSL_add_all_digests();
            OpenSSL_add_all_algorithms();
            ERR_load_EVP_strings();
            ERR_load_crypto_strings();
        }

        bool Encryptor::Support(const std::string& method) noexcept {
            if (method.empty()) {
                return false;
            }
            return NULL != EVP_get_cipherbyname(method.data());
        }
    }
}