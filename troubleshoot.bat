@echo off
chcp 65001 >nul
title 501 Flash Troubleshoot
"C:\Users\86156\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" "%~dp0_diag2.py"
