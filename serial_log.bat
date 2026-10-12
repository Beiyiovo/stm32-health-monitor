@echo off
chcp 65001 >nul
title 501 Long-Run Serial Logger (24h test)
"C:\Users\86156\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" "%~dp0_serial_log.py"
