/**
 * efly-cli — interactive command-line file sharing tool
 *
 * Uses efly_lib.dll to discover LAN peers and transfer files via UDP.
 *
 * Build: cd tools && cmake -S . -B build -G "Visual Studio 17 2022" -A x64
 *        cmake --build build --config Release
 * Run:   tools/build/Release/efly_cli.exe
 */

#include "efly/efly_api.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// shared state
// ============================================================

static std::mutex              g_mtx;
static std::condition_variable g_cv;
static bool                    g_running = true;

// discovered peers  (ip:port -> name)
struct Peer {
    std::string ip;
    uint16_t    port;
    int         seen_count = 1;
};
static std::map<std::string, Peer> g_peers;  // key: "ip:port"

// active transfers
struct Xfer {
    uint32_t id;
    std::string filename;
    uint64_t size;
    uint64_t transferred;
    std::string peer_ip;
    bool     is_sender;
    bool     complete = false;
    bool     error    = false;
    std::string error_msg;
};
static std::map<uint32_t, Xfer> g_xfers;

// incoming announcements waiting for user decision
struct Announce {
    uint32_t    id;
    std::string filename;
    uint64_t    size;
    std::string from_ip;
};
static std::vector<Announce> g_announces;

static std::string g_download_dir = ".";

// ============================================================
// helpers
// ============================================================

static std::string fmt_size(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    double s = static_cast<double>(bytes);
    while (s >= 1024.0 && u < 4) { s /= 1024.0; u++; }
    std::ostringstream ss;
    ss.precision(u > 0 ? 2 : 0);
    ss << std::fixed << s << " " << units[u];
    return ss.str();
}

static void ensure_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
}

// ============================================================
// callbacks
// ============================================================

static void cb_peer(const char* ip, uint16_t port, void*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::string key = std::string(ip) + ":" + std::to_string(port);
    auto it = g_peers.find(key);
    if (it == g_peers.end()) {
        g_peers[key] = {ip, port, 1};
        std::cout << "\n  [discovered] " << ip << ":" << port << std::endl;
    } else {
        it->second.seen_count++;
    }
    g_cv.notify_all();
}

static void cb_announce(uint32_t id, const char* name, uint64_t size,
                         const char* from, void*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_announces.push_back({id, name, size, from});
    g_xfers[id] = {id, name, size, 0, from, false};
    std::cout << "\n  [incoming] #" << id << "  " << name
              << "  (" << fmt_size(size) << ")  from " << from << std::endl;
    std::cout << "  Type 'accept " << id << "' or 'reject " << id << "'" << std::endl;
    g_cv.notify_all();
}

static void cb_progress(uint32_t id, uint64_t total, uint64_t xfer, void*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_xfers.find(id);
    if (it != g_xfers.end()) {
        it->second.size        = total;
        it->second.transferred = xfer;
    }
}

static void cb_complete(uint32_t id, const char* path, void*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_xfers.find(id);
    if (it != g_xfers.end()) {
        it->second.complete = true;
    }
    std::cout << "\n  [completed] #" << id << "  ->  " << path << std::endl;
    g_cv.notify_all();
}

static void cb_error(uint32_t id, int code, const char* msg, void*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_xfers.find(id);
    if (it != g_xfers.end()) {
        it->second.error     = true;
        it->second.error_msg = msg;
    }
    std::cout << "\n  [error] #" << id << "  code=" << code
              << "  " << (msg ? msg : "") << std::endl;
    g_cv.notify_all();
}

// ============================================================
// command handlers
// ============================================================

static void cmd_help() {
    std::cout << R"(
Commands:
  discover <port>   start LAN discovery on the given UDP port
  stop              stop discovery
  peers             list discovered peers
  send <file> <n>   send <file> to peer index <n> (see 'peers')
  accept <id>       accept incoming transfer <id>
  reject <id>       reject incoming transfer <id>
  progress          show all active transfers
  cancel <id>       cancel transfer <id>
  dir <path>        set download directory (default: .)
  clear             clear finished/failed transfers from list
  quit / exit       shutdown and exit
  help              show this message
)" << std::endl;
}

