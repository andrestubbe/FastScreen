# FastScreen Native Compilation (Auto-Detect)
# PowerShell version - more robust with special characters in paths

Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "  FastScreen Native Compilation" -ForegroundColor Cyan
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host ""

# Possible VS paths to check (VS can be in x86 folder even on 64-bit systems)
$vsPaths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
)

$vsPath = $null
foreach ($path in $vsPaths) {
    if (Test-Path $path) {
        $vsPath = $path
        Write-Host "[OK] Found Visual Studio: $path" -ForegroundColor Green
        break
    }
}

if (-not $vsPath) {
    Write-Host "[ERROR] Visual Studio not found!" -ForegroundColor Red
    Write-Host "Searched for vcvarsall.bat in standard locations"
    Write-Host "Please install Visual Studio 2019 or 2022 with C++ workload"
    Pause
    exit 1
}

# Setup VS environment
Write-Host "[INFO] Setting up Visual Studio environment..." -ForegroundColor Yellow

# Create a temporary batch file to capture the environment
$envBatch = @"
@echo off
call "$vsPath" x64 >nul 2>&1
set
"@

$envBatchPath = [System.IO.Path]::GetTempFileName() + ".bat"
Set-Content -Path $envBatchPath -Value $envBatch

# Run the batch and capture environment variables
$envOutput = cmd /c "`"$envBatchPath`""
Remove-Item $envBatchPath

# Parse and set environment variables
foreach ($line in $envOutput) {
    if ($line -match '^([^=]+)=(.*)$') {
        $name = $matches[1]
        $value = $matches[2]
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

# Verify cl.exe is available
$clPath = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
if (-not $clPath) {
    Write-Host "[ERROR] C++ compiler (cl.exe) not found after setup" -ForegroundColor Red
    Pause
    exit 1
}

Write-Host "[OK] Visual Studio environment ready" -ForegroundColor Green
Write-Host "[INFO] Compiler: $clPath" -ForegroundColor Gray
Write-Host ""

# Check JAVA_HOME - auto-detect if not set
if (-not $env:JAVA_HOME) {
    Write-Host "[INFO] JAVA_HOME not set, trying to auto-detect..." -ForegroundColor Yellow
    
    # Common Java locations
    $javaPaths = @(
        "C:\Program Files\Java\jdk-25"
        "C:\Program Files\Java\jdk-21"
        "C:\Program Files\Java\jdk-17"
        "C:\Program Files\Java\jdk-11"
        "C:\Program Files\Java\jdk*"
        "C:\Program Files\Eclipse Adoptium\jdk*"
    )
    
    foreach ($path in $javaPaths) {
        $found = Get-ChildItem -Path $path -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            $javaPath = $found.FullName
            # Remove \bin suffix if present (jni.h is in parent/include)
            if ($javaPath.EndsWith('\bin')) {
                $javaPath = Split-Path -Parent $javaPath
            }
            $env:JAVA_HOME = $javaPath
            Write-Host "[OK] Auto-detected Java: $($env:JAVA_HOME)" -ForegroundColor Green
            break
        }
    }
}

if (-not $env:JAVA_HOME) {
    Write-Host "[ERROR] JAVA_HOME not set and could not auto-detect!" -ForegroundColor Red
    Write-Host "Please set JAVA_HOME to your JDK installation."
    Pause
    exit 1
}

Write-Host "[INFO] JAVA_HOME: $($env:JAVA_HOME)" -ForegroundColor Gray
Write-Host "[INFO] Compiling native DLL..." -ForegroundColor Yellow
Write-Host ""

# Change to native directory
$nativeDir = Join-Path $PSScriptRoot "native"
if (-not (Test-Path $nativeDir)) {
    New-Item -ItemType Directory -Path $nativeDir | Out-Null
}

Push-Location $nativeDir

try {
    # Compile
    $compileArgs = @(
        '/LD', '/EHsc', '/O2', '/W3', '/nologo'
        '/I', "`"$($env:JAVA_HOME)\include`""
        '/I', "`"$($env:JAVA_HOME)\include\win32`""
        'fastscreen.cpp'
        'DXGICapture.cpp'
        '/link'
        'user32.lib', 'gdi32.lib', 'dxgi.lib', 'd3d11.lib'
        '/OUT:fastscreen.dll'
        '/MACHINE:X64'
    )
    
    & cl.exe @compileArgs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "[ERROR] Native compilation failed!" -ForegroundColor Red
        Pause
        exit 1
    }
    
    Write-Host ""
    Write-Host "[SUCCESS] Native DLL compiled successfully!" -ForegroundColor Green
    Write-Host "[INFO] Output: native\fastscreen.dll" -ForegroundColor Gray
    
} finally {
    Pop-Location
}

Write-Host ""
Pause
