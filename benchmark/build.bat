@echo off
REM TinyCFG Host Benchmark — Windows build script (MinGW g++)
setlocal

where g++ >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++ not found.
    echo Install MinGW-w64:  winget install -e --id mingw-w64.mingw-w64
    echo Or use MSYS2:       https://www.msys2.org/
    exit /b 1
)

echo Building tinycfg_bench...
g++ -std=c++17 -O2 -Wall -Wextra -DTINYCFG_HOST_BENCH ^
    -Ihost -I../src ^
    -o tinycfg_bench.exe ^
    tinycfg_bench.cpp host/arduino_shim.cpp ^
    ../src/TinyCFG.cpp ../src/TinyCFGDependency.cpp

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo Build OK. Running benchmark...
tinycfg_bench.exe dataset.csv 500 results_raw.csv

if errorlevel 1 exit /b 1

echo.
echo Generating analysis tables...
python analyze_results.py results_raw.csv

endlocal
