@echo off
:: Lanza el instalador PowerShell con permisos de administrador
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%~dp0install_windows.ps1\"'"
