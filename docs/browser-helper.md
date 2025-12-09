# Browser Helper Architecture (macOS, experimental)

## Goal
Host CEF in a dedicated macOS app with a proper `NSApplication` main-thread run loop, and communicate with the headless engine over IPC (planned: WebSocket on 127.0.0.1:4777).

## Message sketch (JSON over TCP, line-delimited)
- Engine → Helper
  - `handshake {type:"handshake", token, client, v}`
  - `ping {type:"ping", token, client, v}`
  - `initBrowser {type:"initBrowser", id, url?, width?, height?, fps?, css?, v, token}`
  - `updateBrowser {type:"updateBrowser", id, url?, width?, height?, css?, js?, v, token}`
  - `disposeBrowser {type:"disposeBrowser", id, v, token}`
  - (future) `captureFrame {id,v}` (optional pull; push is preferred)
- Helper → Engine
  - `helper_ready {type:"helper_ready", port, v}` (preamble after connect)
  - `handshake_ack {type:"handshake_ack", status:"ok", v}`
  - `pong {type:"pong", v}`
  - `browserReady {type:"browserReady", id, status:"ok", v}`
  - `browserUpdated {type:"browserUpdated", id, status:"ok", v}`
  - `browserDisposed {type:"browserDisposed", id, status:"ok", v}`
  - (future) `frame {id,fmt:"BGRA",width,height,stride,timestampUs,payloadBase64,v}`
  - (future) `audio {id,sampleRate,channels,layout,payloadBase64,v}`
  - `error {type:"error", message, v}`

## Build (helper only)
```bash
cd streamlumo-engine
cmake -S . -B build-helper -DENABLE_BROWSER_HELPER=ON
cmake --build build-helper --target streamlumo-browser-helper
```
Output: `build-helper/browser-helper/streamlumo-browser-helper.app`

For a helper-only build (no OBS deps), build from the subdir:
```bash
cd streamlumo-engine
cmake -S browser-helper -B build-helper-only
cmake --build build-helper-only --target streamlumo-browser-helper
```

## Runtime config (env)
- `BROWSER_HELPER_PORT` (default 4777)
- `BROWSER_HELPER_TOKEN` (required for auth; engine will generate an ephemeral one if not provided)

Engine will export these env vars before launching the helper; the helper enforces token on `handshake`/`ping`.

## Smoke test
With the helper running:
```bash
export BROWSER_HELPER_TOKEN=smoketoken
python3 scripts/test-helper-handshake.py --port 4777 --token "$BROWSER_HELPER_TOKEN"
```
Expected: `helper_ready`, `handshake_ack`, `pong` then PASS.

## Next steps
1) Implement real WebSocket server inside `browser-helper` (replace `WebSocketStub`).
2) Add CEF initialization (OSR, no UI windows) in `AppDelegate` once IPC is wired.
3) Engine-side launcher + WS client to proxy frames into OBS textures.
4) Optional: move to IOSurface/shared-memory for frame transport; keep WS for control plane.
