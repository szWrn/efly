#include "efly/protocol_handler.h"

namespace efly {

// ============================================================
// Decode
// ============================================================

std::optional<Packet> ProtocolHandler::decode(const uint8_t* data, size_t len) {
    if (len < EFL_HEADER_SIZE) {
        return std::nullopt;
    }

    // Validate magic
    uint32_t magic = read_u32(data);
    if (magic != EFL_MAGIC) {
        return std::nullopt;
    }

    // Validate version
    uint8_t version = read_u8(data + 4);
    if (version != EFL_VERSION) {
        return std::nullopt;
    }

    // Read type and payload length
    uint8_t  type        = read_u8(data + 5);
    uint16_t payload_len = read_u16(data + 6);
    uint32_t seq         = read_u32(data + 8);

    // Validate remaining data
    if (EFL_HEADER_SIZE + payload_len > len) {
        return std::nullopt;
    }

    const uint8_t* payload = data + EFL_HEADER_SIZE;

    Packet pkt;
    pkt.type = static_cast<PacketType>(type);
    pkt.seq  = seq;

    if (!decode_payload(pkt.type, payload, payload_len, pkt)) {
        return std::nullopt;
    }

    return pkt;
}

bool ProtocolHandler::decode_payload(PacketType type, const uint8_t* payload,
                                      size_t len, Packet& pkt) {
    switch (type) {
        case PacketType::DISCOVERY: {
            if (len < 2) return false;
            DiscoveryPayload p;
            p.port = read_u16(payload);
            pkt.payload = p;
            return true;
        }
        case PacketType::DISCOVERY_RESP: {
            if (len < 2) return false;
            DiscoveryRespPayload p;
            p.port = read_u16(payload);
            pkt.payload = p;
            return true;
        }
        case PacketType::FILE_ANNOUNCE: {
            if (len < 14) return false; // file_id(4) + file_size(8) + name_len(2)
            FileAnnouncePayload p;
            p.file_id   = read_u32(payload);
            p.file_size = read_u64(payload + 4);
            uint16_t name_len = read_u16(payload + 12);
            if (14 + name_len > len) return false;
            p.filename.assign(reinterpret_cast<const char*>(payload + 14), name_len);
            pkt.payload = p;
            return true;
        }
        case PacketType::FILE_ANNOUNCE_ACK: {
            if (len < 5) return false;
            FileAnnounceAckPayload p;
            p.file_id  = read_u32(payload);
            p.accepted = (payload[4] != 0);
            pkt.payload = p;
            return true;
        }
        case PacketType::FILE_DATA: {
            if (len < 10) return false; // file_id(4) + chunk_offset(4) + chunk_len(2)
            FileDataPayload p;
            p.file_id      = read_u32(payload);
            p.chunk_offset = read_u32(payload + 4);
            uint16_t chunk_len = read_u16(payload + 8);
            if (10 + chunk_len > len) return false;
            p.data.assign(payload + 10, payload + 10 + chunk_len);
            pkt.payload = std::move(p);
            return true;
        }
        case PacketType::ACK: {
            if (len < 8) return false;
            AckPayload p;
            p.file_id = read_u32(payload);
            p.ack_seq = read_u32(payload + 4);
            pkt.payload = p;
            return true;
        }
        case PacketType::TRANSFER_COMPLETE: {
            if (len < 4) return false;
            TransferCompletePayload p;
            p.file_id = read_u32(payload);
            pkt.payload = p;
            return true;
        }
        case PacketType::CANCEL: {
            if (len < 5) return false;
            CancelPayload p;
            p.file_id = read_u32(payload);
            p.reason  = static_cast<CancelReason>(payload[4]);
            pkt.payload = p;
            return true;
        }
    }
    return false;
}

// ============================================================
// Encode
// ============================================================

std::vector<uint8_t> ProtocolHandler::encode(const Packet& packet) {
    std::vector<uint8_t> buf;
    buf.reserve(EFL_MAX_PACKET);

    // Reserve header space (filled at the end)
    buf.resize(EFL_HEADER_SIZE);

    // Encode payload
    encode_payload(packet, buf);

    // Fill header
    size_t payload_len = buf.size() - EFL_HEADER_SIZE;
    uint8_t type_byte  = static_cast<uint8_t>(packet.type);

    write_u32(buf.data(),      EFL_MAGIC);
    write_u8 (buf.data() + 4,  EFL_VERSION);
    write_u8 (buf.data() + 5,  type_byte);
    write_u16(buf.data() + 6,  static_cast<uint16_t>(payload_len));
    write_u32(buf.data() + 8,  packet.seq);

    return buf;
}

void ProtocolHandler::encode_payload(const Packet& pkt, std::vector<uint8_t>& out) {
    std::visit([&out](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, DiscoveryPayload>) {
            uint8_t tmp[2];
            write_u16(tmp, payload.port);
            out.insert(out.end(), tmp, tmp + 2);
        }
        else if constexpr (std::is_same_v<T, DiscoveryRespPayload>) {
            uint8_t tmp[2];
            write_u16(tmp, payload.port);
            out.insert(out.end(), tmp, tmp + 2);
        }
        else if constexpr (std::is_same_v<T, FileAnnouncePayload>) {
            uint8_t tmp[14];
            write_u32(tmp,      payload.file_id);
            write_u64(tmp + 4,  payload.file_size);
            write_u16(tmp + 12, static_cast<uint16_t>(payload.filename.size()));
            out.insert(out.end(), tmp, tmp + 14);
            out.insert(out.end(),
                       reinterpret_cast<const uint8_t*>(payload.filename.data()),
                       reinterpret_cast<const uint8_t*>(payload.filename.data()) + payload.filename.size());
        }
        else if constexpr (std::is_same_v<T, FileAnnounceAckPayload>) {
            uint8_t tmp[5];
            write_u32(tmp,     payload.file_id);
            write_u8 (tmp + 4, payload.accepted ? 1 : 0);
            out.insert(out.end(), tmp, tmp + 5);
        }
        else if constexpr (std::is_same_v<T, FileDataPayload>) {
            uint8_t tmp[10];
            write_u32(tmp,     payload.file_id);
            write_u32(tmp + 4, payload.chunk_offset);
            write_u16(tmp + 8, static_cast<uint16_t>(payload.data.size()));
            out.insert(out.end(), tmp, tmp + 10);
            out.insert(out.end(), payload.data.begin(), payload.data.end());
        }
        else if constexpr (std::is_same_v<T, AckPayload>) {
            uint8_t tmp[8];
            write_u32(tmp,     payload.file_id);
            write_u32(tmp + 4, payload.ack_seq);
            out.insert(out.end(), tmp, tmp + 8);
        }
        else if constexpr (std::is_same_v<T, TransferCompletePayload>) {
            uint8_t tmp[4];
            write_u32(tmp, payload.file_id);
            out.insert(out.end(), tmp, tmp + 4);
        }
        else if constexpr (std::is_same_v<T, CancelPayload>) {
            uint8_t tmp[5];
            write_u32(tmp,     payload.file_id);
            write_u8 (tmp + 4, static_cast<uint8_t>(payload.reason));
            out.insert(out.end(), tmp, tmp + 5);
        }
    }, pkt.payload);
}

} // namespace efly
