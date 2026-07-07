#pragma once

#include "efly_types.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace efly {

// ============================================================
// Payload variants (one per packet type)
// ============================================================

struct DiscoveryPayload {
    uint16_t port = 0;  // unicast port this node listens on
};

struct DiscoveryRespPayload {
    uint16_t port = 0;
};

struct FileAnnouncePayload {
    uint32_t    file_id   = 0;
    uint64_t    file_size = 0;
    std::string filename;
};

struct FileAnnounceAckPayload {
    uint32_t file_id  = 0;
    bool     accepted = false;
};

struct FileDataPayload {
    uint32_t             file_id      = 0;
    uint32_t             chunk_offset = 0;  // byte offset in file
    std::vector<uint8_t> data;
};

struct AckPayload {
    uint32_t file_id = 0;
    uint32_t ack_seq = 0;  // cumulative: all chunks <= ack_seq received
};

struct TransferCompletePayload {
    uint32_t file_id = 0;
};

struct CancelPayload {
    uint32_t     file_id = 0;
    CancelReason reason  = CancelReason::USER;
};

// ---- The variant ----
using Payload = std::variant<
    DiscoveryPayload,
    DiscoveryRespPayload,
    FileAnnouncePayload,
    FileAnnounceAckPayload,
    FileDataPayload,
    AckPayload,
    TransferCompletePayload,
    CancelPayload
>;

// ============================================================
// Packet (decoded, typed)
// ============================================================

struct Packet {
    PacketType type = PacketType::DISCOVERY;
    uint32_t   seq  = 0;
    Payload    payload;

    // Convenience: get source address (set by caller after decode)
    std::string from_ip;
    uint16_t    from_port = 0;
};

// ============================================================
// Binary serialization helpers (big-endian)
// ============================================================

inline void write_u8(uint8_t* buf, uint8_t v) {
    buf[0] = v;
}

inline uint8_t read_u8(const uint8_t* buf) {
    return buf[0];
}

inline void write_u16(uint8_t* buf, uint16_t v) {
    buf[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[1] = static_cast<uint8_t>(v & 0xFF);
}

inline uint16_t read_u16(const uint8_t* buf) {
    return (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
}

inline void write_u32(uint8_t* buf, uint32_t v) {
    buf[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(v & 0xFF);
}

inline uint32_t read_u32(const uint8_t* buf) {
    return (static_cast<uint32_t>(buf[0]) << 24) |
           (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8)  |
           static_cast<uint32_t>(buf[3]);
}

inline void write_u64(uint8_t* buf, uint64_t v) {
    write_u32(buf,     static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
    write_u32(buf + 4, static_cast<uint32_t>(v & 0xFFFFFFFF));
}

inline uint64_t read_u64(const uint8_t* buf) {
    return (static_cast<uint64_t>(read_u32(buf)) << 32) | read_u32(buf + 4);
}

// ============================================================
// ProtocolHandler — stateless encode/decode
// ============================================================

class ProtocolHandler {
public:
    /// Decode a raw UDP payload into a Packet.
    /// Returns nullopt on parse failure (bad magic, version, length).
    static std::optional<Packet> decode(const uint8_t* data, size_t len);

    /// Encode a Packet into a UDP-ready byte buffer (<= 1472 bytes).
    static std::vector<uint8_t> encode(const Packet& packet);

private:
    static bool decode_payload(PacketType type, const uint8_t* payload, size_t len, Packet& pkt);
    static void encode_payload(const Packet& pkt, std::vector<uint8_t>& out);
};

} // namespace efly
