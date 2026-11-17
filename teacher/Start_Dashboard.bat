@echo off
rem ELT-295: one-click teacher dashboard. Double-click to start; the dashboard opens in your browser.
rem Close this window to stop the server.
rem
rem Needs nothing installed. The first run downloads a portable Python (with Flask) and Cloudflare's
rem cloudflared into teacher\runtime\ (about 30 MB, internet needed once); later runs reuse them.
rem Students connect through a Cloudflare https://....trycloudflare.com address, so no port on this
rem PC has to be reachable from theirs. Run with --no-tunnel to use this PC's IP address instead.
setlocal EnableDelayedExpansion
title ELT-295 Teacher Dashboard
cd /d "%~dp0"

set "RT=%~dp0runtime"
set "PYVER=3.12.10"
set "PYDIR=%RT%\python"
set "PY="

rem 1. Prefer a working installed Python that already has Flask.
py -3 -c "import flask" >nul 2>&1 && set "PY=py -3"

rem 2. Otherwise use (or set up) the portable Python in runtime\python.
if not defined PY (
  if not exist "%PYDIR%\python.exe" (
    echo Setting up a portable Python %PYVER% in %PYDIR% ^(first run only^)...
    if not exist "%RT%" mkdir "%RT%"
    curl.exe -L --fail -o "%RT%\python.zip" "https://www.python.org/ftp/python/%PYVER%/python-%PYVER%-embed-amd64.zip" || goto :dlfail
    mkdir "%PYDIR%" 2>nul
    tar.exe -xf "%RT%\python.zip" -C "%PYDIR%" || goto :dlfail
    del "%RT%\python.zip"
    rem Let the embedded Python load installed packages (site-packages).
    > "%PYDIR%\python312._pth" (
      echo python312.zip
      echo .
      echo import site
    )
  )
  "%PYDIR%\python.exe" -c "import flask" >nul 2>&1 || (
    echo Installing Flask into the portable Python...
    if not exist "%PYDIR%\Scripts\pip.exe" (
      curl.exe -L --fail -o "%RT%\get-pip.py" "https://bootstrap.pypa.io/get-pip.py" || goto :dlfail
      "%PYDIR%\python.exe" "%RT%\get-pip.py" --no-warn-script-location || goto :dlfail
      del "%RT%\get-pip.py"
    )
    "%PYDIR%\python.exe" -m pip install --no-warn-script-location -r server\requirements.txt || goto :dlfail
  )
  set "PY="%PYDIR%\python.exe""
)

rem 3. cloudflared for the tunnel (skipped with --no-tunnel).
set "TUNNEL=--tunnel"
echo %* | find /i "--no-tunnel" >nul && set "TUNNEL="
set "ARGS=%*"
if defined ARGS set "ARGS=!ARGS:--no-tunnel=!"
if defined TUNNEL (
  where cloudflared >nul 2>&1 || if not exist "%RT%\cloudflared.exe" (
    echo Downloading cloudflared ^(Cloudflare tunnel^) ^(first run only^)...
    if not exist "%RT%" mkdir "%RT%"
    curl.exe -L --fail -o "%RT%\cloudflared.exe" "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe" || (
      del "%RT%\cloudflared.exe" 2>nul
      echo Could not download cloudflared. Starting without the tunnel ^(students use this PC's IP^).
      set "TUNNEL="
    )
  )
)

%PY% server\server.py %TUNNEL% !ARGS!
if errorlevel 1 (
  echo.
  echo The dashboard stopped with an error ^(see above^).
  echo If the portable Python is broken, delete the "%RT%" folder and run this again.
  pause
)
exit /b

:dlfail
echo.
echo Setup failed: could not download or install the portable Python / Flask.
echo Check the internet connection, delete the "%RT%" folder, and run this again.
pause
exit /b 1
