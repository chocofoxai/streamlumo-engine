# StreamLumo Browser Helper (macOS)

Standalone Cocoa app that will host CEF on the main thread and expose a localhost IPC surface to the headless engine.

## Current status (stub)
- Builds a GUI-less macOS bundle (`streamlumo-browser-helper.app`).
- Runs `NSApplicationMain` with an `AppDelegate` and a placeholder WebSocket stub (no actual IPC yet).
- Optionally built when `-DENABLE_BROWSER_HELPER=ON` on macOS.

## Build
```bash
cd streamlumo-engine
cmake -S . -B build-helper -DENABLE_BROWSER_HELPER=ON
cmake --build build-helper --target streamlumo-browser-helper
```
Result: `browser-helper/streamlumo-browser-helper.app` under the build tree.

## Next steps
- Replace `WebSocketStub` with a real WebSocket server (e.g., CFNetwork-based or a lightweight embedded WS library) listening on 127.0.0.1:4777.
- Define IPC messages: `initBrowser`, `updateBrowser`, `frame`, `audio`, `disposeBrowser`, `error`, `ready`.
- Embed/initialize CEF in `AppDelegate` (OSR, no windows) once IPC is wired.
- Add engine-side launcher + WS client to proxy browser sources to OBS textures.
