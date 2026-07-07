#include "efly/efly_api.h"
#include "efly/peer_discovery.h"
#include "efly/transfer_manager.h"
#include "efly/udp_server.h"

#include <boost/asio.hpp>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// ============================================================
// Global state
// ============================================================

namespace efly {

static std::unique_ptr<boost::asio::io_context> g_io_context;
static std::unique_ptr<std::thread>             g_io_thread;
static std::unique_ptr<UdpServer>               g_server;
static std::unique_ptr<PeerDiscovery>           g_discovery;
static std::unique_ptr<TransferManager>         g_transfer_manager;
static std::mutex                               g_init_mutex;
static bool                                     g_initialized = false;
static bool                                     g_running     = false;
static uint16_t                                 g_transfer_port = 0;

// ---- Callback storage ----
// Stored as raw C function pointers + user_data for FFI compatibility.
// The efly_api.cpp functions set these; internal components fire them.

static PeerFoundCallback         g_peer_found_cb    = nullptr;
static void*                     g_peer_found_ud    = nullptr;
static TransferAnnounceCallback  g_announce_cb      = nullptr;
static void*                     g_announce_ud      = nullptr;
static TransferProgressCallback  g_progress_cb      = nullptr;
static void*                     g_progress_ud      = nullptr;
static TransferCompleteCallback  g_complete_cb      = nullptr;
static void*                     g_complete_ud      = nullptr;
static ErrorCallback             g_error_cb         = nullptr;
static void*                     g_error_ud         = nullptr;

// ---- Callback fire helpers (called from io_context thread) ----

void fire_peer_found(const std::string& ip, uint16_t port) {
    if (g_peer_found_cb) {
        g_peer_found_cb(ip.c_str(), port, g_peer_found_ud);
    }
}

void fire_transfer_announce(uint32_t file_id, const std::string& filename,
                            uint64_t file_size, const std::string& from_ip) {
    if (g_announce_cb) {
        g_announce_cb(file_id, filename.c_str(), file_size, from_ip.c_str(), g_announce_ud);
    }
}

void fire_transfer_progress(uint32_t file_id, uint64_t total, uint64_t transferred) {
    if (g_progress_cb) {
        g_progress_cb(file_id, total, transferred, g_progress_ud);
    }
}

void fire_transfer_complete(uint32_t file_id, const std::string& saved_path) {
    if (g_complete_cb) {
        g_complete_cb(file_id, saved_path.c_str(), g_complete_ud);
    }
}

void fire_transfer_error(uint32_t file_id, int error_code, const std::string& message) {
    if (g_error_cb) {
        g_error_cb(file_id, error_code, message.c_str(), g_error_ud);
    }
}

// ---- Internal helpers to get our own IP ----

static std::string get_own_ip() {
    // Get the first non-loopback IPv4 address
    try {
        boost::asio::io_context io;
        boost::asio::ip::udp::resolver resolver(io);
        auto hostname = boost::asio::ip::host_name();
        auto results = resolver.resolve(hostname, "");
        for (const auto& entry : results) {
            auto addr = entry.endpoint().address();
            if (addr.is_v4() && !addr.is_loopback()) {
                return addr.to_string();
            }
        }
    } catch (...) {}
    return "127.0.0.1";
}

} // namespace efly

// ============================================================
// DllMain — Windows DLL entry point
// ============================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef SOCKET_ERROR  // conflicts with efly EflyError::SOCKET_ERROR enum

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            // Boost.Asio handles WSAStartup internally on Windows
            break;
        case DLL_PROCESS_DETACH:
            // Clean up if not already done
            efly_shutdown();
            break;
    }
    return TRUE;
}
#endif

// ============================================================
// C API: Lifecycle
// ============================================================

extern "C" EFL_API int efly_init(void) {
    using namespace efly;

    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (g_initialized) return 0;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    try {
        g_io_context = std::make_unique<boost::asio::io_context>();
        g_initialized = true;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[efly] init failed: " << e.what() << std::endl;
        return static_cast<int>(EflyError::UNKNOWN);
    }
}

extern "C" EFL_API void efly_shutdown(void) {
    using namespace efly;

    std::lock_guard<std::mutex> lock(g_init_mutex);

    // Stop components
    if (g_discovery) {
        g_discovery->stop();
        g_discovery.reset();
    }
    if (g_server) {
        g_server->stop();
        g_server.reset();
    }
    g_transfer_manager.reset();

    // Stop IO thread
    g_running = false;
    if (g_io_context) {
        g_io_context->stop();
    }
    if (g_io_thread && g_io_thread->joinable()) {
        g_io_thread->join();
    }
    g_io_thread.reset();
    g_io_context.reset();

    g_initialized = false;
    g_transfer_port = 0;
}

