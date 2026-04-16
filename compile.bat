@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

echo ===========================================
echo FastScreen Native Compilation
echo ===========================================
echo.

:: Check for Visual Studio
where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Visual Studio C++ compiler not found!
    echo Please run this script from "Developer Command Prompt for VS 2022"
    echo.
    pause
    exit /b 1
)

:: Check JAVA_HOME
if not defined JAVA_HOME (
    echo [ERROR] JAVA_HOME not set!
    echo Please set JAVA_HOME to your JDK installation.
    echo.
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

:: Check if Maven is available
where mvn >nul 2>&1
if errorlevel 0 (
    echo [INFO] Building Java project with Maven...
    mvn clean compile
    if errorlevel 1 (
        echo [ERROR] Maven build failed!
        pause
        exit /b 1
    )
    echo.
    echo [SUCCESS] Full build complete!
) else (
    echo [INFO] Maven not found. Java compilation skipped.
    echo [INFO] Run 'mvn clean compile' manually when ready.
)

echo.
pause
