@echo off
chcp 65001 >nul
echo ===========================================
echo FastScreen Streaming Viewer
echo ===========================================
echo.

set "FASTSCREEN_ROOT=..\.."
set "JAVA_FILE=StreamingViewer.java"

:: Check prerequisites
echo [CHECK] Looking for FastScreen Java classes...
if not exist "%FASTSCREEN_ROOT%\target\classes\fastscreen\FastScreen.class" (
    echo [ERROR] FastScreen classes not found!
    echo [INFO] Please build parent project first:
    echo   cd %FASTSCREEN_ROOT%
    echo   mvn clean compile
    pause
    exit /b 1
)
echo [OK] FastScreen classes found
echo.

:: Check for FastCore JAR
echo [CHECK] Looking for FastCore JAR...
set "FASTCORE_JAR=%USERPROFILE%\.m2\repository\com\github\andrestubbe\fastcore\1.0.0\fastcore-1.0.0.jar"
if not exist "%FASTCORE_JAR%" (
    echo [ERROR] FastCore not found!
    pause
    exit /b 1
)
echo [OK] FastCore found
echo.

:: Check for native DLL
echo [CHECK] Looking for native DLL...
if not exist "%FASTSCREEN_ROOT%\native\fastscreen.dll" (
    echo [ERROR] fastscreen.dll not found!
    echo [INFO] Run compile-auto.ps1 to build the DLL
    pause
    exit /b 1
)
echo [OK] Native DLL found
echo.

:: Compile
echo [COMPILE] Building viewer...
if exist "StreamingViewer.class" del StreamingViewer.class

javac -cp "%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" %JAVA_FILE%

if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)
echo [OK] Compilation successful
echo.

:: Run
echo [RUN] Starting streaming viewer...
echo ===========================================
echo.
echo Controls:
echo   START - Begin live stream
echo   STOP  - Stop streaming
echo   EXIT  - Close application
echo.

java -cp ".;%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" -Djava.library.path="%FASTSCREEN_ROOT%\native" StreamingViewer

echo.
echo ===========================================
pause
