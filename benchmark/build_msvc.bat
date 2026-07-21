@echo off
REM Build TinyCFG benchmark with Microsoft Visual C++ (Developer Command Prompt)
setlocal

where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found.
    echo Open "x64 Native Tools Command Prompt for VS" then run build_msvc.bat
    exit /b 1
)

echo Building with MSVC...
cl /nologo /EHsc /O2 /std:c++17 /DTINYCFG_HOST_BENCH ^
    /Ihost /I..\src ^
    tinycfg_bench.cpp host\arduino_shim.cpp ^
    ..\src\TinyCFG.cpp ..\src\TinyCFGDependency.cpp ^
    /Fe:tinycfg_bench.exe

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo Running benchmark...
tinycfg_bench.exe dataset.csv 500 results_raw.csv
python analyze_results.py results_raw.csv --latex
endlocal
