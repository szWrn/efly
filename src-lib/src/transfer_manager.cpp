#include "efly/transfer_manager.h"

#include <boost/asio.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace efly {

using namespace std::chrono_literals;
namespace ip = boost::asio::ip;

TransferManager::TransferManager(boost::asio::io_context& io) : io_(io) {}

TransferManager::~TransferManager() {
    for (auto& [id, _] : send_sessions_) cancel_transfer(id);
    for (auto& [id, _] : recv_sessions_) cancel_transfer(id);
    send_sessions_.clear();
    recv_sessions_.clear();
}

// ============================================================
// Packet dispatch
// ============================================================

void TransferManager::on_packet(const Packet& pkt) {
    switch (pkt.type) {
        case PacketType::FILE_ANNOUNCE:      handle_file_announce(pkt);      break;
        case PacketType::FILE_ANNOUNCE_ACK:  handle_file_announce_ack(pkt);  break;
        case PacketType::FILE_DATA:          handle_file_data(pkt);          break;
        case PacketType::ACK:                handle_ack(pkt);                break;
        case PacketType::TRANSFER_COMPLETE:  handle_transfer_complete(pkt);  break;
        case PacketType::CANCEL:             handle_cancel(pkt);             break;
        default: break;
    }
}

// ============================================================
// Sender API
// ============================================================

uint32_t TransferManager::start_send(const std::string& file_path,
                                      const ip::udp::endpoint& dest) {
    uint32_t file_id = generate_file_id();

    auto s = std::make_unique<TransferSession>();
    s->state   = TransferState::ANNOUNCING;
    s->file_id = file_id;
    s->peer    = dest;

    s->reader = std::make_unique<FileReader>();
    if (!s->reader->open(file_path)) {
        fire_error(file_id, static_cast<int>(EflyError::FILE_ERROR),
                   "Failed to open file: " + file_path);
        return 0;
    }
    s->total_size = s->reader->size();
    s->file_name  = std::filesystem::path(file_path).filename().string();
    s->ack_timer  = std::make_unique<boost::asio::steady_timer>(io_);

    send_sessions_[file_id] = std::move(s);

    Packet announce;
    announce.type    = PacketType::FILE_ANNOUNCE;
    announce.seq     = 0;
    announce.payload = FileAnnouncePayload{file_id,
        send_sessions_[file_id]->total_size,
        send_sessions_[file_id]->file_name};
    send_packet(announce, dest);

    start_ack_timer(*send_sessions_[file_id]);

    std::cout << "[TransferManager] Announcing: " << send_sessions_[file_id]->file_name
              << " (" << send_sessions_[file_id]->total_size << " bytes) to "
              << dest.address().to_string() << ":" << dest.port() << std::endl;
    return file_id;
}

// ============================================================
// Receiver API
// ============================================================

void TransferManager::accept_transfer(uint32_t file_id, const std::string& save_dir) {
    std::cout << "[TransferManager] accept_transfer called: file_id=" << file_id
              << " recv_sessions count=" << recv_sessions_.size() << std::endl;
    auto it = recv_sessions_.find(file_id);
    if (it == recv_sessions_.end()) {
        std::cout << "[TransferManager] accept_transfer: session NOT FOUND for " << file_id << std::endl;
        return;
    }
    auto& s = *it->second;
    if (s.state != TransferState::ANNOUNCING) {
        std::cout << "[TransferManager] accept_transfer: state is " << (int)s.state
                  << " (expected " << (int)TransferState::ANNOUNCING << ")" << std::endl;
        return;
    }

    // Send ACK
    Packet ack;
    ack.type    = PacketType::FILE_ANNOUNCE_ACK;
    ack.seq     = 0;
    ack.payload = FileAnnounceAckPayload{file_id, true};
    send_packet(ack, s.peer);

    std::string save_path = save_dir + "/" + s.file_name;
    std::cout << "[TransferManager] Accepting transfer " << file_id
              << ", saving to " << save_path
              << ", sending ACK to " << s.peer.address().to_string()
              << ":" << s.peer.port() << std::endl;
    s.writer = std::make_unique<FileWriter>();
    if (!s.writer->open(save_path, s.total_size)) {
        fire_error(file_id, static_cast<int>(EflyError::FILE_ERROR),
                   "Failed to create file: " + save_path + s.writer->last_error());
        recv_sessions_.erase(file_id);
        return;
    }
    s.save_path      = save_path;
    s.state          = TransferState::TRANSFERRING;
    s.recv_expected  = 0;
    s.recv_timer     = std::make_unique<boost::asio::steady_timer>(io_);
    start_recv_timer(s);
}

