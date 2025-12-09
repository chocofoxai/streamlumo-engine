# obs-browser-bridge Plugin Architecture

## Overview

The `obs-browser-bridge` plugin provides browser source functionality for OBS by communicating with an external browser helper process via TCP IPC. This approach ensures CEF (Chromium Embedded Framework) runs in an isolated process, preventing crashes or GPU issues from affecting OBS.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      OBS / StreamLumo Engine                    │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                 obs-browser-bridge Plugin                  │ │
│  │  ┌─────────────────┐  ┌─────────────────────────────────┐ │ │
│  │  │ BrowserSource 1 │  │ BrowserBridgeManager (Singleton)│ │ │
│  │  │ (1280x720 URL)  ├──┤  - Launches helper process      │ │ │
│  │  └─────────────────┘  │  - Manages IPC connection       │ │ │
│  │  ┌─────────────────┐  │  - Routes frames to sources     │ │ │
│  │  │ BrowserSource 2 │  │                                 │ │ │
│  │  │ (800x600 Alert) ├──┤           IPCClient             │ │ │
│  │  └─────────────────┘  │  - TCP JSON-line protocol       │ │ │
│  │        ...            │  - Frame receive thread         │ │ │
│  └───────────────────────┴─────────────────────────────────┘ │ │
└────────────────────────────────────────────────────────────────┘
                              │
                    TCP Port 4777 (JSON-line)
                              │
┌─────────────────────────────────────────────────────────────────┐
│              streamlumo-browser-helper.app                      │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                    BrowserManager                          │ │
│  │  ┌─────────────────┐  ┌─────────────────┐                 │ │
│  │  │ Browser Inst 1  │  │ Browser Inst 2  │                 │ │
│  │  │ CefBrowserHost  │  │ CefBrowserHost  │                 │ │
│  │  │ - OnPaint →     │  │ - OnPaint →     │                 │ │
│  │  │   frameReady    │  │   frameReady    │                 │ │
│  │  └─────────────────┘  └─────────────────┘                 │ │
│  │                                                            │ │
│  │  ┌──────────────────────────────────────────────────────┐ │ │
│  │  │            CEF (Chromium Embedded Framework)          │ │ │
│  │  │  - Renderer subprocess                                │ │ │
│  │  │  - GPU subprocess                                     │ │ │
│  │  │  - Plugin subprocess                                  │ │ │
│  │  └──────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Components

### 1. obs-browser-bridge Plugin (OBS side)

**Files:**
- `plugin-main.cpp` - OBS module entry point, registers browser source
- `browser-bridge-manager.hpp/cpp` - Singleton managing helper process and IPC
- `browser-bridge-source.hpp/cpp` - OBS source implementation
- `ipc-client.hpp/cpp` - TCP client with receive thread
- `frame-decoder.hpp/cpp` - Base64 to BGRA decoder

**Key Features:**
- Registers as `browser_bridge_source` OBS source type
- Launches helper process on first browser source creation
- Multiple browser sources share single helper process
- Frames received via TCP and uploaded to OBS textures

### 2. streamlumo-browser-helper.app (CEF side)

**Features:**
- Standalone macOS app bundle with CEF framework
- Includes all CEF subprocess helpers (GPU, Renderer, Plugin, Alerts)
- Off-screen rendering via CefRenderHandler::OnPaint
- Base64 encodes BGRA frames for IPC

## IPC Protocol

JSON-line protocol over TCP (port 4777 by default).

### Commands (Plugin → Helper)

```json
{"type":"initBrowser","browserId":"browser_123","url":"https://example.com","width":1280,"height":720,"fps":30}
```

```json
{"type":"disposeBrowser","browserId":"browser_123"}
```

### Events (Helper → Plugin)

```json
{"type":"helper_ready"}
```

```json
{"type":"browserReady","browserId":"browser_123"}
```

```json
{"type":"frameReady","browserId":"browser_123","width":1280,"height":720,"data":"<base64 BGRA>"}
```

## Building

```bash
cd streamlumo-engine
cmake --preset macos-arm64 -DENABLE_BROWSER_HELPER=ON
cmake --build build/macos-arm64 -j8
```

## Output Files

After build:
```
build/macos-arm64/
├── streamlumo-engine              # Main engine executable
├── Helpers/
│   └── streamlumo-browser-helper.app/    # CEF helper bundle
└── PlugIns/
    └── obs-browser-bridge.so      # Browser source plugin
```

## Future Enhancements

1. **Windows/Linux Support**: Create equivalent helper executables
2. **Shared Memory IPC**: Use SHM instead of TCP for lower latency
3. **Interaction Support**: Mouse/keyboard events via IPC
4. **CSS Injection**: Support custom CSS per browser instance
5. **Page Lifecycle**: Handle page load events, console logs, errors

## Cross-Platform Strategy

| Platform | Helper Implementation |
|----------|----------------------|
| macOS    | streamlumo-browser-helper.app (CEF, Obj-C) |
| Windows  | streamlumo-browser-helper.exe (CEF, C++) |
| Linux    | streamlumo-browser-helper (CEF, C++) |

All platforms use the same IPC protocol, only the helper binary differs.
