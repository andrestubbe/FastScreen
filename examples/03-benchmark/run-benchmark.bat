@echo off
chcp 65001 >nul
echo ===========================================
echo FastScreen Benchmark - vs java.awt.Robot
echo ===========================================
echo.

set "FASTSCREEN_ROOT=..\.."
set "JAVA_FILE=FastScreenBenchmark.java"

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
    echo [INFO] Maven should download it automatically
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

:: Compile benchmark
echo [COMPILE] Building benchmark...
if exist "FastScreenBenchmark.class" del FastScreenBenchmark.class

javac -cp "%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" %JAVA_FILE%

if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)
echo [OK] Compilation successful
echo.

:: Run benchmark
echo [RUN] Starting benchmark...
echo ===========================================
echo.
echo This will compare:
echo   1. java.awt.Robot (100 iterations)
echo   2. FastScreen single capture (100 iterations)
echo   3. FastScreen streaming (5 seconds)
echo.
echo Press any key to start...
pause >nul

echo.
java -cp ".;%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" -Djava.library.path="%FASTSCREEN_ROOT%\native" FastScreenBenchmark

echo.
echo ===========================================
pause