// ============================================================
// C API: Discovery
// ============================================================

extern "C" EFL_API int efly_start_discovery(uint16_t port) {
    using namespace efly;

    if (!g_initialized || !g_io_context) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }

    std::lock_guard<std::mutex> lock(g_init_mutex);

    g_transfer_port = port;

    // Create transfer UDP server
    try {
        g_server = std::make_unique<UdpServer>(*g_io_context, port);
    } catch (const std::exception& e) {
        std::cerr << "[efly] Failed to create UDP server on port " << port
                  << ": " << e.what() << std::endl;
        g_transfer_manager.reset();
        return static_cast<int>(EflyError::SOCKET_ERROR);
    }

    // Create transfer manager
    g_transfer_manager = std::make_unique<TransferManager>(*g_io_context);

    // Wire transfer manager sender to UDP server
    g_transfer_manager->set_sender([server = g_server.get()](
            const std::vector<uint8_t>& data,
            const boost::asio::ip::udp::endpoint& dest) {
        server->send_to(data, dest);
    });

    // Wire callbacks
    g_transfer_manager->set_announce_callback([](uint32_t fid, const std::string& name,
                                                  uint64_t size, const std::string& from) {
        fire_transfer_announce(fid, name, size, from);
    });
    g_transfer_manager->set_progress_callback([](uint32_t fid, uint64_t total, uint64_t xfer) {
        fire_transfer_progress(fid, total, xfer);
    });
    g_transfer_manager->set_complete_callback([](uint32_t fid, const std::string& path) {
        fire_transfer_complete(fid, path);
    });
    g_transfer_manager->set_error_callback([](uint32_t fid, int code, const std::string& msg) {
        fire_transfer_error(fid, code, msg);
    });

    // Wire UDP server to dispatch transfer packets to manager
    g_server->set_packet_callback([mgr = g_transfer_manager.get()](const Packet& pkt) {
        static int pkt_count = 0;
        pkt_count++;
        if (pkt_count <= 5 || pkt.type == PacketType::FILE_DATA)
            std::cout << "[UdpServer dispatch] type=" << (int)pkt.type
                      << " seq=" << pkt.seq << " count=" << pkt_count << std::endl;
        switch (pkt.type) {
            case PacketType::FILE_ANNOUNCE:
            case PacketType::FILE_ANNOUNCE_ACK:
            case PacketType::FILE_DATA:
            case PacketType::ACK:
            case PacketType::TRANSFER_COMPLETE:
            case PacketType::CANCEL:
                mgr->on_packet(pkt);
                break;
            default:
                break;
        }
    });

    // Create peer discovery
    g_discovery = std::make_unique<PeerDiscovery>(*g_io_context, port);

    // Wire peer found callback
    g_discovery->set_peer_found_callback([](const std::string& ip, uint16_t p) {
        fire_peer_found(ip, p);
    });

    // Start the UDP server
    g_server->start();

    // Start peer discovery
    g_discovery->start();
    if (!g_discovery->is_running()) {
        std::cerr << "[efly] Failed to start peer discovery on port "
                  << EFL_DISCOVERY_PORT << std::endl;
        g_discovery.reset();
        g_transfer_manager.reset();
        g_server->stop();
        g_server.reset();
        return static_cast<int>(EflyError::SOCKET_ERROR);
    }

    // Start IO thread if not running
    if (!g_running) {
        g_running = true;
        g_io_thread = std::make_unique<std::thread>([io = g_io_context.get()]() {
            try {
                // Use a work guard to keep the io_context alive
                auto work = boost::asio::make_work_guard(*io);
                io->run();
            } catch (const std::exception& e) {
                std::cerr << "[efly] IO thread error: " << e.what() << std::endl;
            }
        });
    }

    std::cout << "[efly] Discovery started on transfer port " << port << std::endl;
    return 0;
}

extern "C" EFL_API void efly_stop_discovery(void) {
    using namespace efly;

    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (g_discovery) {
        g_discovery->stop();
        g_discovery.reset();
    }
    if (g_transfer_manager) {
        g_transfer_manager.reset();
    }
    if (g_server) {
        g_server->stop();
        g_server.reset();
    }

    g_transfer_port = 0;
}

