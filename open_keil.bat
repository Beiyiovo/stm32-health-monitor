@echo off
setlocal
set MDK=T:\TE\UV4\UV4.exe
set PROJ=%~dp009_main\step9_main.uvprojx
if not exist "%MDK%" (
    echo [X] Keil MDK-ARM not found: %MDK%
    pause
    exit /b 1
)
if not exist "%PROJ%" (
    echo [X] Project not found: %PROJ%
    pause
    exit /b 1
)
echo Opening Keil MDK-ARM 5.42 with step9_main.uvprojx ...
start "" "%MDK%" "%PROJ%"
exit /b 0
