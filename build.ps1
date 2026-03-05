# PowerShell Build Script for Unified Directory Synchronizer
# Compiles the project using g++ directly

Write-Host "=== Building Unified Directory Synchronizer ===" -ForegroundColor Green

# Create build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
    Write-Host "Created build directory" -ForegroundColor Yellow
}

# Compiler flags
$CXX_FLAGS = "-std=c++17", "-Wall", "-Wextra", "-O2", "-D_WIN32_WINNT=0x0A00"
$INCLUDE_DIRS = "-I."
$LIBS = @()
if ($IsWindows) {
    $LIBS = @("-lws2_32", "-lshell32")
}

# Source files
$SHARED_SOURCES = @(
    "shared/protocol.cpp",
    "shared/utils.cpp",
    "shared/config.cpp",
    "shared/logger.cpp",
    "shared/file_handler.cpp",
    "shared/browser_launcher.cpp"
)

$CLIENT_SOURCES = @(
    "client/watcher.cpp",
    "client/sender.cpp"
)

$SERVER_SOURCES = @(
    "server/receiver.cpp",
    "server/web_server.cpp"
)

$MAIN_SOURCE = "main.cpp"

# Compile shared library objects
Write-Host "`nCompiling shared sources..." -ForegroundColor Cyan
$SHARED_OBJECTS = @()
foreach ($src in $SHARED_SOURCES) {
    $obj = "build/" + [System.IO.Path]::GetFileNameWithoutExtension($src) + ".o"
    $SHARED_OBJECTS += $obj
    
    Write-Host "  Compiling $src -> $obj" -ForegroundColor Gray
    $result = & g++ $CXX_FLAGS $INCLUDE_DIRS -c $src -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        Write-Host $result
        exit 1
    }
}

# Compile client sources
Write-Host "`nCompiling client sources..." -ForegroundColor Cyan
$CLIENT_OBJECTS = @()
foreach ($src in $CLIENT_SOURCES) {
    $obj = "build/" + [System.IO.Path]::GetFileNameWithoutExtension($src) + ".o"
    $CLIENT_OBJECTS += $obj
    
    Write-Host "  Compiling $src -> $obj" -ForegroundColor Gray
    $result = & g++ $CXX_FLAGS $INCLUDE_DIRS -c $src -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        Write-Host $result
        exit 1
    }
}

# Compile server sources
Write-Host "`nCompiling server sources..." -ForegroundColor Cyan
$SERVER_OBJECTS = @()
foreach ($src in $SERVER_SOURCES) {
    $obj = "build/" + [System.IO.Path]::GetFileNameWithoutExtension($src) + ".o"
    $SERVER_OBJECTS += $obj
    
    Write-Host "  Compiling $src -> $obj" -ForegroundColor Gray
    $result = & g++ $CXX_FLAGS $INCLUDE_DIRS -c $src -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        Write-Host $result
        exit 1
    }
}

# Compile main.cpp
Write-Host "`nCompiling main application..." -ForegroundColor Cyan
$MAIN_OBJECT = "build/main.o"
Write-Host "  Compiling $MAIN_SOURCE -> $MAIN_OBJECT" -ForegroundColor Gray
$result = & g++ $CXX_FLAGS $INCLUDE_DIRS -c $MAIN_SOURCE -o $MAIN_OBJECT 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to compile $MAIN_SOURCE" -ForegroundColor Red
    Write-Host $result
    exit 1
}

# Link unified executable
Write-Host "`nLinking sync_app.exe..." -ForegroundColor Cyan
$ALL_OBJECTS = @($MAIN_OBJECT) + $SHARED_OBJECTS + $CLIENT_OBJECTS + $SERVER_OBJECTS
$result = & g++ @ALL_OBJECTS -o build/sync_app.exe @CXX_FLAGS -lws2_32 -lshell32 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to link sync_app" -ForegroundColor Red
    Write-Host $result
    exit 1
}

Write-Host "`n=== Build Successful! ===" -ForegroundColor Green
Write-Host "Executable created:" -ForegroundColor Yellow
Write-Host "  - build/sync_app.exe" -ForegroundColor White
Write-Host "`nTo run:" -ForegroundColor Yellow
Write-Host "  .\build\sync_app.exe" -ForegroundColor White
Write-Host "`nThe application will:" -ForegroundColor Yellow
Write-Host "  1. Auto-launch your browser to http://localhost:8888" -ForegroundColor White
Write-Host "  2. Let you choose Server or Client mode" -ForegroundColor White
Write-Host "  3. Configure sync folder and connection settings" -ForegroundColor White
Write-Host "  4. Start synchronizing files automatically" -ForegroundColor White