void TransferManager::reject_transfer(uint32_t file_id) {
    auto it = recv_sessions_.find(file_id);
    if (it == recv_sessions_.end()) return;

    Packet pkt;
    pkt.type    = PacketType::FILE_ANNOUNCE_ACK;
    pkt.seq     = 0;
    pkt.payload = FileAnnounceAckPayload{file_id, false};
    send_packet(pkt, it->second->peer);

    recv_sessions_.erase(it);
}

// ============================================================
// Cancel
// ============================================================

void TransferManager::cancel_transfer(uint32_t file_id) {
    // Check both maps
    TransferSession* s = nullptr;
    bool is_send = false;

    auto sit = send_sessions_.find(file_id);
    if (sit != send_sessions_.end()) { s = sit->second.get(); is_send = true; }
    else {
        auto rit = recv_sessions_.find(file_id);
        if (rit != recv_sessions_.end()) s = rit->second.get();
    }
    if (!s) return;

    // Notify peer
    Packet pkt;
    pkt.type    = PacketType::CANCEL;
    pkt.seq     = 0;
    pkt.payload = CancelPayload{file_id, CancelReason::USER};
    send_packet(pkt, s->peer);

    // Cancel timers
    if (s->ack_timer)  s->ack_timer->cancel();
    if (s->recv_timer) s->recv_timer->cancel();

    // Clean up files
    if (s->reader) s->reader->close();
    if (s->writer) { s->writer->close(); std::error_code ec; std::filesystem::remove(s->save_path, ec); }

    remove_session(file_id);
    fire_error(file_id, static_cast<int>(EflyError::CANCELLED), "Transfer cancelled");
}

// ============================================================
// Progress
// ============================================================

bool TransferManager::get_progress(uint32_t file_id, uint64_t& total, uint64_t& transferred) const {
    // Check both maps
    auto sit = send_sessions_.find(file_id);
    if (sit != send_sessions_.end()) {
        total = sit->second->total_size;
        transferred = sit->second->has_acked
            ? static_cast<uint64_t>(sit->second->last_acked + 1) * EFL_CHUNK_SIZE
            : 0;
        if (transferred > total) transferred = total;
        return true;
    }
    auto rit = recv_sessions_.find(file_id);
    if (rit != recv_sessions_.end()) {
        total = rit->second->total_size;
        transferred = rit->second->writer ? rit->second->writer->bytes_written() : 0;
        return true;
    }
    return false;
}

// ============================================================
// Packet handlers
// ============================================================

void TransferManager::handle_file_announce(const Packet& pkt) {
    auto& payload = std::get<FileAnnouncePayload>(pkt.payload);

    auto s = std::make_unique<TransferSession>();
    s->state      = TransferState::ANNOUNCING;
    s->file_id    = payload.file_id;
    s->total_size = payload.file_size;
    s->file_name  = payload.filename;
    s->peer       = ip::udp::endpoint(ip::make_address(pkt.from_ip), pkt.from_port);

    uint32_t fid = payload.file_id;
    recv_sessions_[fid] = std::move(s);

    if (on_announce_)
        on_announce_(fid, payload.filename, payload.file_size, pkt.from_ip);
}

