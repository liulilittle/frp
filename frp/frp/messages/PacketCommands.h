#pragma once

namespace frp {
    namespace messages {
        enum PacketCommands {
            PacketCommands_Connect,
            PacketCommands_ConnectOK,
            PacketCommands_Disconnect,
            PacketCommands_Write,
            PacketCommands_WriteTo,
            PacketCommands_Heartbeat,
        };
    }
}