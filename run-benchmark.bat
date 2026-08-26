@echo off
setlocal
chcp 65001 > nul
cd /d "%~dp0"
set "MAVEN_OPTS=--enable-native-access=ALL-UNNAMED --sun-misc-unsafe-memory-access=allow -Dorg.slf4j.simpleLogger.defaultLogLevel=warn"

echo ===================================================
echo  Building FastScreen ^& JMH Benchmarks Uber-Jar
echo ===================================================

call mvn -q clean install -DskipTests 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] FastScreen install failed!
    pause
    exit /b %ERRORLEVEL%
)

cd examples\Benchmark
call mvn -q clean package 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Benchmark packaging failed!
    pause
    exit /b %ERRORLEVEL%
)

echo ===================================================
echo  Running JMH Benchmarks (Throughput: ops/ms)
echo ===================================================
java --enable-native-access=ALL-UNNAMED --sun-misc-unsafe-memory-access=allow "-Djava.library.path=..\..\native;native;target\classes" -jar target\benchmarks.jar -f 1 -wi 2 -i 3 -tu ms -bm thrpt
pause
