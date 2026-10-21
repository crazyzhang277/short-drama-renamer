@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

where node >nul 2>nul
if errorlevel 1 (
  echo.
  echo   [!] 没有找到 Node.js
  echo       请先安装 Node.js: https://nodejs.org/
  echo       安装完成后重新双击本文件。
  echo.
  pause
  exit /b 1
)

node "%~dp0rename.js" %*

echo.
pause
