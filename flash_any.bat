@echo off
chcp 65001 >nul
title 501 Universal Flash Tool
rem ============================================================
rem  自动探测 Python。优先级：
rem    1) 环境变量 PYTHON_EXE（自己指定：set PYTHON_EXE=D:\Python\python.exe）
rem    2) py / python / python3（PATH 里能找到哪个用哪个）
rem  找不到会给出清楚的提示，不会闪退。
rem ============================================================

if defined PYTHON_EXE goto run

set "PY="
for %%C in (py python python3) do call :try %%C

if not defined PY goto nopython
set "PYTHON_EXE=%PY%"

:run
"%PYTHON_EXE%" "%~dp0_flash_any.py"
exit /b %errorlevel%

:try
if defined PY exit /b
where %1 >nul 2>&1
if not errorlevel 1 set "PY=%1"
exit /b

:nopython
echo.
echo   [X] 找不到 Python 解释器（已试过 py / python / python3）
echo.
echo       1. 装 Python 3.8 或以上，安装时勾选 "Add Python to PATH"
echo       2. 已装但不在 PATH：先执行下面这行再运行本文件
echo            set PYTHON_EXE=D:\你的路径\python.exe
echo       3. 依赖：pip install -r requirements.txt
echo.
pause
exit /b 1
