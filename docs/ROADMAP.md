# FastScreen Roadmap 🗺️

**Vision:** To provide the fastest, zero-allocation native primitives for screen capture and live desktop ingestion in the Java ecosystem.

---

## 🟢 v0.1.0: Core Engine Release (Completed)
- [x] **DXGI Desktop Duplication**: DirectX 11 hardware-accelerated capture pipeline.
- [x] **Zero-Copy Streaming**: `DirectByteBuffer` exposing native GPU staging memory.
- [x] **Hardware Scaling**: HLSL vertex/pixel shader pipeline for GPU downsampling.
- [x] **Native Frame Pooling**: Triple-buffering (`POOL_SIZE = 3`) with zero Java heap allocations.

---

## 🟢 v0.1.1: Window Exclusion & Resilient Fallback (Completed)
- [x] **Window Capture Exclusion**: Win32 `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` (0x11).
- [x] **Title-Based Exclusion**: Automatic top-level window search and affinity tagging.
- [x] **GDI DIBSection Fallback**: Resilient fallback path for headless sessions, RDP, and VMs.
- [x] **Dynamic Monitor Bounds**: Automatic resolution adaptation and safe dimension clamping.
- [x] **Visual Studio 2026/2022 Auto-Detection**: Universal build automation via `vswhere.exe`.
- [x] **Real-World Benchmark Verification**: 229.8 FPS measured streaming throughput.

---

## 🟢 v0.1.2: FastImage Ecosystem Interop & Live Anti-Aliasing (Current)
- [x] **Zero-Copy FastImage Streaming**: `getNextFrameImage()` wrapping DXGI staging memory directly.
- [x] **Direct Region Capture to FastImage**: `captureImage(Rectangle)` bypassing BufferedImage overhead.
- [x] **Interactive Live Anti-Aliasing**: Dynamic runtime hotkey `[A]` switching between nearest-neighbor and linear texture sampling with live telemetry.

---

## 🟡 v0.2.0: GPU-to-GPU Zero-Copy Interop
- [ ] **Direct GPU-to-Vulkan Interop**: Windows Shared Surface Handle (`IDXGIResource::GetSharedHandle`) to import DXGI frames directly into Vulkan textures with 0 CPU readback.
- [ ] **Hardware Mouse Cursor Compositing**: In-shader pointer composition support.
- [ ] **Multi-Adapter Selection**: Explicit GPU adapter selection for hybrid laptops (iGPU vs dGPU).

---

## 🟠 v0.3.0: Windows Graphics Capture (WGC)
- [ ] **Modern WinRT Capture API**: Targeted per-window capture mode (`GraphicsCaptureItem`).
- [ ] **Capture Border Control**: Disable yellow capture border on Windows 11.
- [ ] **HDR & 10-Bit Color**: `DXGI_FORMAT_R10G10B10A2_UNORM` support for HDR displays.

---

## 🔴 v1.0.0: Enterprise Production Hardening
- [ ] **Audio Loopback Capture**: Integrated WASAPI desktop audio stream synchronization.
- [ ] **Extended OS Parity**: Investigate Linux PipeWire / Wayland capture backends.
- [ ] **Long-Run Telemetry & Stress Suite**: 48h soak testing with zero leaks.

---

**Part of the FastJava Ecosystem** — *Making the JVM faster. ⚡*