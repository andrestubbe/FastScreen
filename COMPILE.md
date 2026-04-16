# Building FastScreen

## Prerequisites

- **JDK 17+** ([Oracle](https://www.oracle.com/java/technologies/downloads/) or [OpenJDK](https://adoptium.net/))
- **Maven 3.9+** ([Download](https://maven.apache.org/download.cgi))
- **Visual Studio 2019+** with C++ workload
- **Windows 10/11 SDK**

## Quick Build

```bash
# Clone repository
git clone https://github.com/andrestubbe/FastScreen.git
cd FastScreen

# Build everything (Java + Native)
mvn clean compile

# Create JAR with native libraries
mvn package
```

## Manual Native Compilation

### Using Visual Studio Developer Command Prompt

```cmd
# Open "Developer Command Prompt for VS 2022"
cd FastScreen\native

# Compile native DLL
cl /LD /EHsc /O2 /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" ^
   fastscreen.cpp DXGICapture.cpp ^
   /link user32.lib gdi32.lib dxgi.lib d3d11.lib /OUT:fastscreen.dll
```

### Using Provided Batch Script

```cmd
# In project root
compile.bat
```

## Project Structure

```
FastScreen/
├── src/main/java/          # Java source
│   └── fastscreen/
│       └── FastScreen.java
├── native/                 # C++ source
│   ├── fastscreen.cpp     # JNI wrapper
│   ├── fastscreen.h       # JNI header
│   └── DXGICapture.cpp    # DirectX capture implementation
├── target/                # Build output
│   ├── classes/           # Compiled Java
│   └── native/            # Compiled DLL
└── pom.xml                # Maven config
```

## Troubleshooting

### "Cannot find JNI headers"
- Ensure `JAVA_HOME` environment variable is set
- JDK (not JRE) is required for JNI development

### "DXGI Desktop Duplication not available"
- Windows 10/11 required
- Desktop Window Manager must be running
- Some remote desktop sessions don't support DXGI

### "DLL not found"
- Ensure `fastscreen.dll` is in `native/` directory
- Or add to `java.library.path`

## IDE Setup

### IntelliJ IDEA
1. Open `pom.xml` as project
2. Maven → Reload Project
3. Build → Build Project

### VS Code
1. Install "Extension Pack for Java"
2. Open folder
3. Run build tasks from terminal

## Debug Build

```bash
# Compile with debug symbols
cl /LDd /EHsc /Zi /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" ^
   fastscreen.cpp DXGICapture.cpp ^
   /link user32.lib gdi32.lib dxgi.lib d3d11.lib /OUT:fastscreen.dll
```

## Release Checklist

- [ ] All tests pass (`mvn test`)
- [ ] Native DLL compiled with optimizations (/O2)
- [ ] JAR includes native libraries
- [ ] Version updated in `pom.xml`
- [ ] README updated with new features
- [ ] CHANGELOG.md updated
- [ ] Git tag created (`git tag v1.0.0`)
