# StreamLumo Engine - Cross-Platform Build Guide

This document provides comprehensive instructions for building StreamLumo Engine on all supported platforms: **macOS**, **Windows**, and **Linux**.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Building on macOS](#building-on-macos)
4. [Building on Windows](#building-on-windows)
5. [Building on Linux](#building-on-linux)
6. [CMake Presets Reference](#cmake-presets-reference)
7. [Troubleshooting](#troubleshooting)

---

## Overview

StreamLumo Engine is a headless OBS Studio server that provides:
- WebSocket API (obs-websocket v5.x protocol)
- Full OBS Studio plugin ecosystem
- Browser sources (CEF-based)
- Virtual camera output
- Platform-native optimizations

### Supported Platforms

| Platform | Architecture | Status | Notes |
|----------|--------------|--------|-------|
| macOS 11+ | arm64 (Apple Silicon) | ✅ Production | M1/M2/M3/M4 |
| macOS 11+ | x86_64 (Intel) | ✅ Production | Intel Macs |
| macOS 11+ | Universal | ✅ Production | Fat binary |
| Windows 10+ | x64 | ✅ Production | MSVC or MinGW |
| Linux (Ubuntu 20.04+) | x64 | ✅ Production | GCC 9+ |
| Linux (Ubuntu 20.04+) | arm64 | ✅ Production | Raspberry Pi 4+ |

---

## Prerequisites

### All Platforms

- **CMake** 3.22 or later
- **Ninja** build system (recommended)
- **Git** for source control
- **OBS Studio** source code (built)

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install build tools via Homebrew
brew install cmake ninja

# Install OBS dependencies
brew install ffmpeg x264 mbedtls jansson
```

### Windows

```powershell
# Option 1: Visual Studio 2022 (recommended)
# Install from https://visualstudio.microsoft.com/
# Include "Desktop development with C++" workload

# Option 2: MinGW-w64
# Install from https://www.msys2.org/

# Install CMake and Ninja
winget install Kitware.CMake
winget install Ninja-build.Ninja

# Or via Chocolatey
choco install cmake ninja
```

### Linux (Ubuntu/Debian)

```bash
# Install build essentials
sudo apt update
sudo apt install build-essential cmake ninja-build git

# Install OBS dependencies
sudo apt install \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libx264-dev libmbedtls-dev libjansson-dev \
    libpulse-dev libasound2-dev \
    libgl1-mesa-dev libwayland-dev libxcomposite-dev \
    libv4l-dev libpipewire-0.3-dev
```

### Linux (Fedora/RHEL)

```bash
# Install build essentials
sudo dnf install cmake ninja-build gcc-c++ git

# Install OBS dependencies
sudo dnf install \
    ffmpeg-devel x264-devel mbedtls-devel jansson-devel \
    pulseaudio-libs-devel alsa-lib-devel \
    mesa-libGL-devel wayland-devel libXcomposite-devel \
    libv4l-devel pipewire-devel
```

---

## Building on macOS

### Step 1: Build OBS Studio

```bash
cd /path/to/obs-studio

# For Apple Silicon (M1/M2/M3/M4)
cmake --preset macos-arm64
cmake --build --preset macos-arm64

# For Intel Macs
cmake --preset macos-x86_64
cmake --build --preset macos-x86_64

# For Universal binary
cmake --preset macos-universal
cmake --build --preset macos-universal
```

### Step 2: Build StreamLumo Engine

```bash
cd /path/to/streamlumo-engine

# Apple Silicon
cmake --preset macos-arm64
cmake --build --preset macos-arm64

# Intel
cmake --preset macos-x86_64
cmake --build --preset macos-x86_64

# Universal
cmake --preset macos-universal
cmake --build --preset macos-universal
```

### Step 3: Install

```bash
cmake --install build/macos-arm64 --prefix dist/macos-arm64
```

### Code Signing (Production)

```bash
# Sign the engine
codesign --deep --force --sign "Developer ID Application: Your Name" \
    --entitlements entitlements/streamlumo-engine.entitlements \
    dist/macos-arm64/MacOS/streamlumo-engine

# Notarize for distribution
xcrun notarytool submit streamlumo-engine.zip \
    --apple-id "your@email.com" \
    --team-id "YOUR_TEAM_ID" \
    --password "app-specific-password" \
    --wait
```

---

## Building on Windows

### Step 1: Build OBS Studio

```powershell
cd C:\path\to\obs-studio

# Using Ninja (faster)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Using Visual Studio
cmake --preset windows-x64-msvc
cmake --build --preset windows-x64-msvc --config Release
```

### Step 2: Build StreamLumo Engine

```powershell
cd C:\path\to\streamlumo-engine

# Using Ninja
cmake --preset windows-x64
cmake --build --preset windows-x64

# Using Visual Studio
cmake --preset windows-x64-msvc
cmake --build --preset windows-x64-msvc --config Release
```

### Step 3: Install

```powershell
cmake --install build\windows-x64 --prefix dist\windows-x64
```

### Deployment

Copy required DLLs alongside the executable:
- `obs.dll`
- `obs-frontend-api.dll`
- Qt6 DLLs (Core, Network, Gui)
- ffmpeg DLLs
- OpenSSL DLLs

---

## Building on Linux

### Step 1: Build OBS Studio

```bash
cd /path/to/obs-studio

# x64
cmake --preset linux-x64
cmake --build --preset linux-x64

# ARM64 (Raspberry Pi 4+)
cmake --preset linux-arm64
cmake --build --preset linux-arm64
```

### Step 2: Build StreamLumo Engine

```bash
cd /path/to/streamlumo-engine

# x64
cmake --preset linux-x64
cmake --build --preset linux-x64

# ARM64
cmake --preset linux-arm64
cmake --build --preset linux-arm64
```

### Step 3: Install

```bash
sudo cmake --install build/linux-x64 --prefix /opt/streamlumo
```

### Systemd Service

Create `/etc/systemd/system/streamlumo.service`:

```ini
[Unit]
Description=StreamLumo Headless OBS Server
After=network.target

[Service]
Type=simple
User=streamlumo
ExecStart=/opt/streamlumo/bin/streamlumo-engine
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable streamlumo
sudo systemctl start streamlumo
```

---

## CMake Presets Reference

### Configure Presets

| Preset | Platform | Architecture | Description |
|--------|----------|--------------|-------------|
| `macos-arm64` | macOS | arm64 | Apple Silicon (M1+) |
| `macos-x86_64` | macOS | x86_64 | Intel Macs |
| `macos-universal` | macOS | arm64 + x86_64 | Fat binary |
| `macos-arm64-debug` | macOS | arm64 | Debug build |
| `windows-x64` | Windows | x64 | Ninja generator |
| `windows-x64-debug` | Windows | x64 | Debug build |
| `windows-x64-msvc` | Windows | x64 | Visual Studio |
| `linux-x64` | Linux | x64 | Standard build |
| `linux-x64-debug` | Linux | x64 | Debug build |
| `linux-arm64` | Linux | arm64 | Raspberry Pi 4+ |

### Build Presets

Same names as configure presets. Use with `cmake --build --preset <name>`.

### Custom Variables

Override via `-D` or in `CMakeUserPresets.json`:

| Variable | Default | Description |
|----------|---------|-------------|
| `OBS_SOURCE_DIR` | `../obs-studio` | Path to OBS source |
| `OBS_BUILD_DIR` | `../obs-studio/build_<arch>` | Path to OBS build |
| `CEF_ROOT_DIR` | `../deps/cef_binary_<platform>` | CEF for browser sources |
| `ENABLE_BROWSER` | `ON` | Enable browser sources |
| `ENABLE_WEBSOCKET` | `ON` | Enable WebSocket server |

---

## Troubleshooting

### Common Issues

#### "obsconfig.h not found"

OBS Studio hasn't been built. Build OBS first:
```bash
cd ../obs-studio
cmake --preset <your-preset>
cmake --build --preset <your-preset>
```

#### "Library not loaded: @rpath/libobs.framework"

Run from the dist directory or set `DYLD_LIBRARY_PATH`:
```bash
export DYLD_LIBRARY_PATH=/path/to/obs-studio/build/libobs/Release
```

#### Windows: "obs.dll not found"

Copy OBS DLLs to the executable directory or add to `PATH`.

#### Linux: "libobs.so: cannot open shared object file"

Set library path:
```bash
export LD_LIBRARY_PATH=/opt/streamlumo/lib:$LD_LIBRARY_PATH
```

Or add to ldconfig:
```bash
echo "/opt/streamlumo/lib" | sudo tee /etc/ld.so.conf.d/streamlumo.conf
sudo ldconfig
```

### Debug Build

For debugging, use debug presets:
```bash
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
```

### Verbose Build Output

```bash
cmake --build --preset macos-arm64 -- -v
```

### Clean Rebuild

```bash
rm -rf build/macos-arm64
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

---

## Platform Abstraction Layer

StreamLumo Engine includes a production-grade platform abstraction layer in `src/platform/`:

- `platform.h` - Cross-platform API declarations
- `platform_common.cpp` - Shared utility functions
- `platform_macos.cpp` - macOS/Cocoa implementations
- `platform_windows.cpp` - Windows/Win32 implementations
- `platform_linux.cpp` - Linux/POSIX implementations

### Available APIs

```cpp
namespace streamlumo::platform {
    // System Information
    PlatformType getPlatformType();
    SystemInfo getSystemInfo();
    
    // Path Management
    PathInfo getPaths();
    std::string getExecutablePath();
    std::string getAppDataDir();
    
    // Plugin Discovery
    PluginInfo getPluginInfo();
    
    // Dynamic Libraries
    LibraryHandle loadLibrary(const std::string& path);
    void* getLibrarySymbol(LibraryHandle, const std::string& name);
    
    // Process Management
    int executeCommand(const std::string& cmd, std::string* output);
    
    // Threading
    void setThreadName(const std::string& name);
    bool setThreadPriority(ThreadPriority priority);
    
    // High-Resolution Timing
    uint64_t getTimestampNanos();
}
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
jobs:
  build:
    strategy:
      matrix:
        include:
          - os: macos-14
            preset: macos-arm64
          - os: macos-13
            preset: macos-x86_64
          - os: windows-latest
            preset: windows-x64
          - os: ubuntu-22.04
            preset: linux-x64
    
    runs-on: ${{ matrix.os }}
    
    steps:
      - uses: actions/checkout@v4
      
      - name: Configure
        run: cmake --preset ${{ matrix.preset }}
      
      - name: Build
        run: cmake --build --preset ${{ matrix.preset }}
      
      - name: Test
        run: ctest --preset ${{ matrix.preset }}
```

---

## License

StreamLumo Engine is licensed under GPL-2.0-or-later, same as OBS Studio.

Copyright (C) 2024 StreamLumo / Intelli-SAAS
