@echo off
setlocal
cd /d "%~dp0"

set "MINGW=%LOCALAPPDATA%\short-drama-renamer\mingw64\bin"
if exist "%MINGW%\g++.exe" set "PATH=%MINGW%;%PATH%"
where g++.exe >nul 2>nul
if errorlevel 1 (
  echo [x] 找不到 C++ 编译器 g++，请先执行： pwsh -NoProfile -File .\install-toolchain.ps1
  pause
  exit /b 1
)

echo [1/4] 编译资源 ...
windres.exe -I src -O coff -o src\app.res src\app.rc
if errorlevel 1 goto err

echo [2/4] 编译 C++ ...
g++.exe -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers src\main.cpp src\app.res -o renamer.exe -lcomctl32 -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lgdi32 -luser32 -lmfplat -lmfreadwrite
if errorlevel 1 goto err

echo [3/4] 设置文件名 ...
powershell -NoProfile -Command "Move-Item -Force 'renamer.exe' ([string][char]0x77ED+[char]0x5267+[char]0x66F4+[char]0x540D+[char]0x5DE5+[char]0x5177+'.exe')"
if errorlevel 1 goto err

echo [4/4] 完成
dir /b *.exe
echo.
echo 双击 短剧更名工具.exe 即可运行
exit /b 0

:err
echo.
echo [x] 编译失败，请把上面的错误信息发给开发者
pause
exit /b 1
