# The Philosophy of FastScreen 💡

> [!IMPORTANT]
> **"Keine Kopien. Niemals. Kritischer JNI-Pfad. Native-First Performance."**

FastScreen is built on the conviction that real-time computer vision, autonomous agents, and high-performance desktop graphics in Java should never be crippled by legacy OS abstractions or JVM garbage collection overhead.

---

## Core Tenets

### 1. Direct GPU Compositor Access
Standard Java treats screen capture as an afterthought, relying on AWT `Robot` and legacy GDI `BitBlt` calls tied to the Event Dispatch Thread (EDT). FastScreen breaks out of this limitation by directly communicating with the Windows Desktop Window Manager (DWM) compositor via the DirectX Graphics Infrastructure (DXGI 1.2+). Frames are duplicated directly from the GPU framebuffer at hardware refresh rates.

### 2. Zero-Copy JNI Architecture
Data should move across the JNI boundary only when strictly necessary, and never through heap allocations.
*   **0 Heap Bytes**: Native frame pooling (`POOL_SIZE = 3`) eliminates `malloc` churn.
*   **Direct Memory Mapping**: `getNextFrameDirect()` exposes native memory directly to the JVM via `DirectByteBuffer`, allowing zero-copy sharing with native libraries (OpenCV, Vulkan, Direct3D).

### 3. In-GPU Hardware Processing
Pixel downsampling and format conversions belong on the GPU, not the CPU. FastScreen uses dedicated HLSL vertex and pixel shaders to scale textures and convert color formats (BGRA to RGBA) before memory is ever mapped for CPU readback.

### 4. Compositor-Level Window Control
Screen capture should be controllable at the OS window manager level. FastScreen incorporates native Win32 Display Affinity (`WDA_EXCLUDEFROMCAPTURE`), empowering developers to render UI overlays, capture tools, and live warpers that capture *through* themselves without recursive mirror feedback (Droste effect).

### 5. Deterministic Real-Time Throughput
Autonomous AI agents and gaming bots cannot tolerate unpredictable 50ms GC pauses or dropped frames. FastScreen guarantees deterministic sub-millisecond frame acquisition latencies and continuous streaming up to 2000 FPS.

### 6. FastJava Blueprint Consistency
As a core pillar of the **FastJava** ecosystem:
*   **Native Backend**: Hand-tuned C++17 with Direct3D 11 and DXGI 1.2.
*   **Unified Loading**: Powered by `FastCore` for seamless zero-dependency deployment.
*   **Production Quality**: MIT licensed, resilient fallback, thoroughly profiled with JMH.

---

**⚡ FastScreen — Powering the next generation of Native Java.**

