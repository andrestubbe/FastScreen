@echo off
chcp 65001 >nul
echo ===========================================
echo FastScreen Build Script
echo ===========================================
echo.

:: Check for Maven
where mvn >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Maven not found!
    echo Please install Maven 3.9+ and add to PATH
    pause
    exit /b 1
)

echo [INFO] Building FastScreen...
echo.

:: Clean and compile
mvn clean compile

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build complete!
echo [INFO] Classes: target\classes\
echo.

:: Package if requested
if "%1"=="package" (
    echo [INFO] Creating JAR package...
    mvn package
    if errorlevel 1 (
        echo [ERROR] Packaging failed!
        pause
        exit /b 1
    )
    echo [SUCCESS] Package created: target\*.jar
)

pause
