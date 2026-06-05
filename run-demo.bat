@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo [FastScreen] Running Demo (via JitPack)...
cd examples\00-basic-capture
call mvn compile exec:java -Dexec.mainClass=fastscreen.examples.BasicCaptureDemo
cd ..\..
pause
