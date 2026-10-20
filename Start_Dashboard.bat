@echo off
rem ELT-295: one-click teacher dashboard. Double-click to start; the dashboard opens in your browser.
rem Close this window to stop the server.
title ELT-295 Teacher Dashboard
cd /d "%~dp0"
py -3 server\server.py %*
if errorlevel 1 (
  echo.
  echo Could not start. Is Python installed, and Flask?  Try:  py -3 -m pip install -r server\requirements.txt
  pause
)
