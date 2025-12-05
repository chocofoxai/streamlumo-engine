# StreamLumo Engine

**StreamLumo Engine** is a headless OBS (Open Broadcaster Software) server designed for integration with the StreamLumo desktop application. It provides a lightweight, high-performance video compositing and streaming engine without the standard OBS user interface.

## Features

- **Headless Operation**: Runs as a background process or CLI tool without any UI
- **WebSocket Control**: Full control via obs-websocket protocol v5.x (port 4455 default)
- **Browser Sources**: Full support for CEF-based browser sources
- **Cross-Platform**: Supports macOS (Intel/Apple Silicon), Windows, and Linux
- **Virtual Camera**: Exposes OBS output as a virtual webcam device
- **18+ Modules**: Full OBS plugin ecosystem including obs-browser, RTMP, HLS, and more
- **Secure**: Sandboxed with proper macOS entitlements

## Supported Platforms

| Platform | Architecture | Status |
|----------|--------------|--------|
| macOS 11+ | arm64 (Apple Silicon M1/M2/M3/M4) | ✅ Production |
| macOS 11+ | x86_64 (Intel) | ✅ Production |
| Windows 10+ | x64 | ✅ Production |
| Linux (Ubuntu 20.04+) | x64 | ✅ Production |
| Linux | arm64 (Raspberry Pi 4+) | ✅ Production |

## Architecture

StreamLumo Engine is built on top of `libobs`, the core library of OBS Studio. It acts as a thin wrapper that initializes the engine, loads plugins, and exposes the WebSocket interface for control.

```
StreamLumo App (Electron)
       │
       ▼ WebSocket (JSON)
       │
StreamLumo Engine (C++)
       │
       ├── libobs (Core)
       ├── obs-websocket (Control)
       ├── obs-browser (CEF Sources)
       └── Platform Abstraction Layer
               ├── macOS (Cocoa/Foundation)
               ├── Windows (Win32)
               └── Linux (POSIX)
```

## Prerequisites

### macOS
- macOS 11.0 (Big Sur) or later
- Xcode 13+ (for building)
- CMake 3.22+
- Ninja build system

### Windows
- Windows 10 or later
- Visual Studio 2022 or MinGW-w64
- CMake 3.22+
- Ninja build system

### Linux
- Ubuntu 20.04+ or equivalent
- GCC 9+ or Clang 11+
- CMake 3.22+
- Ninja build system

## Building

See [docs/BUILDING.md](docs/BUILDING.md) for comprehensive build instructions.

### Quick Start (macOS)

```bash
# Build OBS Studio
cd ../obs-studio
cmake --preset macos-arm64
cmake --build --preset macos-arm64

# Build StreamLumo Engine
cd ../streamlumo-engine
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

### Available Presets

- `macos-arm64` - Apple Silicon Macs
- `macos-x86_64` - Intel Macs
- `macos-universal` - Universal binary
- `windows-x64` - 64-bit Windows
- `linux-x64` - 64-bit Linux
- `linux-arm64` - ARM64 Linux

## Usage

Run the engine from the command line:

```bash
./dist/macos-arm64/MacOS/streamlumo-engine --port 4455 --resolution 1920x1080
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-p, --port <PORT>` | WebSocket server port | 4455 |
| `--websocket-password <PW>` | WebSocket password | (none) |
| `-r, --resolution <WxH>` | Output resolution | 1920x1080 |
| `-f, --fps <FPS>` | Output framerate | 30 |
| `-l, --log-level <LEVEL>` | Log level (debug, info, warn, error) | info |
| `-q, --quiet` | Suppress banner output | false |

## License

StreamLumo Engine is licensed under the **GNU General Public License v2.0 (GPL-2.0)**.

This project is based on [OBS Studio](https://github.com/obsproject/obs-studio), which is also licensed under GPL-2.0.
See the [LICENSE](LICENSE) file for details.
