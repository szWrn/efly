#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace efly {

// ============================================================
// FileReader — sequential chunked file reading
// ============================================================

class FileReader {
public:
    FileReader() = default;
    ~FileReader();

    bool open(const std::string& path);
    void close();
    bool is_open() const;

    uint64_t size() const { return file_size_; }
    const std::string& path() const { return path_; }
    std::optional<std::vector<uint8_t>> read_chunk(uint64_t offset, size_t max_size);

private:
    std::string path_;
    void* handle_ = nullptr;   // HANDLE on Win, fd on POSIX
    uint64_t file_size_ = 0;
};

// ============================================================
// FileWriter — chunked file writing at arbitrary offsets
// ============================================================

class FileWriter {
public:
    FileWriter() = default;
    ~FileWriter();

    bool open(const std::string& path, uint64_t total_size);
    void close();
    bool is_open() const;

    const std::string& path() const { return path_; }
    const std::string& last_error() const { return last_error_; }
    uint64_t bytes_written() const { return bytes_written_; }
    uint64_t total_size() const { return total_size_; }
    bool write_chunk(uint64_t offset, const uint8_t* data, size_t len);

private:
    std::string path_;
    std::string last_error_;
    void* handle_ = nullptr;   // HANDLE on Win, fd on POSIX
    uint64_t total_size_    = 0;
    uint64_t bytes_written_ = 0;
};

} // namespace efly
