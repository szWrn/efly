#pragma once

#include "protocol_handler.h"

#include <array>
#include <boost/asio.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace efly {

namespace ip = boost::asio::ip;

// ============================================================
// UdpServer — async UDP socket using Boost.Asio
// ============================================================

/// Callback type for received packets.
using PacketCallback = std::function<void(const Packet& pkt)>;

class UdpServer {
public:
    UdpServer(boost::asio::io_context& io, uint16_t port);
    ~UdpServer();

    // Non-copyable
    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    /// Start the async receive loop.
    void start();

    /// Stop the server and close the socket.
    void stop();

    /// Send a raw packet synchronously to a destination.
    /// Must be called from the io_context thread.
    void send_to(const std::vector<uint8_t>& data,
                 const ip::udp::endpoint&   dest);

    /// Send a Packet (encoded) to a destination.
    void send_packet(const Packet& pkt, const ip::udp::endpoint& dest);

    /// Register handler for incoming packets.
    void set_packet_callback(PacketCallback cb) { packet_cb_ = std::move(cb); }

    /// The port this server is bound to.
    uint16_t port() const { return port_; }

    /// The underlying io_context.
    boost::asio::io_context& io() { return io_; }

    /// Get a broadcast endpoint for the given port.
    static ip::udp::endpoint broadcast_endpoint(uint16_t port);

private:
    void do_receive();
    void on_receive(boost::system::error_code ec, std::size_t bytes);

    boost::asio::io_context& io_;
    ip::udp::socket           socket_;
    uint16_t                  port_ = 0;
    std::array<uint8_t, EFL_MAX_PACKET> recv_buf_{};
    ip::udp::endpoint         sender_ep_;
    PacketCallback            packet_cb_;
    bool                      running_ = false;
};

} // namespace efly