void TransferManager::handle_file_announce_ack(const Packet& pkt) {
    auto& payload = std::get<FileAnnounceAckPayload>(pkt.payload);
    auto it = send_sessions_.find(payload.file_id);
    if (it == send_sessions_.end()) return;
    auto& s = *it->second;

    if (s.state != TransferState::ANNOUNCING) return;
    if (s.ack_timer) s.ack_timer->cancel();

    if (!payload.accepted) {
        std::cout << "[TransferManager] Announce ACK: file " << payload.file_id
                  << " REJECTED" << std::endl;
        fire_error(payload.file_id, static_cast<int>(EflyError::REJECTED),
                   "Transfer rejected by peer");
        send_sessions_.erase(it);
        return;
    }

    std::cout << "[TransferManager] Announce ACK accepted for file "
              << payload.file_id << ", starting data transfer" << std::endl;
    s.state           = TransferState::TRANSFERRING;
    s.has_acked       = false;
    s.last_acked      = 0;
    s.next_seq        = 0;
    s.retry_count     = 0;
    s.all_chunks_sent = false;
    fill_and_send_window(s);
    // Start ACK timer for the first window (subsequent windows start it in handle_ack)
    if (!s.send_buf.empty())
        start_ack_timer(s);
}

void TransferManager::handle_file_data(const Packet& pkt) {
    auto& payload = std::get<FileDataPayload>(pkt.payload);
    static int dc = 0;
    if (++dc <= 3) std::cout << "[TransferManager] handle_file_data: file_id="
        << payload.file_id << " seq=" << pkt.seq << " offset="
        << payload.chunk_offset << " len=" << payload.data.size()
        << " recv_sessions=" << recv_sessions_.size() << std::endl;
    auto it = recv_sessions_.find(payload.file_id);
    if (it == recv_sessions_.end()) {
        if (dc <= 3) std::cout << "[TransferManager] handle_file_data: session NOT FOUND" << std::endl;
        return;
    }
    auto& s = *it->second;

    if (s.state != TransferState::TRANSFERRING) return;

    if (s.recv_timer) { s.recv_timer->cancel(); start_recv_timer(s); }

    if (!s.recv_buf.insert(pkt.seq, std::move(payload.data))) return; // duplicate

    write_contiguous(s);
    send_ack(s);

    if (on_progress_ && s.writer)
        on_progress_(s.file_id, s.total_size, s.writer->bytes_written());
}

void TransferManager::handle_ack(const Packet& pkt) {
    auto& payload = std::get<AckPayload>(pkt.payload);
    auto it = send_sessions_.find(payload.file_id);
    if (it == send_sessions_.end()) return;
    auto& s = *it->second;

    if (s.state != TransferState::TRANSFERRING) return;
    if (s.ack_timer) s.ack_timer->cancel();

    // Only accept ACKs that represent progress
    if (!s.has_acked || payload.ack_seq > s.last_acked) {
        s.has_acked  = true;
        s.last_acked = payload.ack_seq;
    }
    s.retry_count = 0;
    s.send_buf.ack_up_to(payload.ack_seq);

    if (on_progress_) {
        uint64_t xfer = static_cast<uint64_t>(s.last_acked + 1) * EFL_CHUNK_SIZE;
        if (xfer > s.total_size) xfer = s.total_size;
        on_progress_(s.file_id, s.total_size, xfer);
    }

    if (s.all_chunks_sent && s.send_buf.empty()) {
        Packet done;
        done.type    = PacketType::TRANSFER_COMPLETE;
        done.seq     = 0;
        done.payload = TransferCompletePayload{s.file_id};
        send_packet(done, s.peer);
        s.state = TransferState::COMPLETE;
        if (on_complete_) on_complete_(s.file_id, s.reader->path());
        s.reader->close();
        send_sessions_.erase(it);
        return;
    }

    fill_and_send_window(s);
    if (!s.send_buf.empty()) start_ack_timer(s);
}

