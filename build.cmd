@echo off
setlocal

echo ========================================
echo  Building efly
echo ========================================

:: ---- preflight ----
where cmake  >nul 2>&1 || (echo ERROR: cmake not found & exit /b 1)
where git    >nul 2>&1 || (echo ERROR: git not found    & exit /b 1)
where node   >nul 2>&1 || (echo ERROR: node not found   & exit /b 1)
where cargo  >nul 2>&1 || (echo ERROR: cargo not found  & exit /b 1)

:: ---- 1. efly_core.dll ----
echo [1/3] Building efly_core.dll...
cd /d "%~dp0src-lib"
if not exist build\CMakeCache.txt (
    cmake -S . -B build
)
cmake --build build --config Release
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

:: ---- 2. auto-update.dll ----
echo [2/3] Building auto-update.dll...
cd /d "%~dp0"
if not exist library mkdir library
cd library
if not exist auto-update (
    echo Cloning auto-update...
    git clone https://github.com/szWrn/auto-update.git auto-update
)
cd auto-update
powershell -ExecutionPolicy Bypass -File build.ps1
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

:: ---- 3. Tauri ----
echo [3/3] Building Tauri app...
cd /d "%~dp0"
npm run tauri build

echo ========================================
echo  Done.
echo ========================================
