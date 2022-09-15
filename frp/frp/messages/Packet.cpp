#include <frp/messages/Packet.h>
#include <frp/io/BinaryReader.h>
#include <frp/io/MemoryStream.h>
#include <frp/messages/NetworkAddress.h>

namespace frp {
    namespace messages {
        std::shared_ptr<Byte> Packet::Serialize(int& length) noexcept {
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

        bool Packet::Serialize(frp::io::Stream& stream) noexcept {
            if (!stream.CanWrite()) {
                return false;
            }

            if (Command != frp::messages::PacketCommands::PacketCommands_WriteTo) {
                return stream.WriteByte((Byte)(Command)) &&
                    stream.WriteByte((Byte)(Id >> 24)) &&
                    stream.WriteByte((Byte)(Id >> 16)) &&
                    stream.WriteByte((Byte)(Id >> 8)) &&
                    stream.WriteByte((Byte)(Id)) &&
                    stream.Write(Buffer.get(), Offset, Length);
            }
            else {
                return stream.WriteByte((Byte)(Command)) && stream.Write(Buffer.get(), Offset, Length);
            }
        }

        std::shared_ptr<Packet> Packet::Deserialize(const void* message, int length) noexcept {
            if (!message || length < 1) {
                return NULL;
            }

            std::shared_ptr<Byte> buf((Byte*)message, [](const void*) noexcept {});
            return Deserialize(buf, 0, length);
        }

        std::shared_ptr<Packet> Packet::Deserialize(const std::shared_ptr<Byte>& message, int offset, int length) noexcept {
            if (!message || offset < 0 || length < 1) {
                return NULL;
            }

            std::shared_ptr<Packet> packet = make_shared_object<Packet>();
            if (!packet) {
                return NULL;
            }

            Byte* p = message.get();
            packet->Command = (frp::messages::PacketCommands)*p++;
            if (packet->Command == frp::messages::PacketCommands::PacketCommands_WriteTo) {
                packet->Id = 0;
            }
            elif(length < 5) {
                return NULL;
            }
            else {
                packet->Id = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
                p += 4;
            }

            int next = p - message.get();
            packet->Offset = offset + next;
            packet->Length = length - next;
            packet->Buffer = message;
            return packet;
        }

        bool Packet::PackWriteTo(frp::io::Stream& stream, const void* buffer, int offset, int length, const boost::asio::ip::udp::endpoint& endpoint) noexcept {
            if (NULL == buffer || offset < 0 || length < 1) {
                return false;
            }

            frp::messages::NetworkAddressV4 addressV4; /* The UDP/IP packets are not in sequence, do not need to ensure the sequence. */
            frp::messages::NetworkAddressV6 addressV6;

            frp::messages::Packet messages;
            messages.Command = frp::messages::PacketCommands::PacketCommands_WriteTo;
            messages.Id = 0;
            messages.Offset = 0;
            frp::messages::NetworkAddress::ToAddress(messages, addressV4, addressV6, endpoint);

            messages.Serialize(stream);
            return stream.Write((char*)buffer, 0, length);
        }
    }
}