static void cmd_discover(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: discover <port>" << std::endl;
        return;
    }
    uint16_t port = static_cast<uint16_t>(std::stoul(args[1]));
    if (efly_start_discovery(port) != 0) {
        std::cout << "Failed to start discovery on port " << port << std::endl;
        return;
    }
    std::cout << "Discovery started on port " << port << std::endl;
    std::cout << "Download directory: " << g_download_dir << std::endl;
}

static void cmd_stop() {
    efly_stop_discovery();
    std::lock_guard<std::mutex> lk(g_mtx);
    g_peers.clear();
    g_announces.clear();
    g_xfers.clear();
    std::cout << "Discovery stopped." << std::endl;
}

static void cmd_peers() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_peers.empty()) {
        std::cout << "No peers discovered yet." << std::endl;
        return;
    }
    std::cout << "Discovered peers:" << std::endl;
    int i = 0;
    for (const auto& [key, p] : g_peers) {
        std::cout << "  [" << i << "] " << p.ip << ":" << p.port
                  << "  (seen " << p.seen_count << "x)" << std::endl;
        i++;
    }
}

static void cmd_send(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Usage: send <file_path> <peer_index>" << std::endl;
        std::cout << "  Use 'peers' to see available peers." << std::endl;
        return;
    }

    std::string file_path = args[1];

    // resolve relative path
    if (!fs::exists(file_path)) {
        std::cout << "File not found: " << file_path << std::endl;
        return;
    }

    int idx = std::stoi(args[2]);

    std::string ip;
    uint16_t port = 0;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        int i = 0;
        for (const auto& [key, p] : g_peers) {
            if (i == idx) { ip = p.ip; port = p.port; break; }
            i++;
        }
    }

    if (ip.empty()) {
        std::cout << "Invalid peer index. Use 'peers' to list." << std::endl;
        return;
    }

    uint32_t file_id = 0;
    int ret = efly_send_file(file_path.c_str(), ip.c_str(), port, &file_id);
    if (ret != 0 || file_id == 0) {
        std::cout << "Failed to send file (error " << ret << ")" << std::endl;
        return;
    }

    auto fsize = fs::file_size(file_path);
    std::string fname = fs::path(file_path).filename().string();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_xfers[file_id] = {file_id, fname, fsize, 0, ip, true};
    }
    std::cout << "Sending '" << fname << "' (" << fmt_size(fsize)
              << ") to " << ip << ":" << port << "  [id=" << file_id << "]"
              << std::endl;
    std::cout << "  (waiting for peer to accept...)" << std::endl;
}

static void cmd_accept(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: accept <transfer_id>" << std::endl;
        return;
    }
    uint32_t id = static_cast<uint32_t>(std::stoul(args[1]));

    ensure_dir(g_download_dir);
    int ret = efly_accept_transfer(id, g_download_dir.c_str());
    if (ret != 0) {
        std::cout << "Failed to accept transfer #" << id
                  << " (error " << ret << ")" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_xfers.find(id);
    if (it != g_xfers.end()) {
        std::cout << "Accepted '" << it->second.filename
                  << "', saving to " << g_download_dir << std::endl;
    }
    // remove from pending announces
    g_announces.erase(
        std::remove_if(g_announces.begin(), g_announces.end(),
                       [id](const Announce& a) { return a.id == id; }),
        g_announces.end());
}

static void cmd_reject(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: reject <transfer_id>" << std::endl;
        return;
    }
    uint32_t id = static_cast<uint32_t>(std::stoul(args[1]));
    efly_reject_transfer(id);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_xfers.erase(id);
    }
    std::cout << "Rejected transfer #" << id << std::endl;
}

