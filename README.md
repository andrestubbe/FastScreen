# FastScreen 0.1.0 [ALPHA-2026-06-14] — High-Performance Native Screen Capture for Java

[![Status](https://img.shields.io/badge/status-0.1.0-brightgreen.svg)](https://github.com/andrestubbe/FastScreen/releases/tag/0.1.0)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Java](https://img.shields.io/badge/Java-17+-blue.svg)](https://www.java.com)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010+-lightgrey.svg)]()
[![JitPack](https://img.shields.io/badge/JitPack-ready-green.svg)](https://jitpack.io/#andrestubbe/FastScreen)

**⚡ Ultra-fast Java screen capture — 500–2000 FPS zero-copy capture via DXGI Desktop Duplication.**

FastScreen is a **high-performance Java screen capture library** and part of the **FastJava ecosystem**. It uses **DXGI
Desktop Duplication API** for **zero-copy, hardware-accelerated screen capture** at 500–2000 FPS. Built for **computer
vision**, **gaming bots**, **screen recording**, and **real-time monitoring** applications.

[![FastScreen Showcase](docs/screenshot.png)](https://www.youtube.com/watch?v=BZsqQl7WqWk)

---

## Table of Contents

- [Key Features](#key-features)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Performance Benchmarks](#performance-benchmarks)
- [API Reference](#api-reference)
- [Documentation](#documentation)
- [Platform Support](#platform-support)
- [License](#license)
- [Related Projects](#related-projects)

---

## Key Features

- 🚀 **500–2000 FPS Capture** — Zero-copy DXGI Desktop Duplication, GPU framebuffer direct access.
- ⚡ **10–17× Faster** than `java.awt.Robot` (8–16 ms vs. 50–100 ms per frame).
- 🖥️ **Hardware Acceleration** — GPU → CPU without intermediate memory copy.
- 🚫 **Zero GC Pressure** — Reusable native buffers, nothing lands on the Java heap.
- 📦 **Multiple Output Formats** — `BufferedImage`, raw RGBA pixels, or streaming callback.
- 🖱️ **Multi-Monitor Support** — Target any display by index.
- 🔗 **Powered by FastCore** — Unified JNI loader for all FastJava modules.
- 📄 **MIT Licensed** — Free for commercial use.

---

## Quick Start

```java
import fastscreen.FastScreen;

public class Demo {
    public static void main(String[] args) {
        // Single screenshot
        BufferedImage img = FastScreen.captureScreen();

        // High-FPS stream
        FastScreen.startStream(0, 0, 1920, 1080);
        while (true) {
            if (FastScreen.hasNewFrame()) {
                byte[] frame = FastScreen.getNextFrame();
                // process frame...
            }
        }
    }
}
```

---

## Installation

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
        <artifactId>FastScreen</artifactId>
        <version>0.1.0</version>
    </dependency>

    <!-- FastCore (Required Native Loader) -->
    <dependency>
        <groupId>com.github.andrestubbe</groupId>
        <artifactId>FastCore</artifactId>
        <version>0.1.0</version>
    </dependency>
</dependencies>
```

### Option 2: Gradle (via JitPack)

```groovy
repositories {
    maven { url 'https://jitpack.io' }
}

dependencies {
    implementation 'com.github.andrestubbe:FastScreen:0.1.0'
    implementation 'com.github.andrestubbe:FastCore:0.1.0'
}
```

### Option 3: Direct Download (No Build Tool)

Download the latest JARs directly to add them to your classpath:

1. 📦 **[fastscreen-0.1.0.jar](https://github.com/andrestubbe/FastScreen/releases/download/0.1.0/fastscreen-0.1.0.jar)** (The Core Library)
2. ⚙️ **[fastcore-0.1.0.jar](https://github.com/andrestubbe/FastCore/releases/download/0.1.0/fastcore-0.1.0.jar)** (The Mandatory Native Loader)

> [!IMPORTANT]
> All JARs must be in your classpath for the native JNI calls to function correctly.

---

## Performance Benchmarks

Run the included benchmark yourself:

```bash
cd examples/03-benchmark
mvn compile exec:java
```

| Metric | FastScreen | `java.awt.Robot` | Improvement |
|----------------|------------|------------------|-------------|
| Single capture | 8–16 ms | 50–100 ms | **10–17× faster** |
| Streaming FPS | 60–240 FPS | ~15 FPS | **Up to 16× faster** |
| GC pressure | None | High | **Zero heap allocation** |

---

## API Reference

### Screen Capture

| Method | Description |
|---|---|
| `captureScreen(Rectangle rect)` | `BufferedImage` screenshot of region |
| `captureRaw(int x, int y, int w, int h)` | Raw RGBA pixel array |
| `getPixelColor(int x, int y)` | Single pixel (very fast) |

### Streaming (High-FPS)

| Method | Description |
|---|---|
| `startStream(int x, int y, int w, int h)` | Begin capture stream |
| `hasNewFrame()` | Check for new frame available |
| `getNextFrame()` | Get next frame (non-blocking) |
| `stopStream()` | Stop and release resources |
| `getStreamFPS()` | Current capture FPS |

### Monitor Selection

| Method | Description |
|---|---|
| `getMonitorCount()` | Number of connected displays |
| `captureMonitor(int index)` | Capture a specific monitor |

---

## Documentation

* **[COMPILE.md](docs/COMPILE.md)**: Full compilation guide (MSVC C++17 build chain + JNI Setup).
* **[REFERENCE.md](docs/REFERENCE.md)**: Full API descriptions and method reference.
* **[PHILOSOPHY.md](docs/PHILOSOPHY.md)**: The engineering rationale for zero-allocation performance.
* **[ROADMAP.md](docs/ROADMAP.md)**: Future milestones and planned features.

---

## Platform Support

| Platform      | Status             |
|---------------|--------------------|
| Windows 10/11 | ✅ Fully Supported  |
| Linux         | 🚧 Planned         |
| macOS         | 🚧 Planned         |

---

## License

MIT License — See [LICENSE](LICENSE) file for details.

---

## Related Projects

- [FastCore](https://github.com/andrestubbe/FastCore) — Native Library Loader for Java
- [FastRobot](https://github.com/andrestubbe/FastRobot) — High-FPS Screen Capture & Native Automation for Java
- [FastImage](https://github.com/andrestubbe/FastImage) — Ultra-Fast Native Image Processing for Java
- [FastOCR](https://github.com/andrestubbe/FastOCR) — Ultra-Fast Native OCR for Java

---
**Part of the FastJava Ecosystem** — *Making the JVM faster. ⚡*
