#include <frp/messages/HandshakeRequest.h>
#include <frp/io/BinaryReader.h>
#include <frp/io/MemoryStream.h>

namespace frp {
    namespace messages {
        std::shared_ptr<Byte> HandshakeRequest::Serialize(int& length) noexcept {
            frp::io::MemoryStream stream;
            if (!Serialize(stream)) {
                return NULL;
            }

            length = stream.GetPosition();
            if (length < 1) {
                return NULL;
            }
            return stream.GetBuffer();
        }

        bool HandshakeRequest::Serialize(frp::io::Stream& stream) noexcept {
            if (!stream.CanWrite()) {
                return false;
            }

            int length = std::min<int>(Name.size(), UINT16_MAX);
            return stream.WriteByte((Byte)(Type)) &&
                stream.WriteByte((Byte)(RemotePort >> 8)) &&
                stream.WriteByte((Byte)(RemotePort)) &&
                stream.WriteByte((Byte)(length >> 8)) &&
                stream.WriteByte((Byte)(length)) &&
                stream.Write(Name.data(), 0, length);
        }

        std::shared_ptr<HandshakeRequest> HandshakeRequest::Deserialize(const void* message, int length) noexcept {
            if (!message || length < 1) {
                return NULL;
            }

            std::shared_ptr<Byte> buf((Byte*)message, [](const void*) noexcept {});
            return Deserialize(buf, 0, length);
        }

        std::shared_ptr<HandshakeRequest> HandshakeRequest::Deserialize(const std::shared_ptr<Byte> message, int offset, int length) noexcept {
            if (!message || offset < 0 || length < 1) {
                return NULL;
            }

            frp::io::MemoryStream stream(message, length);
            stream.Seek(offset, frp::io::SeekOrigin::Begin);
            return Deserialize(stream);
        }

        std::shared_ptr<HandshakeRequest> HandshakeRequest::Deserialize(frp::io::Stream& stream) noexcept {
            frp::io::BinaryReader reader(stream);
            return Deserialize(reader);
        }

        std::shared_ptr<HandshakeRequest> HandshakeRequest::Deserialize(frp::io::BinaryReader& reader) noexcept {
            if (!reader.GetStream().CanRead()) {
                return NULL;
            }

            std::shared_ptr<Byte> p = reader.ReadBytes(1);
            if (!p) {
                return NULL;
            }

            std::shared_ptr<HandshakeRequest> request = make_shared_object<HandshakeRequest>();
            if (!request) {
                return NULL;
            }
            else {
                request->Type = (frp::configuration::MappingType)(*p);
            }

            p = reader.ReadBytes(2);
            if (!p) {
                return NULL;
            }
            else {
                Byte* b = p.get();
                request->RemotePort = b[0] << 8 | b[1];
            }

            p = reader.ReadBytes(2);
            if (!p) {
                return NULL;
            }

            Byte* by = p.get();
            int length = by[0] << 8 | by[1];
            if (length < 0) {
                return NULL;
            }

            if (length == 0) {
                return request;
            }

            p = reader.ReadBytes(length);
            if (!p) {
                return NULL;
            }
            else {
                request->Name = std::string((char*)p.get(), length);
            }
            return request;
        }
    }
}