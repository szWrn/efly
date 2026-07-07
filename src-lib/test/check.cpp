/**
 * efly-lib 测试程序
 *
 * 测试 DLL 加载、生命周期、错误处理、回调和 localhost 端到端传输。
 *
 * 编译（在 src-lib 目录下）：
 *   cd test && cmake -S . -B build -G "Visual Studio 17 2022" -A x64
 *   cmake --build build --config Release
 *
 * 运行：
 *   build\bin\Release\efly_test.exe
 */

#include "efly/efly_api.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// 测试框架
// ============================================================

static int  g_passed = 0;
static int  g_failed = 0;

static void pass(const char* name) {
    std::cout << "  [" << name << "] PASSED" << std::endl;
    g_passed++;
}

static void fail(const char* name, const std::string& msg) {
    std::cout << "  [" << name << "] FAILED: " << msg << std::endl;
    g_failed++;
}

// ============================================================
// 回调同步数据（用原子变量 + mutex/cv 做线程间同步）
// ============================================================

struct Sync {
    std::mutex              mtx;
    std::condition_variable cv;

    std::atomic<bool> peer_found{false};
    std::string       peer_ip;
    uint16_t          peer_port = 0;

    std::atomic<bool> announce{false};
    uint32_t          announce_id   = 0;
    std::string       announce_name;
    uint64_t          announce_size = 0;
    std::string       announce_from;

    std::atomic<int>  progress_count{0};
    uint64_t          last_total       = 0;
    uint64_t          last_transferred = 0;

    std::atomic<bool> complete{false};
    uint32_t          complete_id   = 0;
    std::string       complete_path;

    std::atomic<bool> error{false};
    uint32_t          error_id   = 0;
    int               error_code = 0;
    std::string       error_msg;
};

static Sync g;

// ---- 重置同步状态（逐字段，避免 mutex 拷贝问题） ----
static void reset_sync() {
    g.peer_found       = false;
    g.peer_ip.clear();
    g.peer_port        = 0;
    g.announce         = false;
    g.announce_id      = 0;
    g.announce_name.clear();
    g.announce_size    = 0;
    g.announce_from.clear();
    g.progress_count   = 0;
    g.last_total       = 0;
    g.last_transferred = 0;
    g.complete         = false;
    g.complete_id      = 0;
    g.complete_path.clear();
    g.error            = false;
    g.error_id         = 0;
    g.error_code       = 0;
    g.error_msg.clear();
}

// ============================================================
// 回调实现
// ============================================================

static void cb_peer(const char* ip, uint16_t port, void*) {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.peer_found = true;
    g.peer_ip    = ip;
    g.peer_port  = port;
    std::cout << "    [cb] Peer: " << ip << ":" << port << std::endl;
    g.cv.notify_all();
}

static void cb_announce(uint32_t id, const char* name, uint64_t size,
                         const char* from, void*) {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.announce      = true;
    g.announce_id   = id;
    g.announce_name = name;
    g.announce_size = size;
    g.announce_from = from;
    std::cout << "    [cb] Announce: id=" << id << " file=" << name
              << " size=" << size << " from=" << from << std::endl;
    g.cv.notify_all();
}

static void cb_progress(uint32_t id, uint64_t total, uint64_t xfer, void*) {
    int cnt = ++g.progress_count;
    g.last_total       = total;
    g.last_transferred = xfer;
    if (cnt % 100 == 1 || xfer >= total) {
        int pct = total ? (int)(xfer * 100 / total) : 0;
        std::cout << "    [cb] Progress: " << pct << "% ("
                  << xfer << "/" << total << ")" << std::endl;
    }
}

static void cb_complete(uint32_t id, const char* path, void*) {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.complete      = true;
    g.complete_id   = id;
    g.complete_path = path;
    std::cout << "    [cb] Complete: id=" << id << " path=" << path << std::endl;
    g.cv.notify_all();
}

static void cb_error(uint32_t id, int code, const char* msg, void*) {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.error      = true;
    g.error_id   = id;
    g.error_code = code;
    g.error_msg  = msg;
    std::cout << "    [cb] Error: id=" << id << " code=" << code
              << " msg=" << msg << std::endl;
    g.cv.notify_all();
}

// ============================================================
// 辅助
// ============================================================

