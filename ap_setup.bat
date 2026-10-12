@echo off
chcp 65001 >nul
title 501 ESP32-C3 Setup AP Mode
"C:\Users\86156\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" "%~dp0_ap_setup.py"
