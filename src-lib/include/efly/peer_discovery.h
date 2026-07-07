#pragma once

#include "protocol_handler.h"

#include <array>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>

namespace efly {

namespace ip = boost::asio::ip;

// ============================================================
// PeerDiscovery — LAN broadcast beacon + listener
// ============================================================

/// Callback type for peer found events.
using PeerFoundFunc = std::function<void(const std::string& ip, uint16_t port)>;

class PeerDiscovery {
public:
    PeerDiscovery(boost::asio::io_context& io, uint16_t transfer_port);
    ~PeerDiscovery();

    PeerDiscovery(const PeerDiscovery&) = delete;
    PeerDiscovery& operator=(const PeerDiscovery&) = delete;

    /// Start broadcasting and listening on the discovery port.
    void start();

    /// Stop all discovery activity.
    void stop();

    /// Set the callback for when a peer is found.
    void set_peer_found_callback(PeerFoundFunc cb) { on_peer_found_ = std::move(cb); }

    /// Check if discovery is running.
    bool is_running() const { return running_; }

private:
    // Beacon (send broadcast)
    void do_beacon();
    void on_beacon_timer(boost::system::error_code ec);

    // Async receive loop
    void do_receive();
    void on_receive(boost::system::error_code ec, std::size_t bytes);

    // Process a decoded packet from the receive loop
    void process_packet(const Packet& pkt);

    boost::asio::io_context&     io_;
    boost::asio::steady_timer    beacon_timer_;
    ip::udp::socket              socket_;
    uint16_t                     transfer_port_;
    PeerFoundFunc                on_peer_found_;
    bool                         running_ = false;

    std::array<uint8_t, EFL_MAX_PACKET> recv_buf_{};
    ip::udp::endpoint                  sender_ep_;

    static constexpr auto BEACON_INTERVAL = std::chrono::seconds(2);
};

} // namespace efly
