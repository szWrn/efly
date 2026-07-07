# efly-lib 开发文档

## 目录

1. [编译构建](#1-编译构建)
2. [API 参考](#2-api-参考)
3. [回调类型](#3-回调类型)
4. [Rust / Tauri 集成](#4-rust--tauri-集成)
5. [调用流程示例](#5-调用流程示例)
6. [协议说明](#6-协议说明)
7. [错误码](#7-错误码)

---

## 1. 编译构建

### 前置条件

- Visual Studio 2022（含 MSVC v143 工具链）
- CMake ≥ 3.14
- vcpkg（Boost.Asio 已安装在 `vcpkg_installed/` 中）

### 构建命令

```bash
cd src-lib

# 配置（使用项目本地 vcpkg）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 编译 Release
cmake --build build --config Release
```

输出文件：
- `../src-tauri/Release/efly_lib.dll` — 运行时动态库
- `../src-tauri/Release/efly_lib.lib` — 链接时导入库

### 目录结构

```
src-lib/
├── include/efly/          # 头文件（集成时引用这层）
├── src/                   # 源文件
├── CMakeLists.txt         # 构建配置
├── vcpkg.json             # vcpkg 依赖声明
└── vcpkg_installed/       # 本地 vcpkg 包
```

---

## 2. API 参考

所有 API 使用 `extern "C"` 调用约定，返回 `int`，0 表示成功，负数表示错误。

### 生命周期

```c
int  efly_init(void);
void efly_shutdown(void);
```

| 函数 | 说明 |
|------|------|
| `efly_init()` | 初始化库，必须在所有其他调用之前执行。返回 0 成功。 |
| `efly_shutdown()` | 释放所有资源，停止所有网络活动。可安全重复调用。 |

---

### 注册回调（在 `efly_start_discovery` 之前调用）

```c
void efly_set_peer_found_callback(PeerFoundCallback cb, void* user_data);
void efly_set_transfer_announce_callback(TransferAnnounceCallback cb, void* user_data);
void efly_set_transfer_progress_callback(TransferProgressCallback cb, void* user_data);
void efly_set_transfer_complete_callback(TransferCompleteCallback cb, void* user_data);
void efly_set_error_callback(ErrorCallback cb, void* user_data);
```

所有回调从 IO 线程触发，不要在回调中做耗时操作。如需更新 UI，应将事件投递到主线程。

---

### 发现

```c
int  efly_start_discovery(uint16_t port);
void efly_stop_discovery(void);
```

| 函数 | 说明 |
|------|------|
| `efly_start_discovery(port)` | 启动局域网发现。`port` 是收发文件使用的 UDP 端口。广播信标每 2 秒发送一次到 255.255.255.255:21212。 |
| `efly_stop_discovery()` | 停止发现并关闭传输服务端。 |

---

### 发送文件

```c
int efly_send_file(
    const char* file_path,    // 要发送的文件路径（UTF-8）
    const char* remote_ip,    // 目标 IP，如 "192.168.1.5"
    uint16_t    remote_port,  // 目标端口（从 PeerFoundCallback 获得）
    uint32_t*   out_file_id   // [出参] 分配的传输 ID
);
```

**返回值：** 0 成功，`out_file_id` 被填充。之后通过该 ID 查询进度或取消。

---

### 接收文件

```c
int efly_accept_transfer(uint32_t file_id, const char* save_dir);
int efly_reject_transfer(uint32_t file_id);
```

| 函数 | 说明 |
|------|------|
| `efly_accept_transfer(id, dir)` | 接受传入的传输。文件保存到 `dir/filename`。 |
| `efly_reject_transfer(id)` | 拒绝传入的传输。 |

`file_id` 来自 `TransferAnnounceCallback`。

---

### 控制

```c
int efly_cancel_transfer(uint32_t file_id);
```

取消进行中的传输（发送或接收均可）。触发 `ErrorCallback`（错误码 -6）。

---

### 查询进度

```c
int efly_get_transfer_progress(
    uint32_t  file_id,
    uint64_t* out_total,        // [出参] 文件总大小
    uint64_t* out_transferred   // [出参] 已传输字节数
);
```

线程安全，可从任意线程调用。返回 0 成功，负数表示传输不存在。

---

## 3. 回调类型

全部定义在 `efly_types.h` 中。

```c
// 发现新对端 — IP + 传输端口
typedef void (*PeerFoundCallback)(
    const char* ip, uint16_t port, void* user_data);

// 收到传入文件通知 — 需要用户决定接受或拒绝
typedef void (*TransferAnnounceCallback)(
    uint32_t file_id, const char* filename,
    uint64_t file_size, const char* from_ip, void* user_data);

// 传输进度更新 — bytes_transferred / total
typedef void (*TransferProgressCallback)(
    uint32_t file_id, uint64_t total,
    uint64_t transferred, void* user_data);

// 传输完成 — saved_path 为接收方保存路径或发送方源路径
typedef void (*TransferCompleteCallback)(
    uint32_t file_id, const char* saved_path, void* user_data);

// 错误 — error_code 见下方错误码表
typedef void (*ErrorCallback)(
    uint32_t file_id, int error_code,
    const char* message, void* user_data);
```

---

## 4. Rust / Tauri 集成

### 4.1 FFI 声明

在 `src-tauri/src/lib.rs` 或单独的 `ffi.rs` 中：

```rust
use std::ffi::{c_char, c_void, CStr};

// 回调类型别名
type PeerFoundCallback = extern "C" fn(*const c_char, u16, *mut c_void);
type TransferAnnounceCallback = extern "C" fn(u32, *const c_char, u64, *const c_char, *mut c_void);
type TransferProgressCallback = extern "C" fn(u32, u64, u64, *mut c_void);
type TransferCompleteCallback = extern "C" fn(u32, *const c_char, *mut c_void);
type ErrorCallback = extern "C" fn(u32, i32, *const c_char, *mut c_void);

extern "C" {
    fn efly_init() -> i32;
    fn efly_shutdown();
    fn efly_start_discovery(port: u16) -> i32;
    fn efly_stop_discovery();
    fn efly_send_file(
        file_path: *const c_char,
        remote_ip: *const c_char,
        remote_port: u16,
        out_file_id: *mut u32,
    ) -> i32;
    fn efly_accept_transfer(file_id: u32, save_dir: *const c_char) -> i32;
    fn efly_reject_transfer(file_id: u32) -> i32;
    fn efly_cancel_transfer(file_id: u32) -> i32;
    fn efly_get_transfer_progress(
        file_id: u32,
        out_total: *mut u64,
        out_transferred: *mut u64,
    ) -> i32;
    fn efly_set_peer_found_callback(cb: PeerFoundCallback, user_data: *mut c_void);
    fn efly_set_transfer_announce_callback(cb: TransferAnnounceCallback, user_data: *mut c_void);
    fn efly_set_transfer_progress_callback(cb: TransferProgressCallback, user_data: *mut c_void);
    fn efly_set_transfer_complete_callback(cb: TransferCompleteCallback, user_data: *mut c_void);
    fn efly_set_error_callback(cb: ErrorCallback, user_data: *mut c_void);
}
```

### 4.2 回调桥接（推荐：通过 Tauri 事件）

```rust
use tauri::AppHandle;
use std::sync::Mutex;

static APP_HANDLE: Mutex<Option<AppHandle>> = Mutex::new(None);

pub fn set_app_handle(handle: AppHandle) {
    *APP_HANDLE.lock().unwrap() = Some(handle);
}

// 回调实现 — 由 C 调用
extern "C" fn on_peer_found(ip: *const c_char, port: u16, _: *mut c_void) {
    let ip_str = unsafe { CStr::from_ptr(ip) }.to_string_lossy().to_string();
    if let Some(ref handle) = *APP_HANDLE.lock().unwrap() {
        let _ = handle.emit("efly:peer-found",
            serde_json::json!({ "ip": ip_str, "port": port }));
    }
}

extern "C" fn on_transfer_announce(
    file_id: u32, filename: *const c_char,
    file_size: u64, from_ip: *const c_char, _: *mut c_void,
) {
    let name = unsafe { CStr::from_ptr(filename) }.to_string_lossy();
    let ip   = unsafe { CStr::from_ptr(from_ip) }.to_string_lossy();
    if let Some(ref handle) = *APP_HANDLE.lock().unwrap() {
        let _ = handle.emit("efly:transfer-announce",
            serde_json::json!({ "fileId": file_id, "filename": name,
                                "fileSize": file_size, "fromIp": ip }));
    }
}

// ... progress, complete, error 同理
```

### 4.3 Tauri 命令封装

```rust
#[tauri::command]
fn init_library() -> Result<(), String> {
    let ret = unsafe { efly_init() };
    if ret == 0 { Ok(()) }
    else { Err(format!("efly_init failed: {}", ret)) }
}

#[tauri::command]
fn start_discovery(port: u16) -> Result<(), String> {
    let ret = unsafe { efly_start_discovery(port) };
    if ret == 0 { Ok(()) }
    else { Err(format!("start_discovery failed: {}", ret)) }
}

#[tauri::command]
fn send_file(path: String, ip: String, port: u16) -> Result<u32, String> {
    let c_path = std::ffi::CString::new(path).unwrap();
    let c_ip   = std::ffi::CString::new(ip).unwrap();
    let mut file_id: u32 = 0;
    let ret = unsafe {
        efly_send_file(c_path.as_ptr(), c_ip.as_ptr(), port, &mut file_id)
    };
    if ret == 0 { Ok(file_id) }
    else { Err(format!("send_file failed: {}", ret)) }
}

#[tauri::command]
fn accept_file(file_id: u32, save_dir: String) -> Result<(), String> {
    let c_dir = std::ffi::CString::new(save_dir).unwrap();
    let ret = unsafe { efly_accept_transfer(file_id, c_dir.as_ptr()) };
    if ret == 0 { Ok(()) }
    else { Err(format!("accept_transfer failed: {}", ret)) }
}

#[tauri::command]
fn reject_file(file_id: u32) -> Result<(), String> {
    let ret = unsafe { efly_reject_transfer(file_id) };
    if ret == 0 { Ok(()) }
    else { Err(format!("reject_transfer failed: {}", ret)) }
}

#[tauri::command]
fn cancel(file_id: u32) -> Result<(), String> {
    let ret = unsafe { efly_cancel_transfer(file_id) };
    if ret == 0 { Ok(()) }
    else { Err(format!("cancel_transfer failed: {}", ret)) }
}

#[tauri::command]
fn progress(file_id: u32) -> Result<serde_json::Value, String> {
    let mut total: u64 = 0;
    let mut transferred: u64 = 0;
    let ret = unsafe {
        efly_get_transfer_progress(file_id, &mut total, &mut transferred)
    };
    if ret == 0 {
        Ok(serde_json::json!({ "total": total, "transferred": transferred }))
    } else {
        Err(format!("progress query failed: {}", ret))
    }
}
```

### 4.4 注册回调 + 命令

```rust
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            // 保存 AppHandle 给回调使用
            set_app_handle(app.handle().clone());

            // 初始化 DLL + 注册回调
            unsafe {
                efly_init();
                efly_set_peer_found_callback(on_peer_found, std::ptr::null_mut());
                efly_set_transfer_announce_callback(on_transfer_announce, std::ptr::null_mut());
                efly_set_transfer_progress_callback(on_transfer_progress, std::ptr::null_mut());
                efly_set_transfer_complete_callback(on_transfer_complete, std::ptr::null_mut());
                efly_set_error_callback(on_error, std::ptr::null_mut());
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            init_library,
            start_discovery,
            send_file,
            accept_file,
            reject_file,
            cancel,
            progress,
        ])
        .run(tauri::generate_context!())
        .expect("error running tauri app");
}
```

---

## 5. 调用流程示例

### 5.1 应用启动

```
efly_init()
  → 注册 5 个回调
  → efly_start_discovery(12345)
```

### 5.2 发送文件

```
用户选择文件 → efly_send_file("C:\a.pdf", "192.168.1.5", 12345, &id)
  → TransferProgressCallback(id, total, transferred) ...反复回调...
  → TransferCompleteCallback(id, "C:\a.pdf") 或 ErrorCallback(id, code, msg)
```

### 5.3 接收文件

```
TransferAnnounceCallback(id, "a.pdf", 1024000, "192.168.1.5")
  → 用户选择「接受」
  → efly_accept_transfer(id, "C:\Users\xxx\Downloads")
  → TransferProgressCallback(id, total, transferred) ...反复回调...
  → TransferCompleteCallback(id, "C:\Users\xxx\Downloads\a.pdf")
```

### 5.4 应用退出

```
efly_stop_discovery()
efly_shutdown()
```

---

## 6. 协议说明

### 6.1 数据包格式

```
 0       4       5  6    8       12
┌───────┬───┬───┬──────┬───────┬──────────┐
│ magic │ver│typ│pl len│  seq  │ payload  │
│  4B   │1B │1B │ 2B   │  4B   │  变长    │
└───────┴───┴───┴──────┴───────┴──────────┘
```

- **magic**: `0x45464C59`（"EFLY"，大端序）
- **version**: `1`
- **type**: 见下表
- **payload_len**: 负载字节数（大端序）
- **seq**: 序列号（大端序）
- 总包不超过 1472 字节（MTU 安全）

### 6.2 数据包类型

| 值 | 名称 | 方向 | 说明 |
|----|------|------|------|
| 0x01 | DISCOVERY | 广播 | 发现信标，负载：`[port:2B]` |
| 0x02 | DISCOVERY_RESP | 单播 | 发现响应，负载：`[port:2B]` |
| 0x03 | FILE_ANNOUNCE | → 接收方 | 文件通知，负载：`[id:4B][size:8B][name_len:2B][name:N]` |
| 0x04 | FILE_ANNOUNCE_ACK | ← 发送方 | 接受/拒绝，负载：`[id:4B][accepted:1B]` |
| 0x05 | FILE_DATA | → 接收方 | 文件数据块，负载：`[id:4B][offset:4B][len:2B][data:N]` |
| 0x06 | ACK | ← 发送方 | 累积确认，负载：`[id:4B][ack_seq:4B]` |
| 0x07 | TRANSFER_COMPLETE | → 接收方 | 发送完成，负载：`[id:4B]` |
| 0x08 | CANCEL | 双向 | 取消，负载：`[id:4B][reason:1B]` |

### 6.3 发现端口

发现信标固定发送到 UDP 广播地址 `255.255.255.255:21212`。响应也走同一端口。发现阶段完成后，实际文件传输使用 `efly_start_discovery()` 指定的端口。

### 6.4 Go-Back-N 可靠性

- 窗口大小：16 个数据块
- 数据块大小：1430 字节
- ACK 超时：200ms
- 最大重传次数：10（超时视为失败）
- 接收方超时：30 秒无数据则终止

---

## 7. 错误码

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | OK | 成功 |
| -1 | UNKNOWN | 未知错误 |
| -2 | SOCKET_ERROR | 套接字错误（端口占用/网络不可用） |
| -3 | FILE_ERROR | 文件错误（找不到/无权限/磁盘满） |
| -4 | TIMEOUT | 超时（对端无响应） |
| -5 | PROTOCOL_ERROR | 协议错误（数据损坏/版本不匹配） |
| -6 | CANCELLED | 传输被取消（本端或对端） |
| -7 | REJECTED | 传输被对端拒绝 |
| -8 | NOT_INITIALIZED | 库未初始化 |
| -9 | INVALID_ARGUMENT | 参数无效 |
