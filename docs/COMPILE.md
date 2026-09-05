# Building FastScreen 🛠️

Complete build guide for compiling the native C++17 Direct3D/DXGI engine and packaging the Java JAR.

---

## Prerequisites

*   **Windows 10 or 11 (64-bit)**
*   **JDK 17+** ([Eclipse Adoptium](https://adoptium.net/) or [Oracle JDK](https://www.oracle.com/java/technologies/downloads/))
*   **Visual Studio 2022 or 2026** (Community, Professional, or Enterprise) with "Desktop development with C++" workload
*   **Windows 10/11 SDK** (installed with Visual Studio)
*   **Maven 3.9+**

---

## Automated One-Click Build

FastScreen includes an intelligent compilation script with automatic Visual Studio and JDK discovery:

```cmd
# In the FastScreen repository root:
compile.bat
```

What `compile.bat` does automatically:
1. Queries `vswhere.exe` to locate the latest installed Visual Studio (VS 2026 or VS 2022).
2. Initializes the 64-bit developer environment (`vcvars64.bat`).
3. Auto-detects `JAVA_HOME` if not already set.
4. Compiles `native/fastscreen.cpp` and `native/DXGICapture.cpp` with `/O2` optimization and links against `user32.lib`, `gdi32.lib`, `dxgi.lib`, `d3d11.lib`, and `d3dcompiler.lib`.
5. Deploys `fastscreen.dll` directly to:
   - `native/fastscreen.dll`
   - `src/main/resources/native/fastscreen.dll` (for inclusion inside JARs)
   - `%USERPROFILE%\.fastcore\native\fastscreen\fastscreen.dll` (for runtime lookup by FastCore)

---

## Maven Java Packaging

Once the native DLL is compiled, build and install the module to your local Maven repository:

```bash
# Build and install to ~/.m2/repository
mvn clean install -DskipTests
```

---

## Manual C++ Compilation

If you prefer building manually from a Visual Studio Developer Command Prompt:

```cmd
cd native

cl /LD /EHsc /O2 /W3 /nologo ^
   /I"%JAVA_HOME%\include" ^
   /I"%JAVA_HOME%\include\win32" ^
   fastscreen.cpp ^
   DXGICapture.cpp ^
   /link ^
   user32.lib ^
   gdi32.lib ^
   dxgi.lib ^
   d3d11.lib ^
   d3dcompiler.lib ^
   /OUT:fastscreen.dll ^
   /MACHINE:X64
```

---

## Project Structure

```
FastScreen/
├── src/main/java/              # Java source code
│   └── fastscreen/
│       └── FastScreen.java     # Public Java API & JNI bridges
├── src/main/resources/native/  # Bundled native DLL in JAR
│   └── fastscreen.dll
├── native/                     # C++ DirectX 11 source
│   ├── fastscreen.h           # JNI declarations
│   ├── fastscreen.cpp         # JNI bridge & window exclusion
│   └── DXGICapture.cpp        # DirectX 11 & GDI fallback engine
├── examples/                   # Standalone runnable demos & benchmarks
│   ├── Demo/                   # Visual Showcase Hero Demo (FastTheme, AA, COVER)
│   └── Benchmark/              # JMH Microbenchmark Suite
├── docs/                       # Architectural documentation
├── compile.bat                 # Automated VS compiler script
├── run-demo.bat                # Launch Hero Demo
├── run-benchmark.bat           # Run JMH Benchmark Suite
└── pom.xml                     # Maven build configuration
```

---

## Troubleshooting

### "E_ACCESSDENIED (0x80070005) during DuplicateOutput"
- **Cause**: FastScreen was launched from a non-interactive console, headless background agent, or Remote Desktop session without active DWM desktop composition.
- **Solution**: FastScreen automatically handles this via its built-in Win32 GDI DIBSection hardware fallback, continuing execution seamlessly with zero crashes.

### "Visual Studio C++ compiler not found"
- Run `compile.bat` — it uses `vswhere.exe` to find modern Visual Studio installations automatically.

---

**Part of the FastJava Ecosystem** — *Making the JVM faster. ⚡*

