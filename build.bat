@echo off
setlocal
cd /d "%~dp0"

set "MINGW=%LOCALAPPDATA%\short-drama-renamer\mingw64\bin"
if exist "%MINGW%\g++.exe" set "PATH=%MINGW%;%PATH%"
where g++.exe >nul 2>nul
if errorlevel 1 (
  echo [x] g++ was not found. Install with: pwsh -NoProfile -File .\install-toolchain.ps1
  pause
  exit /b 1
)
where windres.exe >nul 2>nul
if errorlevel 1 (
  echo [x] windres was not found. Install with: pwsh -NoProfile -File .\install-toolchain.ps1
  pause
  exit /b 1
)

echo [1/3] Compiling resources ...
windres.exe -I src -O coff -o src\app.res src\app.rc
if errorlevel 1 goto err

echo [2/3] Compiling application ...
g++.exe -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ -pthread -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers src\main.cpp src\renamer_core.cpp src\renamer_ops.cpp src\renamer_media.cpp src\app.res -o renamer.exe -lcomctl32 -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lgdi32 -luser32 -lmfplat -lmfreadwrite -lmfuuid
if errorlevel 1 goto err

echo [3/3] Naming output ...
powershell -NoProfile -Command "Move-Item -Force 'renamer.exe' ([string][char]0x77ED+[char]0x5267+[char]0x66F4+[char]0x540D+[char]0x5DE5+[char]0x5177+'.exe')"
if errorlevel 1 goto err

dir /b *.exe
exit /b 0

:err
echo.
echo [x] Build failed. Review the compiler output above.
pause
exit /b 1
