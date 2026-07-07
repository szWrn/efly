#pragma once

#include "efly_types.h"

// ============================================================
// DLL export/import macros
// ============================================================

#ifdef _WIN32
  #ifdef EFL_API_EXPORTS
    #define EFL_API __declspec(dllexport)
  #else
    #define EFL_API __declspec(dllimport)
  #endif
#else
  #define EFL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Lifecycle
// ============================================================

/// Initialize the library. Must be called first.
/// Returns 0 on success, negative error code on failure.
EFL_API int efly_init(void);

/// Shutdown the library, release all resources.
EFL_API void efly_shutdown(void);

// ============================================================
// Peer discovery
// ============================================================

/// Start broadcasting discovery beacons and listening for peers.
/// @param port  The unicast port for file transfers (announced to peers).
EFL_API int efly_start_discovery(uint16_t port);

/// Stop discovery.
EFL_API void efly_stop_discovery(void);

// ============================================================
// File transfer — sender side
// ============================================================

/// Send a file to a remote peer.
/// @param file_path   Path to the file to send.
/// @param remote_ip   Target IP address (e.g. "192.168.1.5").
/// @param remote_port Target port.
/// @param out_file_id Receives the generated transfer ID.
/// @return 0 on success, negative error code on failure.
EFL_API int efly_send_file(
    const char* file_path,
    const char* remote_ip,
    uint16_t    remote_port,
    uint32_t*   out_file_id);

// ============================================================
// File transfer — receiver side
// ============================================================

/// Accept an incoming file transfer.
/// @param file_id  The transfer ID from the announce callback.
/// @param save_dir Directory to save the received file.
EFL_API int efly_accept_transfer(uint32_t file_id, const char* save_dir);

/// Reject an incoming file transfer.
EFL_API int efly_reject_transfer(uint32_t file_id);

// ============================================================
// Transfer control
// ============================================================

/// Cancel an in-progress transfer (works for both send and receive).
EFL_API int efly_cancel_transfer(uint32_t file_id);

// ============================================================
// Status query
// ============================================================

/// Get the progress of a transfer.
/// @param file_id        Transfer ID.
/// @param out_total      Receives total file size in bytes.
/// @param out_transferred Receives bytes transferred so far.
/// @return 0 on success, negative if transfer not found.
EFL_API int efly_get_transfer_progress(
    uint32_t  file_id,
    uint64_t* out_total,
    uint64_t* out_transferred);

// ============================================================
// Callback registration
// ============================================================

EFL_API void efly_set_peer_found_callback(
    PeerFoundCallback cb, void* user_data);

EFL_API void efly_set_transfer_announce_callback(
    TransferAnnounceCallback cb, void* user_data);

EFL_API void efly_set_transfer_progress_callback(
    TransferProgressCallback cb, void* user_data);

EFL_API void efly_set_transfer_complete_callback(
    TransferCompleteCallback cb, void* user_data);

EFL_API void efly_set_error_callback(
    ErrorCallback cb, void* user_data);

#ifdef __cplusplus
}
#endif
