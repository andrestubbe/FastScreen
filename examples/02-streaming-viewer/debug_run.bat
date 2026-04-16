@echo off
cd /d "c:\Users\andre\Documents\FastJava\2026-04-16-Work-FastScreen-v1.0\examples\02-streaming-viewer"
set "FASTCORE=%USERPROFILE%\.m2\repository\com\github\andrestubbe\fastcore\1.0.0\fastcore-1.0.0.jar"
echo [DEBUG] Starting StreamingViewer...
echo [DEBUG] Classpath: .;..\..\target\classes;%FASTCORE%
echo [DEBUG] Library path: ..\..\native
java -cp ".;..\..\target\classes;%FASTCORE%" -Djava.library.path="..\..\native" StreamingViewer
echo.
echo [DEBUG] Exit code: %errorlevel%
pause
