@echo off
echo ESP32 DMX Node Configurator
echo ============================

python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Python not found. Install Python 3 from https://python.org
    pause
    exit /b 1
)

echo Checking dependencies...
pip install -q -r "%~dp0requirements.txt"

echo Starting configurator...
python "%~dp0esp32_dmx_config.py"
