# efly — 局域网文件分享工具

基于 Tauri + Vue 3 + C++ 的局域网文件快速分享工具，使用 UDP 协议实现设备发现与点对点文件传输。

## 架构

```
efly/
├── src/                    # Vue 3 + TypeScript 前端
│   └── components/         # PeerPanel / TransferPanel
├── src-tauri/              # Rust / Tauri 后端
│   └── src/
│       ├── lib.rs          # Tauri 命令入口
│       ├── update.rs       # 自动更新 FFI
│       └── update_conf.rs  # 更新配置
├── src-lib/                # C++ 文件传输 DLL (efly_core.dll)
│   ├── include/efly/       # 公开 API 头文件
│   └── src/                # 源文件
├── library/
│   └── auto-update/        # C++ 自动更新 DLL (auto-update.dll)
├── build.cmd               # 一键构建脚本
├── version.json            # 本地版本文件
└── updateLists.json        # 远端更新清单（模板）
```

## 前置依赖

| 工具 | 用途 |
|------|------|
| [Node.js](https://nodejs.org/) ≥ 18 | 前端构建 |
| [Rust](https://www.rust-lang.org/) | Tauri 后端 |
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | C++ 编译器 (MSVC) |
| [CMake](https://cmake.org/) ≥ 3.14 | C++ 构建系统 |
| [vcpkg](https://github.com/microsoft/vcpkg) | C++ 依赖管理 |
| [Git](https://git-scm.com/) | 克隆 auto-update 仓库 |

## 快速开始

```bash
# 安装前端依赖
npm install

# 开发模式（需要先构建 C++ DLL）
cd src-lib
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd ..
npm run tauri dev
```

## 一键构建（Release）

```bash
./build
```

该脚本会依次：
1. 编译 `efly_core.dll`（文件传输）
2. 从 GitHub 克隆并编译 `auto-update.dll`（自动更新）
3. 构建 Tauri 安装包

产物位于 `src-tauri/target/release/bundle/`。

## 使用

1. 两台电脑连接到同一局域网
2. 双方启动 efly，在 **Devices** 面板输入端口号，点击 **Discover**
3. 发现对方后，选中设备，点击 **Send File**，选择要发送的文件
4. 接收方收到通知，点击 **Accept** 接受，文件保存到下载目录
5. 进度条实时显示传输状态

## 协议

UDP 自定义二进制协议，MTU 安全（≤ 1472 字节）。

| 类型 | 方向 | 说明 |
|------|------|------|
| DISCOVERY | 广播 | 设备发现信标 |
| FILE_ANNOUNCE | → 接收方 | 文件元信息 |
| FILE_ANNOUNCE_ACK | ← 发送方 | 接受/拒绝 |
| FILE_DATA | → 接收方 | 数据块 (1430B) |
| ACK | ← 发送方 | 累积确认 |
| TRANSFER_COMPLETE | → 接收方 | 传输完成 |
| CANCEL | 双向 | 取消传输 |

Go-Back-N：窗口 16，超时 500ms，最多重传 10 次。

## 自动更新

应用启动时从配置的 URL 拉取 `updateLists.json`，与本地 `version.json` 比对。支持多镜像择优（TCP 延迟探测）。

| 优先级 | 行为 |
|--------|------|
| `silent` | 静默下载安装，不打扰用户 |
| `warn` | 弹窗提示，用户可跳过 |
| `force` | 强制更新，拒绝即退出 |

相关代码见 `src-tauri/src/update.rs`、`src-tauri/src/update_conf.rs`。

## 项目结构对照

| 目录 | 语言 | 产出 |
|------|------|------|
| `src/` | Vue 3 + TS | 前端 UI |
| `src-tauri/` | Rust | Tauri 桌面壳 |
| `src-lib/` | C++ / CMake | `efly_core.dll` |
| `library/auto-update/` | C++ / CMake | `auto-update.dll` |

## License

MIT
