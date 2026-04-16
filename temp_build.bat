@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Users\andre\Documents\FastJava\2026-04-16-Work-FastScreen-v1.0\native
cl.exe /EHsc /O2 /MD /Fe:fastscreen.dll /LD /I"C:\Program Files\Java\jdk-25\include" /I"C:\Program Files\Java\jdk-25\include\win32" DXGICapture.cpp fastscreen.cpp /link d3d11.lib dxgi.lib d3dcompiler.lib /MACHINE:X64 2>&1
pause