static void cmd_progress() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_xfers.empty()) {
        std::cout << "No transfers." << std::endl;
        return;
    }

    // refresh progress from DLL for each transfer
    for (auto& [id, x] : g_xfers) {
        uint64_t total = 0, xfer = 0;
        if (efly_get_transfer_progress(id, &total, &xfer) == 0) {
            x.size        = total;
            x.transferred = xfer;
        }
    }

    std::cout << "Transfers:" << std::endl;
    for (const auto& [id, x] : g_xfers) {
        int pct = x.size > 0 ? (int)(x.transferred * 100 / x.size) : 0;
        std::string dir = x.is_sender ? "SEND" : "RECV";
        std::string status;
        if (x.complete)       status = " DONE";
        else if (x.error)     status = " ERROR: " + x.error_msg;
        else                  status = "";

        std::cout << "  [" << id << "] " << dir << "  " << x.filename
                  << "  " << fmt_size(x.transferred) << " / "
                  << fmt_size(x.size) << " (" << pct << "%)"
                  << status << std::endl;
    }
}

static void cmd_cancel(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: cancel <transfer_id>" << std::endl;
        return;
    }
    uint32_t id = static_cast<uint32_t>(std::stoul(args[1]));
    efly_cancel_transfer(id);
    std::cout << "Cancelled transfer #" << id << std::endl;
}

static void cmd_dir(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Download directory: " << g_download_dir << std::endl;
        return;
    }
    g_download_dir = args[1];
    ensure_dir(g_download_dir);
    std::cout << "Download directory set to: " << g_download_dir << std::endl;
}

static void cmd_clear() {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_xfers.begin();
    while (it != g_xfers.end()) {
        if (it->second.complete || it->second.error) {
            it = g_xfers.erase(it);
        } else {
            ++it;
        }
    }
    g_announces.clear();
    std::cout << "Cleared finished transfers." << std::endl;
}

// ============================================================
// main loop
// ============================================================

int main(int argc, char* argv[]) {
    std::cout << R"(
     ______   ______   __      __  __
    / ____/  / ____/  / /     / / / /
   / /__    / /___   / /     / /_/ /
  / ___ /  /  ___/  / /      \__, /
 /_____   /  /     / /___      / /
/_____/  /__/     /_____/   /___/

     LAN File Sharing Tool
)" << std::endl;
    std::cout << "Type 'help' for commands, 'quit' to exit.\n" << std::endl;

    // init DLL
    if (efly_init() != 0) {
        std::cerr << "Failed to initialize efly library." << std::endl;
        return 1;
    }

    // register callbacks
    efly_set_peer_found_callback(cb_peer, nullptr);
    efly_set_transfer_announce_callback(cb_announce, nullptr);
    efly_set_transfer_progress_callback(cb_progress, nullptr);
    efly_set_transfer_complete_callback(cb_complete, nullptr);
    efly_set_error_callback(cb_error, nullptr);

    // command loop
    std::string line;
    while (g_running) {
        std::cout << "efly> " << std::flush;
        if (!std::getline(std::cin, line)) break;  // EOF
        if (line.empty()) continue;

        // tokenize
        std::istringstream iss(line);
        std::vector<std::string> args;
        std::string token;
        while (iss >> token) args.push_back(token);
        if (args.empty()) continue;

        std::string cmd = args[0];

        if (cmd == "quit" || cmd == "exit") {
            g_running = false;
        }
        else if (cmd == "help" || cmd == "?") {
            cmd_help();
        }
        else if (cmd == "discover") {
            cmd_discover(args);
        }
        else if (cmd == "stop") {
            cmd_stop();
        }
        else if (cmd == "peers") {
            cmd_peers();
        }
        else if (cmd == "send") {
            cmd_send(args);
        }
        else if (cmd == "accept") {
            cmd_accept(args);
        }
        else if (cmd == "reject") {
            cmd_reject(args);
        }
        else if (cmd == "progress" || cmd == "ps") {
            cmd_progress();
        }
        else if (cmd == "cancel") {
            cmd_cancel(args);
        }
        else if (cmd == "dir") {
            cmd_dir(args);
        }
        else if (cmd == "clear") {
            cmd_clear();
        }
        else {
            std::cout << "Unknown command: " << cmd
                      << "  (type 'help' for list)" << std::endl;
        }
    }

    // shutdown
    std::cout << "Shutting down..." << std::endl;
    efly_stop_discovery();
    efly_shutdown();
    std::cout << "Goodbye." << std::endl;
    return 0;
}
