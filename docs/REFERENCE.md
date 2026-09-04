# FastScreen Reference 📘

Detailed technical specification, memory model, and JNI contracts for **FastScreen**.

---

## 1. Hardware & Driver Model

FastScreen utilizes a hybrid acceleration model targeting the Windows Desktop Window Manager (DWM):

*   **Primary Path — DXGI Desktop Duplication (`IDXGIOutputDuplication`)**:
    *   Requires **DirectX 11.1+** on **Windows 8, 10, or 11**.
    *   Targets Direct3D Feature Level `11_0` or `10_1` with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`.
    *   GPU-resident framebuffer access at 240–2000 FPS without CPU intervention.
*   **Hardware Scaling & Color Conversion**:
    *   Embedded HLSL Vertex Shader (`VSMain`) and Pixel Shader (`PSMain`).
    *   Hardware texture sampling with configurable filters: `Point` (0, nearest neighbor) or `Linear` (1, bilinear interpolation).
    *   In-shader swizzle from native desktop BGRA to standard 32-bit RGBA.
*   **Resilient Fallback — Win32 GDI DIBSection**:
    *   Automatically activated if DXGI returns `E_ACCESSDENIED` (e.g. non-interactive sessions, headless CI, Remote Desktop, or secure desktop).
    *   Uses high-speed `CreateDIBSection` with `BitBlt(..., SRCCOPY | CAPTUREBLT)` for hardware-backed capture at 60–240 FPS.

---

## 2. Memory Architecture & Performance Guarantees

*   **Zero Heap Allocations (`0 Bytes GC`)**:
    *   Native triple-buffered frame pooling (`POOL_SIZE = 3`) eliminates `malloc`/`free` calls per frame.
    *   Streaming modes reuse pre-allocated native staging buffers.
*   **Zero-Copy Direct ByteBuffer**:
    *   `getNextFrameDirect()` exposes native memory directly to Java via `env->NewDirectByteBuffer(pixels, size)`.
    *   Java code (and native consumers like OpenCV / TensorRT / Vulkan) can read pixels directly from mapped memory without intermediate copies.
*   **Thread Safety**:
    *   Window display affinity methods (`excludeWindow`, `includeWindow`) are thread-safe and can be invoked from any thread.
    *   Frame acquisition (`getNextFrame`, `getNextFrameDirect`) is designed for dedicated render/capture threads.

---

## 3. Window Capture Exclusion

FastScreen provides native integration with the Windows Display Affinity API to prevent visual feedback loops (Droste effect) and protect sensitive content:

| Constant | Value | Description |
|---|---|---|
| `WDA_NONE` | `0x00000000` | Window is captured normally. |
| `WDA_MONITOR` | `0x00000001` | Legacy monitor-only capture affinity. |
| `WDA_EXCLUDEFROMCAPTURE` | `0x00000011` | Window is completely excluded from capture. Capture tools record whatever is behind the window. |

### API Contracts:
*   `FastScreen.setWindowExcluded(long hwnd, boolean exclude)`: Applies display affinity directly to the Win32 `HWND`.
*   `FastScreen.excludeWindow(long hwnd)`: Convenience wrapper for `setWindowExcluded(hwnd, true)`.
*   `FastScreen.includeWindow(long hwnd)`: Convenience wrapper for `setWindowExcluded(hwnd, false)`.
*   `FastScreen.excludeWindow(String title)`: Searches top-level windows via `FindWindowA` / `EnumWindows` and applies exclusion.

---

## 4. API Specification

### Static Capture Methods

#### `BufferedImage captureScreen()`
Captures the entire primary monitor as a Java `BufferedImage` (ARGB). Returns `null` on capture failure.

#### `BufferedImage captureScreen(Rectangle rect)`
Captures a sub-rectangle `(x, y, w, h)`. Clamped automatically to monitor boundaries.

#### `int[] captureRaw(int x, int y, int w, int h)`
Captures raw 32-bit packed RGBA pixels `(A << 24 | R << 16 | G << 8 | B)`. Zero allocation on pooled reuse.

### High-FPS Streaming Methods

#### `boolean startStream(int x, int y, int width, int height)`
Initializes the DXGI duplication stream for the specified region. Returns `true` if successfully started.

#### `boolean enableHardwareScaling(int outW, int outH, boolean useLinearFilter)`
Enables GPU-side hardware downsampling using HLSL shaders. Must be called after `startStream()`.

#### `boolean hasNewFrame()`
Polls DXGI to check if the desktop surface has updated since the last frame. Non-blocking.

#### `int[] getNextFrame()`
Returns the latest frame pixel array. If `hasNewFrame()` buffered a frame, returns that frame. Returns `null` if no new frame was produced.

#### `ByteBuffer getNextFrameDirect()`
**Zero-Copy:** Returns a direct `ByteBuffer` pointing directly to native staging memory.

#### `void stopStream()`
Stops continuous capture and releases stream-specific textures and staging buffers.

---

## 5. Platform Support Matrix

| OS Version | Architecture | Minimum Direct3D | Supported |
|---|---|---|---|
| Windows 11 | x64 | D3D 11.0 | ✅ Full DXGI + Hardware Scaling |
| Windows 10 (Build 1803+) | x64 | D3D 11.0 | ✅ Full DXGI + Window Exclusion |
| Windows 10 (Build 2004+) | x64 | D3D 11.0 | ✅ `WDA_EXCLUDEFROMCAPTURE` supported |
| Windows Server 2019/2022 | x64 | D3D 11.0 / GDI | ✅ DXGI + Automatic GDI Fallback |

---

**Part of the FastJava Ecosystem** — *Making the JVM faster. ⚡*

Made with ⚡ by Andre Stubbe