// ============================================================
// C API: File transfer
// ============================================================

extern "C" EFL_API int efly_send_file(
    const char* file_path,
    const char* remote_ip,
    uint16_t    remote_port,
    uint32_t*   out_file_id)
{
    using namespace efly;

    if (!g_initialized || !g_transfer_manager) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }
    if (!file_path || !remote_ip || !out_file_id) {
        return static_cast<int>(EflyError::INVALID_ARGUMENT);
    }

    std::string path(file_path);
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(remote_ip, ec);
    if (ec) {
        return static_cast<int>(EflyError::INVALID_ARGUMENT);
    }

    boost::asio::ip::udp::endpoint dest(addr, remote_port);

    // Post to IO thread and wait for result via promise/future
    std::promise<uint32_t> promise;
    auto future = promise.get_future();

    boost::asio::post(*g_io_context, [&]() {
        uint32_t fid = g_transfer_manager->start_send(path, dest);
        promise.set_value(fid);
    });

    uint32_t file_id = future.get();
    if (file_id == 0) {
        return static_cast<int>(EflyError::FILE_ERROR);
    }

    *out_file_id = file_id;
    return 0;
}

extern "C" EFL_API int efly_accept_transfer(uint32_t file_id, const char* save_dir) {
    using namespace efly;

    if (!g_initialized || !g_transfer_manager) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }
    if (!save_dir) {
        return static_cast<int>(EflyError::INVALID_ARGUMENT);
    }

    std::cout << "[efly] accept_transfer: posting to IO thread, file_id="
              << file_id << " save_dir=" << save_dir << std::endl;
    boost::asio::post(*g_io_context, [=]() {
        std::cout << "[efly] accept_transfer lambda: executing on IO thread"
                  << std::endl;
        g_transfer_manager->accept_transfer(file_id, std::string(save_dir));
    });

    return 0;
}

extern "C" EFL_API int efly_reject_transfer(uint32_t file_id) {
    using namespace efly;

    if (!g_initialized || !g_transfer_manager) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }

    boost::asio::post(*g_io_context, [=]() {
        g_transfer_manager->reject_transfer(file_id);
    });

    return 0;
}

extern "C" EFL_API int efly_cancel_transfer(uint32_t file_id) {
    using namespace efly;

    if (!g_initialized || !g_transfer_manager) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }

    boost::asio::post(*g_io_context, [=]() {
        g_transfer_manager->cancel_transfer(file_id);
    });

    return 0;
}

// ============================================================
// C API: Status query (thread-safe, no post needed)
// ============================================================

extern "C" EFL_API int efly_get_transfer_progress(
    uint32_t  file_id,
    uint64_t* out_total,
    uint64_t* out_transferred)
{
    using namespace efly;

    if (!g_initialized || !g_transfer_manager) {
        return static_cast<int>(EflyError::NOT_INITIALIZED);
    }
    if (!out_total || !out_transferred) {
        return static_cast<int>(EflyError::INVALID_ARGUMENT);
    }

    // This is safe to call from any thread since get_progress is const
    // and the map is not modified during reads (only through posted tasks)
    if (g_transfer_manager->get_progress(file_id, *out_total, *out_transferred)) {
        return 0;
    }
    return static_cast<int>(EflyError::INVALID_ARGUMENT);
}

// ============================================================
// C API: Callback registration (set before start_discovery)
// ============================================================

extern "C" EFL_API void efly_set_peer_found_callback(PeerFoundCallback cb, void* user_data) {
    efly::g_peer_found_cb = cb;
    efly::g_peer_found_ud = user_data;
}

extern "C" EFL_API void efly_set_transfer_announce_callback(TransferAnnounceCallback cb, void* user_data) {
    efly::g_announce_cb = cb;
    efly::g_announce_ud = user_data;
}

extern "C" EFL_API void efly_set_transfer_progress_callback(TransferProgressCallback cb, void* user_data) {
    efly::g_progress_cb = cb;
    efly::g_progress_ud = user_data;
}

extern "C" EFL_API void efly_set_transfer_complete_callback(TransferCompleteCallback cb, void* user_data) {
    efly::g_complete_cb = cb;
    efly::g_complete_ud = user_data;
}

extern "C" EFL_API void efly_set_error_callback(ErrorCallback cb, void* user_data) {
    efly::g_error_cb = cb;
    efly::g_error_ud = user_data;
}
