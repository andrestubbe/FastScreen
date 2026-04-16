@echo off
chcp 65001 >nul
echo ===========================================
echo FastScreen Smoke Test - Direct Run
echo ===========================================
echo.

set "FASTSCREEN_ROOT=..\.."
set "JAVA_FILE=SmokeTest.java"

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
    echo [INFO] FastCore not in local Maven repo
echo [INFO] Using placeholder - test will fail at native load
echo   ^(This is expected until native DLL is built^)
    set "FASTCORE_JAR="
) else (
    echo [OK] FastCore found
echo.
)

:: Check for native DLL
echo [CHECK] Looking for native DLL...
if not exist "%FASTSCREEN_ROOT%\native\fastscreen.dll" (
    echo [WARNING] fastscreen.dll not found!
echo [INFO] Native DLL needs to be compiled with Visual Studio
echo   cd %FASTSCREEN_ROOT%\native
echo   compile.bat
) else (
    echo [OK] Native DLL found
)
echo.

:: Compile test
echo [COMPILE] Building smoke test...
if exist "SmokeTest.class" del SmokeTest.class

if defined FASTCORE_JAR (
    javac -cp "%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" %JAVA_FILE%
) else (
    javac -cp "%FASTSCREEN_ROOT%\target\classes" %JAVA_FILE%
)

if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)
echo [OK] Compilation successful
echo.

:: Run test
echo [RUN] Starting smoke test...
echo ===========================================
echo.

if defined FASTCORE_JAR (
    java -cp ".;%FASTSCREEN_ROOT%\target\classes;%FASTCORE_JAR%" -Djava.library.path="%FASTSCREEN_ROOT%\native" SmokeTest
) else (
    java -cp ".;%FASTSCREEN_ROOT%\target\classes" -Djava.library.path="%FASTSCREEN_ROOT%\native" SmokeTest
)

echo.
echo ===========================================
pause
