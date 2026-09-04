@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo [FastScreen] Building library...
call mvn clean install -DskipTests -q
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] FastScreen build failed.
    pause
    exit /b %ERRORLEVEL%
)

powershell -NoProfile -Command "Unblock-File -Path '%USERPROFILE%\.fastcore\native\fastscreen\*', '%~dp0src\main\resources\native\*', '%~dp0release\*' -ErrorAction SilentlyContinue" >nul 2>&1

echo [FastScreen] Compiling Visual Demo...
cd examples\Demo
call mvn compile "-Dmdep.outputFile=cp.txt" dependency:build-classpath -DincludeScope=runtime -q
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Demo compilation failed.
    cd ..\..
    pause
    exit /b %ERRORLEVEL%
)

echo [FastScreen] Starting Visual Showcase Demo...
set /p CP=<cp.txt
java --enable-native-access=ALL-UNNAMED "-Djava.library.path=%~dp0src\main\resources\native;%~dp0release" -cp "target\classes;%CP%" fastscreen.Demo

cd ..\..
pause