void TransferManager::handle_transfer_complete(const Packet& pkt) {
    auto& payload = std::get<TransferCompletePayload>(pkt.payload);
    auto it = recv_sessions_.find(payload.file_id);
    if (it == recv_sessions_.end()) return;
    auto& s = *it->second;

    if (s.state != TransferState::TRANSFERRING) return;
    if (s.recv_timer) s.recv_timer->cancel();

    s.state = TransferState::COMPLETE;
    if (s.writer) s.writer->close();

    if (on_complete_) on_complete_(s.file_id, s.save_path);
    recv_sessions_.erase(it);
}

void TransferManager::handle_cancel(const Packet& pkt) {
    auto& payload = std::get<CancelPayload>(pkt.payload);

    // Check both maps
    TransferSession* s = nullptr;
    auto sit = send_sessions_.find(payload.file_id);
    auto rit = recv_sessions_.find(payload.file_id);

    if (sit != send_sessions_.end()) s = sit->second.get();
    else if (rit != recv_sessions_.end()) s = rit->second.get();
    if (!s) return;

    if (s->ack_timer)  s->ack_timer->cancel();
    if (s->recv_timer) s->recv_timer->cancel();
    if (s->reader) s->reader->close();
    if (s->writer) { s->writer->close(); std::error_code ec; std::filesystem::remove(s->save_path, ec); }

    fire_error(payload.file_id, static_cast<int>(EflyError::CANCELLED),
               "Transfer cancelled by peer");
    remove_session(payload.file_id);
}

// ============================================================
// Send helpers
// ============================================================

void TransferManager::fill_and_send_window(TransferSession& s) {
    if (s.all_chunks_sent) return;

    uint32_t total_chunks = static_cast<uint32_t>(
        (s.total_size + EFL_CHUNK_SIZE - 1) / EFL_CHUNK_SIZE);

    static bool first_window = true;
    if (first_window) {
        std::cout << "[TransferManager] Sending first window: total_chunks="
                  << total_chunks << ", file_size=" << s.total_size << std::endl;
        first_window = false;
    }

    for (uint32_t i = 0; i < EFL_WINDOW_SIZE; i++) {
        uint32_t seq    = s.next_seq;
        uint64_t offset = static_cast<uint64_t>(seq) * EFL_CHUNK_SIZE;
        if (offset >= s.total_size) { s.all_chunks_sent = true; break; }

        size_t remaining = static_cast<size_t>(s.total_size - offset);
        size_t chunk_len = (std::min)(remaining, EFL_CHUNK_SIZE);
        auto data = s.reader->read_chunk(offset, chunk_len);
        if (!data) {
            fire_error(s.file_id, static_cast<int>(EflyError::FILE_ERROR),
                       "Failed to read file chunk");
            cancel_transfer(s.file_id);
            return;
        }

        Packet pkt;
        pkt.type    = PacketType::FILE_DATA;
        pkt.seq     = seq;
        pkt.payload = FileDataPayload{s.file_id, static_cast<uint32_t>(offset), std::move(*data)};

        auto encoded = ProtocolHandler::encode(pkt);
        if (!s.send_buf.push(seq, std::move(encoded))) break;
        s.next_seq = seq + 1;
    }
    send_window(s);
}

void TransferManager::send_window(TransferSession& s) {
    // start_seq: 0 on first send (nothing acked), otherwise last_acked + 1
    uint32_t start_seq = s.has_acked ? s.last_acked + 1 : 0;
    uint32_t end_seq   = start_seq + EFL_WINDOW_SIZE - 1;
    auto packets = s.send_buf.get_window(start_seq, end_seq);
    if (packets.empty()) return;
    for (const auto& [seq, data] : packets)
        sender_(std::vector<uint8_t>(*data), s.peer);
}

