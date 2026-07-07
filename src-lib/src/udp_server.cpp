#include "efly/udp_server.h"

#include <iostream>

namespace efly {

UdpServer::UdpServer(boost::asio::io_context& io, uint16_t port)
    : io_(io)
    , socket_(io)
    , port_(port)
{
    ip::udp::endpoint ep(ip::udp::v4(), port);
    socket_.open(ep.protocol());
    socket_.set_option(ip::udp::socket::reuse_address(true));
    socket_.set_option(boost::asio::socket_base::broadcast(true));
    socket_.bind(ep);
}

UdpServer::~UdpServer() {
    stop();
}

void UdpServer::start() {
    if (running_) return;
    running_ = true;
    do_receive();
}

void UdpServer::stop() {
    running_ = false;
    boost::system::error_code ec;
    socket_.close(ec);
}

void UdpServer::send_to(const std::vector<uint8_t>& data, const ip::udp::endpoint& dest) {
    static int sc = 0;
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(data), dest, 0, ec);
    sc++;
    if (ec) {
        std::cerr << "[UdpServer] send_to #" << sc << " error code=" << ec.value()
                  << ": " << ec.message() << std::endl;
    } else if (sc <= 20) {
        std::cout << "[UdpServer] send_to #" << sc << " OK, " << data.size()
                  << " bytes to " << dest.address().to_string() << ":" << dest.port() << std::endl;
    }
}

void UdpServer::send_packet(const Packet& pkt, const ip::udp::endpoint& dest) {
    auto encoded = ProtocolHandler::encode(pkt);
    send_to(encoded, dest);
}

ip::udp::endpoint UdpServer::broadcast_endpoint(uint16_t port) {
    return ip::udp::endpoint(ip::address_v4::broadcast(), port);
}

void UdpServer::do_receive() {
    if (!running_) return;

    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        sender_ep_,
        [this](boost::system::error_code ec, std::size_t bytes) {
            on_receive(ec, bytes);
        });
}

void UdpServer::on_receive(boost::system::error_code ec, std::size_t bytes) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return; // socket closed
        }
        std::cerr << "[UdpServer] receive error: " << ec.message() << std::endl;
        do_receive(); // continue receiving
        return;
    }

    if (bytes > 0) {
        auto pkt = ProtocolHandler::decode(recv_buf_.data(), bytes);
        if (pkt) {
            pkt->from_ip   = sender_ep_.address().to_string();
            pkt->from_port = sender_ep_.port();

            if (packet_cb_) {
                packet_cb_(*pkt);
            }
        }
    }

    do_receive(); // loop
}

} // namespace efly
