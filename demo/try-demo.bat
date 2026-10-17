@echo off
rem Runs Draz with the sample accounts, products and orders in the demo-data folder.
cd /d "%~dp0"
draz.exe --data demo-data
pause
