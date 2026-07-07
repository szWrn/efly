# efly — LAN File Sharing Tool

A fast LAN file sharing app built with Tauri + Vue 3 + C++. Uses UDP for device discovery and peer-to-peer file transfer.

## Architecture

```
efly/
├── src/                    # Vue 3 + TypeScript frontend
│   └── components/         # PeerPanel / TransferPanel
├── src-tauri/              # Rust / Tauri backend
│   └── src/
│       ├── lib.rs          # Tauri command entry
│       ├── update.rs       # Auto-update FFI bindings
│       └── update_conf.rs  # Update configuration
├── src-lib/                # C++ file-transfer DLL (efly_core.dll)
│   ├── include/efly/       # Public API headers
│   └── src/                # Sources
├── library/
│   └── auto-update/        # C++ auto-update DLL (auto-update.dll)
├── build.cmd               # One-click build script
├── version.json            # Local version file
└── updateLists.json        # Remote update manifest template
```

## Prerequisites

| Tool | Purpose |
|------|---------|
| [Node.js](https://nodejs.org/) ≥ 18 | Frontend build |
| [Rust](https://www.rust-lang.org/) | Tauri backend |
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | C++ compiler (MSVC) |
| [CMake](https://cmake.org/) ≥ 3.14 | C++ build system |
| [vcpkg](https://github.com/microsoft/vcpkg) | C++ dependency management |
| [Git](https://git-scm.com/) | Clone auto-update repository |

## Quick Start

```bash
# Install frontend dependencies
npm install

# Dev mode (build C++ DLLs first)
cd src-lib
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd ..
npm run tauri dev
```

## One-Click Build (Release)

```bash
./build
```

This script performs three steps:
1. Build `efly_core.dll` (file transfer)
2. Clone from GitHub and build `auto-update.dll` (auto-update)
3. Build the Tauri installer bundle

Output is in `src-tauri/target/release/bundle/`.

## Usage

1. Connect two computers to the same LAN
2. Both open efly, enter a port in the **Devices** panel, click **Discover**
3. Once the peer appears, select it, click **Send File**, and pick a file
4. The receiver sees a notification — click **Accept** to save
5. A progress bar shows the transfer status in real time

## Protocol

Custom binary protocol over UDP, MTU-safe (≤ 1472 bytes).

| Type | Direction | Description |
|------|-----------|-------------|
| DISCOVERY | Broadcast | Device discovery beacon |
| FILE_ANNOUNCE | → Receiver | File metadata |
| FILE_ANNOUNCE_ACK | ← Sender | Accept / reject |
| FILE_DATA | → Receiver | Data chunk (1430 B) |
| ACK | ← Sender | Cumulative acknowledgement |
| TRANSFER_COMPLETE | → Receiver | Transfer finished |
| CANCEL | Bidirectional | Cancel transfer |

Go-Back-N: window size 16, ACK timeout 500 ms, max 10 retries.

## Auto Update

On startup the app fetches `updateLists.json` from the configured URL and compares its version against the local `version.json`. Multiple mirrors are probed via TCP latency to pick the fastest.

| Priority | Behaviour |
|----------|-----------|
| `silent` | Download & install in the background |
| `warn` | Show a dialog; user may dismiss |
| `force` | Must update or quit |

See `src-tauri/src/update.rs` and `src-tauri/src/update_conf.rs` for details.

## Project Map

| Directory | Language | Output |
|-----------|----------|--------|
| `src/` | Vue 3 + TS | Frontend UI |
| `src-tauri/` | Rust | Tauri desktop shell |
| `src-lib/` | C++ / CMake | `efly_core.dll` |
| `library/auto-update/` | C++ / CMake | `auto-update.dll` |

## License

MIT
