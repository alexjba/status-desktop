> Draft to show the shape of the change. Spike quality; the master-parity rework (v2) is in progress and will be pushed here.

Part of status-im/status-app#21528 (Android UI memory floor).

## Why
Opening the Browser loads Chromium into the app's UI process: +111 MB PSS on a Redmi A5, and destroying every WebView afterwards gives back only 21 MB (WebView provider code, Chromium heaps and ~31 Chromium threads stay for the life of the process). With the WebViews in a separate `:browser` process, closing the Browser returns 117 of 131 MB (n=3, Redmi A5).

## What changes
1. **WebView in its own panel window** (`WebViewWindow`): each WebView is hosted in a `TYPE_APPLICATION_PANEL` window that tracks the QML item, so the activity window never needs HWUI for the browser. It routes touches over host views (the drawer edge strip) back to the host and hands focus back on outside touches.
2. **WebView in a browser process** (API 30+):
   - `BrowserProcessService`: a bound service in the process the app declares. It owns the `android.webkit.WebView` (`WebViewCore`) and renders it with `SurfaceControlViewHost`.
   - `RemoteWebView` (UI process): Java proxy with the same surface `MobileWebView` exposes to native/QML. It places a `SurfaceView` in the panel window and attaches the `SurfacePackage`. QML/C++ API unchanged; the dApp bridge (injected scripts, postMessage, JS results) goes over Binder.
   - `BrowserIpc` / `BrowserProcessClient`: Bundle-based `call(method, args)` IPC both ways, same-UID check, bind/death handling and replay of state into a respawned process.
   - `WebViewEngine` / `WebViewSink`: split of the old `MobileWebView` logic so the same core runs in-process (API < 30) or in `:browser`.
   - `DataClearManager`: runs in `:browser` when remote (one process per WebView data dir).
3. **Fallback**: below API 30, or when the host doesn't opt in, the in-process path is used.

## What the app has to do (status-app side, ~10 lines)
- Declare the service in its own process in `AndroidManifest.xml`:
  ```xml
  <service android:name="org.mobilewebview.BrowserProcessService"
           android:process=":browser" android:exported="false" />
  ```
- Opt in to remote WebViews at startup (today: `MobileWebView.setRemoteProcess(true)` from `WindowSurfaceMode`).
- Never touch `WebView`/`CookieManager`/`WebStorage` from the UI process on API 30+.

## Status / known gaps (being addressed in v2)
- Geometry: the crop-based navbar handling is replaced by frame-by-frame resize (fixes "can't scroll back to the top").
- Focus/IME/Back across the Qt window, the panel and the embedded window (keyboard not coming back, IME hide acting as Back).
- Drawer edge swipe over the page only on API 34+ (`setTouchableRegion`); a strip window above the page for API 30–33.
- Kill/restore rules: kill after 30 s hidden (not on popup cover), media keep-alive, save state per navigation, lazy per-tab restore, crash handling (respawn once, no loop), pending dApp requests rejected on kill/death/navigation, oneway calls only.
- Activity-dependent WebChromeClient features checked against the in-process behaviour.

## Testing
Manual on Redmi A5 (Android 15), Samsung S21 FE, Lenovo tablet; API 30 emulator for the < 34 paths. No automated coverage for IPC/death/focus yet.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_012EtNak9P8hcfamFzw2icjy
