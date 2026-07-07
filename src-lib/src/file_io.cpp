#include "efly/file_io.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace efly {

// ============================================================
// helpers
// ============================================================

#ifdef _WIN32

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    w.resize(len - 1);
    return w;
}

static HANDLE h(void* p) { return reinterpret_cast<HANDLE>(p); }

static bool open_ro(const std::string& path, void** out, uint64_t* out_size) {
    auto w = to_wide(path);
    HANDLE fh = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "[efly_core] CreateFileW failed: path=%s err=0x%lx\n",
                 path.c_str(), (unsigned long)err);
        OutputDebugStringA(buf);
        fprintf(stderr, "%s", buf);
        return false;
    }
    LARGE_INTEGER li;
    GetFileSizeEx(fh, &li);
    *out_size = static_cast<uint64_t>(li.QuadPart);
    *out = fh;
    return true;
}

static bool open_rw(const std::string& path, void** out, std::string* err) {
    auto w = to_wide(path);
    HANDLE fh = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        if (err) {
            DWORD code = GetLastError();
            char buf[128];
            snprintf(buf, sizeof(buf), " (WinErr 0x%lx)", (unsigned long)code);
            *err = buf;
        }
        return false;
    }
    *out = fh;
    return true;
}

static bool read_at(void* fh, uint64_t offset, uint8_t* buf, size_t len) {
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h(fh), li, nullptr, FILE_BEGIN)) return false;
    DWORD n = 0;
    return ReadFile(h(fh), buf, static_cast<DWORD>(len), &n, nullptr) && n == len;
}

static bool write_at(void* fh, uint64_t offset, const uint8_t* buf, size_t len) {
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h(fh), li, nullptr, FILE_BEGIN)) return false;
    DWORD n = 0;
    return WriteFile(h(fh), buf, static_cast<DWORD>(len), &n, nullptr) && n == len;
}

static void close_handle(void* fh) { if (fh) CloseHandle(h(fh)); }

#else // POSIX

static bool open_ro(const std::string& path, void** out, uint64_t* out_size) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    *out_size = static_cast<uint64_t>(lseek(fd, 0, SEEK_END));
    lseek(fd, 0, SEEK_SET);
    *out = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    return true;
}

static bool open_rw(const std::string& path, void** out) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    *out = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    return true;
}

static int fd_from(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }

static bool read_at(void* fh, uint64_t offset, uint8_t* buf, size_t len) {
    if (lseek(fd_from(fh), static_cast<off_t>(offset), SEEK_SET) < 0) return false;
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::read(fd_from(fh), buf + total, len - total);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

static bool write_at(void* fh, uint64_t offset, const uint8_t* buf, size_t len) {
    if (lseek(fd_from(fh), static_cast<off_t>(offset), SEEK_SET) < 0) return false;
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::write(fd_from(fh), buf + total, len - total);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

static void close_handle(void* fh) { ::close(fd_from(fh)); }

#endif

// ============================================================
// FileReader
// ============================================================

FileReader::~FileReader() { close(); }

bool FileReader::open(const std::string& path) {
    close();
    uint64_t sz = 0;
    void* fh = nullptr;
    if (!open_ro(path, &fh, &sz)) return false;
    handle_    = fh;
    file_size_ = sz;
    path_      = path;
    return true;
}

void FileReader::close() {
    close_handle(handle_);
    handle_    = nullptr;
    file_size_ = 0;
    path_.clear();
}

bool FileReader::is_open() const { return handle_ != nullptr; }

std::optional<std::vector<uint8_t>> FileReader::read_chunk(uint64_t offset, size_t max_size) {
    if (!is_open()) return std::nullopt;
    if (offset >= file_size_) return std::vector<uint8_t>{};

    size_t to_read = (std::min)(max_size, static_cast<size_t>(file_size_ - offset));
    std::vector<uint8_t> buf(to_read);
    if (!read_at(handle_, offset, buf.data(), to_read)) return std::nullopt;
    return buf;
}

// ============================================================
// FileWriter
// ============================================================

FileWriter::~FileWriter() { close(); }

bool FileWriter::open(const std::string& path, uint64_t total_size) {
    close();
    void* fh = nullptr;
    std::string err;
    if (!open_rw(path, &fh, &err)) {
        last_error_ = err;
        return false;
    }
    handle_      = fh;
    path_        = path;
    total_size_  = total_size;
    return true;
}

void FileWriter::close() {
    close_handle(handle_);
    handle_      = nullptr;
    total_size_  = 0;
    bytes_written_ = 0;
    path_.clear();
}

bool FileWriter::is_open() const { return handle_ != nullptr; }

bool FileWriter::write_chunk(uint64_t offset, const uint8_t* data, size_t len) {
    if (!is_open()) return false;
    if (!write_at(handle_, offset, data, len)) return false;
    bytes_written_ += len;
    return true;
}

} // namespace efly
