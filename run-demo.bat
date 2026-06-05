@echo off
echo ðŸš€ Running Hero Demo...
cd examples\00-basic-capture
call mvn -q compile exec:java -Dexec.mainClass=fastscreen.examples.BasicCaptureDemo
cd ..\..
pause
