#include "efly/packet_buffer.h"

namespace efly {

PacketBuffer::PacketBuffer(uint32_t window_size)
    : window_size_(window_size) {}

bool PacketBuffer::push(uint32_t seq, std::vector<uint8_t> data) {
    // Don't accept seq already acked (only after first ACK)
    if (has_acked_ && seq <= last_acked_) return false;

    // Check window size: count entries that are not yet acked
    size_t count = 0;
    for (const auto& [s, _] : buffer_) {
        if (!has_acked_ || s > last_acked_) count++;
    }
    if (count >= window_size_) return false;

    buffer_[seq] = std::move(data);
    return true;
}

std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>>
PacketBuffer::get_window(uint32_t start_seq, uint32_t end_seq) const {
    std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>> result;
    for (const auto& [seq, data] : buffer_) {
        if (seq >= start_seq && seq <= end_seq) {
            result.emplace_back(seq, &data);
        }
    }
    return result;
}

void PacketBuffer::ack_up_to(uint32_t last_acked) {
    has_acked_ = true;
    if (last_acked > last_acked_) {
        // Remove all entries with seq <= last_acked
        auto it = buffer_.begin();
        while (it != buffer_.end() && it->first <= last_acked) {
            it = buffer_.erase(it);
        }
        last_acked_ = last_acked;
    }
}

bool PacketBuffer::empty() const {
    return buffer_.empty();
}

void PacketBuffer::clear() {
    buffer_.clear();
    has_acked_    = false;
    last_acked_   = 0;
    expected_seq_ = 0;
}

size_t PacketBuffer::size() const {
    return buffer_.size();
}

// ---- Receive-side ----

bool PacketBuffer::insert(uint32_t seq, std::vector<uint8_t> data) {
    // Don't accept old seq
    if (seq < expected_seq_) return false;
    // Don't accept duplicate
    if (buffer_.find(seq) != buffer_.end()) return false;
    buffer_[seq] = std::move(data);
    return true;
}

std::optional<std::vector<uint8_t>> PacketBuffer::get_contiguous_from(uint32_t expected) {
    auto it = buffer_.find(expected);
    if (it == buffer_.end()) {
        return std::nullopt;
    }
    auto data = std::move(it->second);
    buffer_.erase(it);
    expected_seq_ = expected + 1;
    return data;
}

void PacketBuffer::advance_expected(uint32_t new_expected) {
    if (new_expected > expected_seq_) {
        // Remove all entries below new_expected
        auto it = buffer_.begin();
        while (it != buffer_.end() && it->first < new_expected) {
            it = buffer_.erase(it);
        }
        expected_seq_ = new_expected;
    }
}

} // namespace efly
