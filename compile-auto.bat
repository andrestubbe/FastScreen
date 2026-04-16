@echo off
chcp 65001 >nul

echo ===========================================
echo FastScreen Native Compilation (Auto-Detect)
echo ===========================================
echo.

:: Try to find Visual Studio
echo [INFO] Searching for Visual Studio...

set VS_PATH=

:: Check for VS 2019 (x86) - most likely on this system
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    echo [OK] Found VS 2019 Community
    goto :found
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    echo [OK] Found VS 2019 Professional
    goto :found
)

:: Check for VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    echo [OK] Found VS 2022 Community
    goto :found
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    echo [OK] Found VS 2022 Professional
    goto :found
)

:found

if not defined VS_PATH (
    echo [ERROR] Visual Studio not found!
    echo Searched in:
    echo   C:\Program Files (x86)\Microsoft Visual Studio\2019\*
    echo   C:\Program Files\Microsoft Visual Studio\2022\*
    echo.
    echo Please run this script from "Developer Command Prompt for VS"
    pause
    exit /b 1
)

echo [INFO] Setting up VS environment...
call "%VS_PATH%" x64

if errorlevel 1 (
    echo [ERROR] Failed to setup VS environment
    pause
    exit /b 1
)

:: Check JAVA_HOME
if not defined JAVA_HOME (
    echo [ERROR] JAVA_HOME not set!
    echo Please set JAVA_HOME to your JDK installation.
    pause
    exit /b 1
)

echo [INFO] JAVA_HOME: %JAVA_HOME%
echo [INFO] Compiling native DLL...
echo.

:: Create native output directory if not exists
if not exist native mkdir native

:: Compile native DLL
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
   /OUT:fastscreen.dll ^
   /MACHINE:X64

if errorlevel 1 (
    echo.
    echo [ERROR] Native compilation failed!
    cd ..
    pause
    exit /b 1
)

cd ..

echo.
echo [SUCCESS] Native DLL compiled successfully!
echo [INFO] Output: native\fastscreen.dll
echo.
pause
