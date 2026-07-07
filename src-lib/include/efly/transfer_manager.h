#pragma once

#include "efly_types.h"
#include "file_io.h"
#include "packet_buffer.h"
#include "protocol_handler.h"

#include <boost/asio.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace efly {

// ============================================================
// TransferManager — orchestrates file send & receive
// ============================================================

/// Callbacks for transfer events (fired from io_context thread).
using TransferAnnounceFunc = std::function<void(
    uint32_t file_id, const std::string& filename,
    uint64_t file_size, const std::string& from_ip)>;

using TransferProgressFunc = std::function<void(
    uint32_t file_id, uint64_t total, uint64_t transferred)>;

using TransferCompleteFunc = std::function<void(
    uint32_t file_id, const std::string& saved_path)>;

using TransferErrorFunc = std::function<void(
    uint32_t file_id, int error_code, const std::string& message)>;

// ============================================================
// TransferSession — per-transfer state
// ============================================================

struct TransferSession {
    TransferState state = TransferState::IDLE;

    // File info
    uint32_t    file_id    = 0;
    uint64_t    total_size = 0;
    std::string file_name;
    std::string save_path;

    // Peer endpoint
    boost::asio::ip::udp::endpoint peer;

    // I/O
    std::unique_ptr<FileReader> reader;
    std::unique_ptr<FileWriter> writer;

    // Go-Back-N state (sender)
    PacketBuffer                send_buf{EFL_WINDOW_SIZE};
    bool                        has_acked  = false; // true after first ACK
    uint32_t                    last_acked = 0;     // cumulative ack_seq from receiver
    uint32_t                    next_seq   = 0;     // next chunk index to send
    bool                        all_chunks_sent = false;
    uint32_t                    retry_count = 0;
    uint32_t                    timer_gen  = 0;     // incremented per-timer-start; stale detection

    // Receive state
    PacketBuffer                recv_buf{EFL_WINDOW_SIZE};
    uint32_t                    recv_expected = 0;  // next expected chunk seq

    // Timers
    std::unique_ptr<boost::asio::steady_timer> ack_timer;
    std::unique_ptr<boost::asio::steady_timer> recv_timer;

    bool is_sender() const { return reader != nullptr; }
};

// ============================================================
// TransferManager
// ============================================================

class TransferManager {
public:
    TransferManager(boost::asio::io_context& io);
    ~TransferManager();

    TransferManager(const TransferManager&) = delete;
    TransferManager& operator=(const TransferManager&) = delete;

    // ---- Packet dispatch ----

    /// Handle an incoming transfer-related packet.
    void on_packet(const Packet& pkt);

    // ---- Sender API ----

    /// Initiate sending a file. Returns the new file_id.
    uint32_t start_send(const std::string& file_path,
                        const boost::asio::ip::udp::endpoint& dest);

    // ---- Receiver API ----

    /// Accept an incoming transfer.
    void accept_transfer(uint32_t file_id, const std::string& save_dir);

    /// Reject an incoming transfer.
    void reject_transfer(uint32_t file_id);

    // ---- Control ----

    /// Cancel a transfer from either side.
    void cancel_transfer(uint32_t file_id);

    // ---- Query ----

    /// Get progress. Returns false if transfer not found.
    bool get_progress(uint32_t file_id, uint64_t& total, uint64_t& transferred) const;

    // ---- Callbacks ----

    void set_announce_callback(TransferAnnounceFunc cb) { on_announce_ = std::move(cb); }
    void set_progress_callback(TransferProgressFunc cb) { on_progress_ = std::move(cb); }
    void set_complete_callback(TransferCompleteFunc cb) { on_complete_ = std::move(cb); }
    void set_error_callback(TransferErrorFunc cb) { on_error_ = std::move(cb); }

    // ---- Sender function ----

    /// Set the function used to send raw packets.
    void set_sender(std::function<void(const std::vector<uint8_t>&,
                                       const boost::asio::ip::udp::endpoint&)> sender) {
        sender_ = std::move(sender);
    }

private:
    // Packet handlers
    void handle_file_announce(const Packet& pkt);
    void handle_file_announce_ack(const Packet& pkt);
    void handle_file_data(const Packet& pkt);
    void handle_ack(const Packet& pkt);
    void handle_transfer_complete(const Packet& pkt);
    void handle_cancel(const Packet& pkt);

    // Send-side helpers
    void fill_and_send_window(TransferSession& s);
    void send_window(TransferSession& s);
    void start_ack_timer(TransferSession& s);
    void on_ack_timeout(uint32_t file_id);

    // Receive-side helpers
    void send_ack(const TransferSession& s);
    void write_contiguous(TransferSession& s);
    void start_recv_timer(TransferSession& s);
    void on_recv_timeout(uint32_t file_id);

    // Utilities
    uint32_t generate_file_id();
    void remove_session(uint32_t file_id);
    TransferSession* find_session(uint32_t file_id);
    void fire_error(uint32_t file_id, int code, const std::string& msg);
    void send_packet(const Packet& pkt, const boost::asio::ip::udp::endpoint& dest);

    // State
    boost::asio::io_context& io_;
    std::unordered_map<uint32_t, std::unique_ptr<TransferSession>> send_sessions_;
    std::unordered_map<uint32_t, std::unique_ptr<TransferSession>> recv_sessions_;
    std::atomic<uint32_t> next_id_{1};

    // Callbacks
    TransferAnnounceFunc  on_announce_;
    TransferProgressFunc  on_progress_;
    TransferCompleteFunc  on_complete_;
    TransferErrorFunc     on_error_;

    // Sender function
    std::function<void(const std::vector<uint8_t>&,
                       const boost::asio::ip::udp::endpoint&)> sender_;
};

} // namespace efly
