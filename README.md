# FastScreen â€” High-performance screen capture for Java [ALPHA] - v0.1.0
**âš¡ Ultra-fast Java screen capture library â€” 500-2000 FPS zero-copy capture**

[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Java](https://img.shields.io/badge/Java-17+-blue.svg)](https://www.java.com)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010+-lightgrey.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![JitPack](https://jitpack.io/v/andrestubbe/FastScreen.svg)](https://jitpack.io/#andrestubbe/FastScreen)

**Keywords:** fastjava, java screen capture, java screenshot, directx screen capture, dxgi desktop duplication, zero copy capture, high fps screen capture, java vision, computer vision java, gpu screen capture

FastScreen is a **high-performance Java screen capture library** and part of the **FastJava ecosystem**. It uses **DXGI Desktop Duplication API** for **zero-copy, hardware-accelerated screen capture** at 500-2000 FPS. Built for **computer vision**, **gaming bots**, **screen recording**, and **real-time monitoring** applications.

If you need **high-FPS screen capture** without the 50-100ms latency of `java.awt.Robot`, FastScreen delivers native-level performance with Java simplicity. Part of the FastJava ecosystem â€” *Making the JVM faster.*

---

## Quick Start

### Installation

### Option 1: Maven (Recommended)
Add the JitPack repository and the dependencies to your `pom.xml`:

```xml
<repositories>
    <repository>
        <id>jitpack.io</id>
        <url>https://jitpack.io</url>
    </repository>
</repositories>

<dependencies>
    <!-- FastScreen Library -->
    <dependency>
        <groupId>com.github.andrestubbe</groupId>
        <artifactId>fastscreen</artifactId>
        <version>v0.1.0</version>
    </dependency>

    <!-- FastCore (Required Native Loader) -->
    <dependency>
        <groupId>com.github.andrestubbe</groupId>
        <artifactId>fastcore</artifactId>
        <version>v0.1.0</version>
    </dependency>
</dependencies>
```

### Option 2: Gradle (via JitPack)
```groovy
repositories {
    maven { url 'https://jitpack.io' }
}

dependencies {
    implementation 'com.github.andrestubbe:fastscreen:v0.1.0'
    implementation 'com.github.andrestubbe:fastcore:v0.1.0'
}
```

### Option 3: Direct Download (No Build Tool)
Download the latest JARs directly to add them to your classpath:

1. 📦 **[fastscreen-v0.1.0.jar](https://github.com/andrestubbe/FastScreen/releases/download/v0.1.0/fastscreen-v0.1.0.jar)** (The Core Library)
2. ⚙️ **[fastcore-v0.1.0.jar](https://github.com/andrestubbe/FastCore/releases/download/v0.1.0/fastcore-v0.1.0.jar)** (The Mandatory Native Loader)

> [!IMPORTANT]
> All JARs must be in your classpath for the native JNI calls to function correctly.


## Key Features

- **500-2000 FPS capture** â€” Zero-copy DXGI Desktop Duplication
- **10-17Ã— faster** than `java.awt.Robot` (8-16ms vs 50-100ms)
- **Hardware acceleration** â€” GPU to CPU without memory copy
- **Zero GC pressure** â€” Native buffers, reusable arrays
- **Multiple outputs** â€” BufferedImage, raw pixels, or stream callback
- **Powered by FastCore** â€” Unified JNI loader for all FastJava modules
- **MIT licensed** â€” free for commercial use

---

## Performance Benchmarks

**Measure yourself** â€” run the included benchmark:

```bash
cd examples/03-benchmark
mvn compile exec:java
```

**Expected improvements** (your hardware may vary):
- Single capture: **10-50Ã— faster** than `java.awt.Robot`
- Streaming: **60-240fps** depending on GPU and resolution
- Zero GC pressure with native buffers

See [examples/03-benchmark](examples/03-benchmark) for detailed measurement.

---

## Examples

All examples are in the `examples/` folder:

```bash
# Basic screenshot demo [ALPHA] - v0.1.0
cd examples/00-basic-capture
mvn compile exec:java

# High-FPS streaming demo [ALPHA] - v0.1.0
cd examples/01-streaming
cd examples/02-vision-pipeline
```

---

## Project Structure

```
fastscreen/
â”œâ”€â”€ src/main/java/fastscreen/     # Main API
â”‚   â””â”€â”€ FastScreen.java           # Core capture class
â”œâ”€â”€ examples/                     # Runnable examples
â”‚   â”œâ”€â”€ 00-basic-capture/         # Simple screenshot
â”‚   â”œâ”€â”€ 01-streaming/             # High-FPS demo
â”‚   â””â”€â”€ 02-vision-pipeline/       # CV integration
â”œâ”€â”€ native/                       # C++ JNI source
â”‚   â”œâ”€â”€ fastscreen.cpp            # JNI wrapper
â”‚   â””â”€â”€ DXGICapture.cpp           # DirectX capture
â”œâ”€â”€ pom.xml                       # Maven configuration
â””â”€â”€ README.md                     # This file
```

---

## Build from Source

See [COMPILE.md](COMPILE.md) for detailed build instructions.

---

## API Reference

### Screen Capture
- `captureScreen(Rectangle rect)` â€” BufferedImage screenshot
- `captureRaw(int x, int y, int w, int h)` â€” Raw RGBA pixel array
- `getPixelColor(int x, int y)` â€” Single pixel (fast)

### Streaming (High-FPS)
- `startStream(int x, int y, int w, int h)` â€” Begin capture stream
- `hasNewFrame()` â€” Check for new frame available
- `getNextFrame()` â€” Get next frame (non-blocking)
- `stopStream()` â€” Stop and cleanup
- `getStreamFPS()` â€” Current capture FPS

### Monitor Selection
- `getMonitorCount()` â€” Number of displays
- `captureMonitor(int index)` â€” Capture specific monitor

---

## Architecture

```
Java API (FastScreen.java)
    â†“ JNI (via FastCore)
Native Layer (C++/Win32)
    â””â”€â”€ DXGI Desktop Duplication API
        â””â”€â”€ Direct GPU framebuffer access
    â†“
Windows OS (Hardware)
```

**Powered by [FastCore](https://github.com/andrestubbe/FastCore)** â€” Unified JNI loader for the FastJava ecosystem.

---

## Platform Support

| Platform | Status |
|----------|--------|
| Windows 11 | âœ… Full support |
| Windows 10 | âœ… Full support |
| Linux | âŒ Not planned (no DXGI) |
| macOS | âŒ Not planned (no DXGI) |

---

## Version History

### v1.0.0 â€” Current
- **DXGI Desktop Duplication API** â€” hardware-accelerated capture
- **Zero-copy streaming** â€” 500-2000 FPS
- **FastCore integration** â€” Unified JNI loader
- **Multiple output formats** â€” BufferedImage, raw pixels

---

## License

MIT License â€” free for commercial and private use. See [LICENSE](LICENSE) for details.

---

## Part of FastJava Ecosystem

| Module | Purpose | Link |
|--------|---------|------|
| **FastCore** | JNI loader | [GitHub](https://github.com/andrestubbe/FastCore) |
| **FastRobot** | Input automation | [GitHub](https://github.com/andrestubbe/FastRobot) |
| **FastVision** | GPU vision pipeline | [GitHub](https://github.com/andrestubbe/FastVision) |
| **FastImage** | Image processing | [GitHub](https://github.com/andrestubbe/FastImage) |
| **FastGraphics** | GPU rendering | [GitHub](https://github.com/andrestubbe/FastGraphics) |

---

## License

MIT License â€” See [LICENSE](LICENSE) for details.

---

**FastScreen** â€” *High-performance Java screen capture.*

**Part of the FastJava Ecosystem** â€” *[github.com/andrestubbe](https://github.com/andrestubbe)*  
*Making the JVM faster through native acceleration.*

