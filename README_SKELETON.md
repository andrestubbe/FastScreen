# FastScreen — High-performance screen capture for Java

> **500-2000 FPS zero-copy capture** — DXGI Desktop Duplication for Java
>
> Hardware-accelerated screen capture with zero GC pressure
>
> 🚧 **ALPHA — Daily Active Development** 🚧

[![Java](https://img.shields.io/badge/Java-17+-blue.svg)](https://www.java.com)
[![Maven](https://img.shields.io/badge/Maven-3.9+-orange.svg)](https://maven.apache.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![JitPack](https://img.shields.io/badge/JitPack-ready-green.svg)](https://jitpack.io)

---

## 🚀 Quick Start

```java
import fastscreen.FastScreen;
import java.awt.Rectangle;
import java.awt.image.BufferedImage;

FastScreen screen = new FastScreen();

// Single screenshot — 10-17× faster than Robot
BufferedImage img = screen.captureScreen(new Rectangle(0, 0, 1920, 1080));

// High-FPS streaming mode
screen.startStream(0, 0, 1920, 1080);
while (running) {
    if (screen.hasNewFrame()) {
        int[] pixels = screen.getNextFrame(); // RGBA array
        double fps = screen.getStreamFPS();
    }
}
screen.stopStream();
```

---

## 📦 Installation

### Maven (JitPack)

```xml
<repositories>
    <repository>
        <id>jitpack.io</id>
        <url>https://jitpack.io</url>
    </repository>
</repositories>

<dependency>
    <groupId>com.github.andrestubbe</groupId>
    <artifactId>fastscreen</artifactId>
    <version>v1.0.0</version>
</dependency>
```

### Gradle

```groovy
repositories { maven { url 'https://jitpack.io' } }
dependencies { implementation 'com.github.andrestubbe:fastscreen:v1.0.0' }
```

### Direct Download

Download JAR from [Releases](https://github.com/andrestubbe/FastScreen/releases)

**Required:** FastCore is automatically included via Maven. For direct download, get both:
- `fastscreen-1.0.0.jar` — Main library
- `fastcore-1.0.0.jar` — [JNI loader](https://github.com/andrestubbe/FastCore/releases)

---

## Project Structure

```
fastscreen/
├── .github/workflows/          # CI/CD
├── examples/                     # Runnable demos
│   ├── 00-basic-capture/         # Simple screenshot
│   ├── 01-smoke-test/            # Functionality test
│   ├── 02-streaming-viewer/      # High-FPS demo
│   └── 03-benchmark/             # Performance test
├── native/
│   ├── fastscreen.cpp            # JNI wrapper
│   ├── DXGICapture.cpp           # DirectX capture engine
│   └── fastscreen.h              # Header file
├── src/main/java/fastscreen/     # Java API
│   └── FastScreen.java           # Core capture class
├── compile.bat                   # Native build script
├── COMPILE.md                    # Build instructions
├── pom.xml                       # Maven config
└── README.md                     # This file
```

**Why `examples/` on root level?**
- Not part of the library → separate mini-projects
- Not tests → tutorials for users
- Each example has its own `pom.xml` → runnable standalone
- Copy-paste friendly → users can use as starter template

---

## ⚡ Performance Benchmarks

| Capture Mode | java.awt.Robot | FastScreen | Speedup |
|--------------|----------------|------------|---------|
| **Single 1080p** | 50-100ms | 8-16ms | **6-10×** |
| **Streaming** | ~10 FPS | 60-240 FPS | **6-24×** |
| **Memory** | JVM heap | Off-heap | **Zero GC** |

*Hardware: Windows 11, RTX 3070, Java 17. Measured with included benchmark.*

---

## Building

See [COMPILE.md](COMPILE.md) for detailed build instructions.

### Quick Build

```bash
# Build native DLL
compile.bat

# Build JAR with DLL
mvn clean package -DskipTests
```

### Run Examples

```bash
cd examples/00-basic-capture
mvn compile exec:java
```

---

## 🧠 Why FastScreen?

**java.awt.Robot Problems:**
- ❌ Software-based capture → slow bitblt
- ❌ 50-100ms latency → unusable for real-time
- ❌ JVM heap storage → GC pauses

**FastScreen Solutions:**
- ✅ **DXGI Desktop Duplication** — hardware-accelerated GPU capture
- ✅ **8-16ms latency** — direct framebuffer access
- ✅ **Zero-copy streaming** — native buffers, no GC
- ✅ **60-240 FPS** — real-time capture for CV/bots

---

## 🗺 Part of FastJava Ecosystem

| Module | Purpose | Link |
|--------|---------|------|
| **FastCore** | JNI loader | ⚠️ Alpha |
| **FastRobot** | Input automation | ⚠️ Alpha |
| **FastScreen** | Screen capture | ⚠️ Alpha |
| **FastImage** | Image processing | ⚠️ Alpha |
| **FastGraphics** | GPU rendering | ⚠️ Alpha |

---

## 📚 Examples

Every feature has a standalone example in `examples/`:

```bash
cd examples/00-basic-capture
mvn compile exec:java    # Simple screenshot
```

| Example | Demonstrates |
|---------|-------------|
| `00-basic-capture` | Single screenshot API |
| `01-smoke-test` | Basic functionality test |
| `02-streaming-viewer` | High-FPS streaming |
| `03-benchmark` | Performance comparison |

---

## 🔧 API Reference

### Screen Capture
- `captureScreen(Rectangle rect)` — BufferedImage screenshot
- `captureRaw(int x, int y, int w, int h)` — Raw RGBA pixel array
- `getPixelColor(int x, int y)` — Single pixel (fastest)

### Streaming (High-FPS)
- `startStream(int x, int y, int w, int h)` — Begin capture stream
- `hasNewFrame()` — Check for new frame available
- `getNextFrame()` — Get next frame (non-blocking)
- `getNextFrameDirect()` — Zero-copy ByteBuffer
- `stopStream()` — Stop and cleanup
- `getStreamFPS()` — Current capture FPS

### Monitor Selection
- `getMonitorCount()` — Number of displays
- `captureMonitor(int index)` — Capture specific monitor

---

## License

MIT License — See [LICENSE](LICENSE) for details.

---

**Part of the FastJava Ecosystem** — *Making the JVM faster.*
