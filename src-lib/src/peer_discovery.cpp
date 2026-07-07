#include "efly/peer_discovery.h"

#include <iostream>

namespace efly {

PeerDiscovery::PeerDiscovery(boost::asio::io_context& io, uint16_t transfer_port)
    : io_(io)
    , beacon_timer_(io)
    , socket_(io)
    , transfer_port_(transfer_port)
{
}

PeerDiscovery::~PeerDiscovery() {
    stop();
}

void PeerDiscovery::start() {
    if (running_) return;

    try {
        ip::udp::endpoint ep(ip::udp::v4(), EFL_DISCOVERY_PORT);
        socket_.open(ep.protocol());
        socket_.set_option(ip::udp::socket::reuse_address(true));
        socket_.set_option(boost::asio::socket_base::broadcast(true));
        socket_.bind(ep);
    } catch (const std::exception& e) {
        std::cerr << "[PeerDiscovery] Failed to bind to port "
                  << EFL_DISCOVERY_PORT << ": " << e.what() << std::endl;
        return;
    }

    running_ = true;
    do_receive();
    do_beacon();

    std::cout << "[PeerDiscovery] Started on port " << EFL_DISCOVERY_PORT
              << ", transfer port " << transfer_port_ << std::endl;
}

void PeerDiscovery::stop() {
    running_ = false;
    beacon_timer_.cancel();
    boost::system::error_code ec;
    socket_.close(ec);
}

// ============================================================
// Beacon (send broadcast)
// ============================================================

void PeerDiscovery::do_beacon() {
    if (!running_) return;

    beacon_timer_.expires_after(BEACON_INTERVAL);
    beacon_timer_.async_wait([this](boost::system::error_code ec) {
        on_beacon_timer(ec);
    });
}

void PeerDiscovery::on_beacon_timer(boost::system::error_code ec) {
    if (ec == boost::asio::error::operation_aborted) return;
    if (!running_) return;

    // Build and send DISCOVERY broadcast
    Packet pkt;
    pkt.type    = PacketType::DISCOVERY;
    pkt.seq     = 0;
    pkt.payload = DiscoveryPayload{transfer_port_};

    auto encoded = ProtocolHandler::encode(pkt);
    auto dest = ip::udp::endpoint(ip::address_v4::broadcast(), EFL_DISCOVERY_PORT);

    boost::system::error_code send_ec;
    socket_.send_to(boost::asio::buffer(encoded), dest, 0, send_ec);
    if (send_ec) {
        std::cerr << "[PeerDiscovery] beacon send error: " << send_ec.message() << std::endl;
    }

    // Schedule next beacon
    do_beacon();
}

// ============================================================
// Async receive loop
// ============================================================

void PeerDiscovery::do_receive() {
    if (!running_) return;

    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        sender_ep_,
        [this](boost::system::error_code ec, std::size_t bytes) {
            on_receive(ec, bytes);
        });
}

void PeerDiscovery::on_receive(boost::system::error_code ec, std::size_t bytes) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) return;
        std::cerr << "[PeerDiscovery] receive error: " << ec.message() << std::endl;
        do_receive();
        return;
    }

    if (bytes > 0) {
        auto pkt = ProtocolHandler::decode(recv_buf_.data(), bytes);
        if (pkt) {
            pkt->from_ip   = sender_ep_.address().to_string();
            pkt->from_port = sender_ep_.port();
            process_packet(*pkt);
        }
    }

    do_receive();
}

// ============================================================
// Packet processing
// ============================================================

void PeerDiscovery::process_packet(const Packet& pkt) {
    if (pkt.type == PacketType::DISCOVERY) {
        const auto& payload = std::get<DiscoveryPayload>(pkt.payload);

        // Notify about the peer
        if (on_peer_found_) {
            on_peer_found_(pkt.from_ip, payload.port);
        }

        // Send DISCOVERY_RESP back to sender
        Packet resp;
        resp.type    = PacketType::DISCOVERY_RESP;
        resp.seq     = 0;
        resp.payload = DiscoveryRespPayload{transfer_port_};

        auto encoded = ProtocolHandler::encode(resp);
        ip::udp::endpoint dest(ip::make_address(pkt.from_ip), pkt.from_port);

        boost::system::error_code send_ec;
        socket_.send_to(boost::asio::buffer(encoded), dest, 0, send_ec);
        if (send_ec) {
            std::cerr << "[PeerDiscovery] response send error: " << send_ec.message() << std::endl;
        }
    }
    else if (pkt.type == PacketType::DISCOVERY_RESP) {
        const auto& payload = std::get<DiscoveryRespPayload>(pkt.payload);

        if (on_peer_found_) {
            on_peer_found_(pkt.from_ip, payload.port);
        }
    }
}

} // namespace efly