static std::string rand_str(size_t n) {
    static const char C[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s; s.reserve(n);
    for (size_t i = 0; i < n; i++) s += C[rand() % (sizeof(C)-1)];
    return s;
}

static std::string make_temp_file(const fs::path& dir, size_t size_kb) {
    auto p = dir / ("efly_test_" + rand_str(8) + ".bin");
    std::ofstream f(p, std::ios::binary);
    std::vector<char> buf(4096);
    size_t total = size_kb * 1024;
    for (size_t w = 0; w < total; w += buf.size()) {
        size_t chunk = std::min(buf.size(), total - w);
        for (size_t i = 0; i < chunk; i++) buf[i] = (char)((w + i) % 256);
        f.write(buf.data(), chunk);
    }
    return p.string();
}

static bool files_equal(const std::string& a, const std::string& b) {
    std::ifstream fa(a, std::ios::binary | std::ios::ate);
    std::ifstream fb(b, std::ios::binary | std::ios::ate);
    if (!fa || !fb) return false;
    if (fa.tellg() != fb.tellg()) return false;
    fa.seekg(0); fb.seekg(0);
    std::vector<char> ba(65536), bb(65536);
    while (fa && fb) {
        fa.read(ba.data(), ba.size());
        fb.read(bb.data(), bb.size());
        auto na = (size_t)fa.gcount(), nb = (size_t)fb.gcount();
        if (na != nb) return false;
        if (std::memcmp(ba.data(), bb.data(), na)) return false;
    }
    return true;
}

template<typename T>
static std::string str(const T& v) {
    std::ostringstream ss; ss << v; return ss.str();
}

// ============================================================
// 测试 1: init / shutdown 生命周期
// ============================================================

static void t1_lifecycle() {
    const char* name = "init + shutdown lifecycle";
    efly_shutdown();  // clean slate

    int r = efly_init();
    if (r != 0) { fail(name, "efly_init returned " + str(r)); return; }

    r = efly_init(); // 幂等
    if (r != 0) { fail(name, "second init returned " + str(r)); return; }

    efly_shutdown();
    pass(name);
}

// ============================================================
// 测试 2: 未初始化调用返回错误
// ============================================================

static void t2_not_init() {
    const char* name = "calls before init return errors";
    efly_shutdown();

    uint32_t id = 0;
    int r = efly_send_file("x.txt", "127.0.0.1", 1, &id);
    if (r != -8) { fail(name, "send_file expected -8, got " + str(r)); return; }

    r = efly_accept_transfer(1, ".");
    if (r != -8) { fail(name, "accept expected -8, got " + str(r)); return; }

    r = efly_start_discovery(1);
    if (r != -8) { fail(name, "start_discovery expected -8, got " + str(r)); return; }

    pass(name);
}

// ============================================================
// 测试 3: 回调注册
// ============================================================

static void t3_callbacks() {
    const char* name = "callback registration";
    efly_shutdown();
    if (efly_init() != 0) { fail(name, "init failed"); return; }

    efly_set_peer_found_callback(nullptr, nullptr);
    efly_set_transfer_announce_callback(nullptr, nullptr);
    efly_set_transfer_progress_callback(nullptr, nullptr);
    efly_set_transfer_complete_callback(nullptr, nullptr);
    efly_set_error_callback(nullptr, nullptr);

    efly_set_peer_found_callback(cb_peer, nullptr);
    efly_set_transfer_announce_callback(cb_announce, nullptr);
    efly_set_transfer_progress_callback(cb_progress, nullptr);
    efly_set_transfer_complete_callback(cb_complete, nullptr);
    efly_set_error_callback(cb_error, nullptr);

    efly_shutdown();
    pass(name);
}

// ============================================================
// 测试 4: 进度查询无效 ID
// ============================================================

static void t4_progress_invalid() {
    const char* name = "progress query with invalid ID";
    efly_shutdown();
    if (efly_init() != 0) { fail(name, "init failed"); return; }

    uint64_t t = 0, x = 0;
    int r = efly_get_transfer_progress(99999, &t, &x);
    if (r == 0) { fail(name, "should fail for non-existent id"); return; }

    efly_shutdown();
    pass(name);
}

// ============================================================
// 测试 5: 发现 启动/停止
// ============================================================

static void t5_discovery() {
    const char* name = "discovery start + stop";
    efly_shutdown();

    // round 1
    if (efly_init() != 0) { fail(name, "init-1 failed"); return; }
    efly_set_peer_found_callback(cb_peer, nullptr);
    if (efly_start_discovery(22345) != 0) { fail(name, "start-1 failed"); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    efly_stop_discovery();
    efly_shutdown();

    // Give OS time to release UDP ports
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // round 2 - restart after full shutdown
    int ret = efly_init();
    if (ret != 0) { fail(name, "init-2 failed: " + str(ret)); return; }
    efly_set_peer_found_callback(cb_peer, nullptr);
    ret = efly_start_discovery(22345);
    if (ret != 0) { fail(name, "start-2 failed: " + str(ret)); return; }
    efly_stop_discovery();
    efly_shutdown();

    pass(name);
}

// ============================================================
// 测试 6: 端到端传输（localhost 回环）
// ============================================================

static void t6_e2e() {
    const char*    name = "end-to-end localhost transfer";
    const uint16_t PORT = 22346;

    reset_sync();
    efly_shutdown();

    // ---- 准备文件 ----
    fs::path tmp = fs::temp_directory_path() / "efly_test";
    fs::create_directories(tmp);
    std::string src_file = make_temp_file(tmp, 512);  // 512 KB
    std::string dest_dir = (tmp / "received").string();
    fs::create_directories(dest_dir);
    auto expected_size = fs::file_size(src_file);

    std::cout << "    src:  " << src_file << " (" << expected_size << " bytes)" << std::endl;

    // ---- Init ----
    if (efly_init() != 0) { fail(name, "init failed"); goto cleanup; }
    efly_set_peer_found_callback(cb_peer, nullptr);
    efly_set_transfer_announce_callback(cb_announce, nullptr);
    efly_set_transfer_progress_callback(cb_progress, nullptr);
    efly_set_transfer_complete_callback(cb_complete, nullptr);
    efly_set_error_callback(cb_error, nullptr);

    // ---- 启动 ----
    if (efly_start_discovery(PORT) != 0) { fail(name, "start_discovery failed"); goto cleanup; }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ---- 发送 ----
    uint32_t fid = 0;
    if (efly_send_file(src_file.c_str(), "127.0.0.1", PORT, &fid) != 0) {
        fail(name, "send_file failed"); goto cleanup;
    }
    if (fid == 0) { fail(name, "file_id is 0"); goto cleanup; }
    std::cout << "    file_id = " << fid << std::endl;

    // ---- 等待 ANNOUNCE ----
    {
        std::unique_lock<std::mutex> lk(g.mtx);
        if (!g.cv.wait_for(lk, std::chrono::seconds(3), []{ return g.announce.load() || g.error.load(); })) {
            fail(name, "timeout waiting for announce"); goto cleanup;
        }
    }
    if (g.error) {
        fail(name, "error during announce: " + g.error_msg); goto cleanup;
    }
    if (!g.announce) {
        fail(name, "announce not received"); goto cleanup;
    }

    // ---- 接受 ----
    std::cout << "    Accepting..." << std::endl;
    if (efly_accept_transfer(fid, dest_dir.c_str()) != 0) {
        fail(name, "accept_transfer failed"); goto cleanup;
    }

    // ---- 等待完成 ----
    {
        std::unique_lock<std::mutex> lk(g.mtx);
        if (!g.cv.wait_for(lk, std::chrono::seconds(30), []{ return g.complete.load() || g.error.load(); })) {
            fail(name, "timeout waiting for completion"); goto cleanup;
        }
    }
    if (g.error) {
        fail(name, "transfer error: " + g.error_msg + " (code=" + str(g.error_code) + ")");
        goto cleanup;
    }
    if (!g.complete) { fail(name, "transfer did not complete"); goto cleanup; }

    // ---- 校验 ----
    if (g.progress_count <= 0) { fail(name, "no progress callbacks"); goto cleanup; }
    if (g.last_transferred != expected_size) {
        fail(name, "size mismatch: " + str(g.last_transferred) + " vs " + str(expected_size));
        goto cleanup;
    }
    std::cout << "    progress callbacks: " << g.progress_count << std::endl;

    if (!fs::exists(g.complete_path)) {
        fail(name, "received file missing: " + g.complete_path); goto cleanup;
    }
    if (!files_equal(src_file, g.complete_path)) {
        fail(name, "file content mismatch"); goto cleanup;
    }
    std::cout << "    file integrity OK" << std::endl;

    pass(name);

cleanup:
    efly_stop_discovery();
    efly_shutdown();
    fs::remove_all(tmp);
}

// ============================================================
// main
// ============================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  efly-lib Test Suite" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    std::cout << "--- Basic Tests ---" << std::endl;
    t1_lifecycle();
    t2_not_init();
    t3_callbacks();
    t4_progress_invalid();
    t5_discovery();
    std::cout << std::endl;

    std::cout << "--- End-to-End Test ---" << std::endl;
    t6_e2e();
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "  " << g_passed << " passed, " << g_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed ? 1 : 0;
}
