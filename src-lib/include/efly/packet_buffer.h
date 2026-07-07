#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace efly {

// ============================================================
// PacketBuffer — ordered sliding-window buffer
//
// Used for both send-side (Go-Back-N window) and receive-side
// (reorder out-of-order chunks).
// ============================================================

class PacketBuffer {
public:
    explicit PacketBuffer(uint32_t window_size = 16);

    // ---- Send-side API ----

    /// Push a pre-encoded chunk at a given sequence number.
    /// Returns false if the window is full.
    bool push(uint32_t seq, std::vector<uint8_t> data);

    /// Get all packets in [start_seq, end_seq] (inclusive).
    /// Returns empty vector if none found.
    std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>>
    get_window(uint32_t start_seq, uint32_t end_seq) const;

    /// Acknowledge (discard) all entries with seq <= last_acked.
    void ack_up_to(uint32_t last_acked);

    /// Return true if no entries remain.
    bool empty() const;

    /// Clear all entries.
    void clear();

    /// Total entries currently buffered.
    size_t size() const;

    // ---- Receive-side API ----

    /// Insert a received chunk at seq.
    /// Returns true if it was inserted (not a duplicate).
    bool insert(uint32_t seq, std::vector<uint8_t> data);

    /// Get the contiguous block starting at `expected`.
    /// Returns the data for `expected` if available, nullopt if there's a gap.
    std::optional<std::vector<uint8_t>> get_contiguous_from(uint32_t expected);

    /// The next expected sequence number (after the last contiguous block).
    uint32_t next_expected() const { return expected_seq_; }

    /// Discard all up to and including ack_seq, advance expected.
    void advance_expected(uint32_t new_expected);

private:
    uint32_t window_size_;
    bool     has_acked_    = false;  // true after first ACK received
    uint32_t last_acked_   = 0;
    uint32_t expected_seq_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> buffer_;
};

} // namespace efly
