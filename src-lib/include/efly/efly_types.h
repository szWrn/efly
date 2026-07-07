#pragma once

#include <cstdint>

// ============================================================
// Protocol constants
// ============================================================

constexpr uint32_t EFL_MAGIC          = 0x45464C59;  // "EFLY"
constexpr uint8_t  EFL_VERSION        = 1;
constexpr size_t   EFL_HEADER_SIZE    = 12;           // magic(4) + ver(1) + type(1) + payload_len(2) + seq(4)
constexpr size_t   EFL_MAX_PACKET     = 1472;         // MTU-safe UDP payload
constexpr size_t   EFL_MAX_PAYLOAD    = EFL_MAX_PACKET - EFL_HEADER_SIZE;
constexpr size_t   EFL_CHUNK_SIZE     = 1430;         // max file data per DATA packet
constexpr uint16_t EFL_DISCOVERY_PORT = 21212;        // well-known discovery port
constexpr uint32_t EFL_WINDOW_SIZE    = 16;           // Go-Back-N window
constexpr uint32_t EFL_MAX_RETRIES    = 10;           // max ACK timeout retries
constexpr auto     EFL_ACK_TIMEOUT    = 500;          // ms (as int for timers)

// ============================================================
// Packet types
// ============================================================

enum class PacketType : uint8_t {
    DISCOVERY         = 0x01,
    DISCOVERY_RESP    = 0x02,
    FILE_ANNOUNCE     = 0x03,
    FILE_ANNOUNCE_ACK = 0x04,
    FILE_DATA         = 0x05,
    ACK               = 0x06,
    TRANSFER_COMPLETE = 0x07,
    CANCEL            = 0x08,
};

// ============================================================
// Error codes (returned as negative ints)
// ============================================================

enum class EflyError : int {
    OK                  = 0,
    UNKNOWN             = -1,
    SOCKET_ERROR        = -2,
    FILE_ERROR          = -3,
    TIMEOUT             = -4,
    PROTOCOL_ERROR      = -5,
    CANCELLED           = -6,
    REJECTED            = -7,
    NOT_INITIALIZED     = -8,
    INVALID_ARGUMENT    = -9,
};

// ============================================================
// Cancel reason codes
// ============================================================

enum class CancelReason : uint8_t {
    USER         = 0,
    TIMEOUT      = 1,
    DISK_ERROR   = 2,
    PROTOCOL_ERR = 3,
};

// ============================================================
// Transfer state
// ============================================================

enum class TransferState : uint8_t {
    IDLE,
    ANNOUNCING,
    TRANSFERRING,
    COMPLETE,
    CANCELLED,
};

// ============================================================
// Callback typedefs
// ============================================================

typedef void (*PeerFoundCallback)(
    const char* ip,
    uint16_t    port,
    void*       user_data);

typedef void (*TransferAnnounceCallback)(
    uint32_t    file_id,
    const char* filename,
    uint64_t    file_size,
    const char* from_ip,
    void*       user_data);

typedef void (*TransferProgressCallback)(
    uint32_t    file_id,
    uint64_t    total,
    uint64_t    transferred,
    void*       user_data);

typedef void (*TransferCompleteCallback)(
    uint32_t    file_id,
    const char* saved_path,
    void*       user_data);

typedef void (*ErrorCallback)(
    uint32_t    file_id,
    int         error_code,
    const char* message,
    void*       user_data);
