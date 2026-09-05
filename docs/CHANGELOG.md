# Changelog 📝

All notable changes to **FastScreen** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.2] - 2026-09-04

### Added
- **FastImage Ecosystem Integration**:
  - Added zero-copy `FastImage` bridge methods:
    - `FastScreen.getNextFrameImage()`: Zero-copy wrapping of the active streaming buffer directly into a `FastImage`.
    - `FastScreen.captureImage(Rectangle rect)`: Direct native screenshot capture returning an off-heap `FastImage`.
  - Enables instant chaining with SIMD resize, Dual-Kawase blur, and color conversions.
- **Interactive Anti-Aliasing in Demo**:
  - Added runtime hotkey `[A]` in `examples/Demo` to toggle between Point (nearest neighbor) and Linear (bilinear anti-aliasing) GPU texture sampling on the fly.
  - Live window title telemetry shows current scaling mode and hardware FPS.

---

## [0.1.1] - 2026-09-04

### Added
- **Window Capture Exclusion**:
  - Implemented Win32 `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` (`0x00000011`).
  - Exposed `FastScreen.excludeWindow(long hwnd)` and `FastScreen.includeWindow(long hwnd)`.
  - Added top-level window title enumeration via `FastScreen.excludeWindow(String title)`.
  - Prevents recursive mirror feedback loops (Droste effect) when viewing screen streams in a window.
- **Resilient GDI DIBSection Fallback**:
  - Automatically activates when DXGI Desktop Duplication is unavailable (e.g. non-interactive sessions, headless CI, Remote Desktop, or UAC secure desktop).
  - Retains zero JVM heap allocations via native frame pooling (`POOL_SIZE = 3`).
- **Dynamic Frame Dimension Queries**:
  - Added `getFrameWidth()` and `getFrameHeight()` native bindings.
  - Updated `captureRaw(...)` and `captureScreen(...)` to automatically adapt to physical monitor resolution bounds.
  - Added zero-argument `captureScreen()` for instant full-desktop capture.
- **Modernized Build Chain**:
  - Updated `compile.bat` with `vswhere.exe` for Visual Studio 2026 and 2022 auto-detection.
  - Automatic deployment of `fastscreen.dll` to `src/main/resources/native/` and `~/.fastcore/native/fastscreen/`.
- **Real-World Benchmark Suite**:
  - Validated streaming throughput: **229.8 FPS** at 1440×960 (1,149 frames in 5s, 0 dropped frames, <1.00ms latency).

---

## [0.1.0] - 2026-06-14

### Added
- Initial release of FastScreen.
- Direct3D 11 & DXGI 1.2+ Desktop Duplication API integration.
- Single screenshot capture (`captureScreen`, `captureRaw`).
- Continuous streaming mode (`startStream`, `getNextFrame`).
- Embedded HLSL vertex and pixel shaders for GPU-accelerated scaling and BGRA-to-RGBA swizzling.
- Zero-copy native buffer access via `getNextFrameDirect()` (`DirectByteBuffer`).
- Multi-monitor output enumeration.
- FastCore integration for unified JNI library loading.

