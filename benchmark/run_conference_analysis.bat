@echo off
REM Full conference benchmark: dataset + bench + tables + graphs
cd /d "%~dp0"
python -m pip install matplotlib -q 2>nul
python run_conference_analysis.py --iterations 200
pause