void TransferManager::start_ack_timer(TransferSession& s) {
    if (!s.ack_timer) return;
    uint32_t gen = ++s.timer_gen;
    s.ack_timer->expires_after(std::chrono::milliseconds(EFL_ACK_TIMEOUT));
    uint32_t fid = s.file_id;
    s.ack_timer->async_wait([this, fid, gen](boost::system::error_code ec) {
        if (ec) return; // cancelled
        auto it = send_sessions_.find(fid);
        if (it == send_sessions_.end()) return;
        if (it->second->timer_gen != gen) return; // stale
        on_ack_timeout(fid);
    });
}

void TransferManager::on_ack_timeout(uint32_t file_id) {
    auto it = send_sessions_.find(file_id);
    if (it == send_sessions_.end()) return;
    auto& s = *it->second;

    // If transfer is already done (ACK arrived just as timer fired), skip
    if (s.all_chunks_sent && s.send_buf.empty()) return;

    if (s.state != TransferState::TRANSFERRING) return;

    if (++s.retry_count > EFL_MAX_RETRIES) {
        fire_error(file_id, static_cast<int>(EflyError::TIMEOUT),
                   "Transfer timed out waiting for ACKs");
        cancel_transfer(file_id);
        return;
    }
    send_window(s);
    start_ack_timer(s);
}

// ============================================================
// Receive helpers
// ============================================================

void TransferManager::send_ack(const TransferSession& s) {
    Packet pkt;
    pkt.type = PacketType::ACK;
    pkt.seq  = 0;
    uint32_t ack_val = s.recv_buf.next_expected() > 0
                       ? s.recv_buf.next_expected() - 1 : 0;
    pkt.payload = AckPayload{s.file_id, ack_val};
    send_packet(pkt, s.peer);
}

void TransferManager::write_contiguous(TransferSession& s) {
    while (true) {
        auto data = s.recv_buf.get_contiguous_from(s.recv_expected);
        if (!data) break;
        if (s.writer && !data->empty()) {
            uint64_t off = static_cast<uint64_t>(s.recv_expected) * EFL_CHUNK_SIZE;
            if (!s.writer->write_chunk(off, data->data(), data->size())) {
                fire_error(s.file_id, static_cast<int>(EflyError::FILE_ERROR),
                           "Disk write error");
                cancel_transfer(s.file_id);
                return;
            }
        }
        s.recv_expected++;
    }
}

void TransferManager::start_recv_timer(TransferSession& s) {
    if (!s.recv_timer) return;
    s.recv_timer->expires_after(30s);
    uint32_t fid = s.file_id;
    s.recv_timer->async_wait([this, fid](boost::system::error_code ec) {
        if (!ec) on_recv_timeout(fid);
    });
}

void TransferManager::on_recv_timeout(uint32_t file_id) {
    auto it = recv_sessions_.find(file_id);
    if (it == recv_sessions_.end()) return;
    fire_error(file_id, static_cast<int>(EflyError::TIMEOUT),
               "Receive timed out — no data received");
    cancel_transfer(file_id);
}

// ============================================================
// Utilities
// ============================================================

uint32_t TransferManager::generate_file_id() {
    return next_id_++;
}

void TransferManager::remove_session(uint32_t file_id) {
    send_sessions_.erase(file_id);
    recv_sessions_.erase(file_id);
}

TransferSession* TransferManager::find_session(uint32_t file_id) {
    auto sit = send_sessions_.find(file_id);
    if (sit != send_sessions_.end()) return sit->second.get();
    auto rit = recv_sessions_.find(file_id);
    if (rit != recv_sessions_.end()) return rit->second.get();
    return nullptr;
}

void TransferManager::fire_error(uint32_t file_id, int code, const std::string& msg) {
    if (on_error_) on_error_(file_id, code, msg);
}

void TransferManager::send_packet(const Packet& pkt, const ip::udp::endpoint& dest) {
    if (sender_) {
        auto encoded = ProtocolHandler::encode(pkt);
        sender_(encoded, dest);
    }
}

} // namespace efly
