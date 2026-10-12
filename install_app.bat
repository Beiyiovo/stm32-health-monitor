@echo off
chcp 65001 >nul
title 501 Health App - ADB Install
echo.
echo ============================================================
echo      501 Health Helper - Install App via ADB
echo ============================================================
echo.

set ADB=T:\TOOL\Android\Sdk\platform-tools\adb.exe
set APK=T:\AS\esp32\Android\Apptest\app\build\outputs\apk\debug\app-debug.apk

if not exist "%ADB%" (
    echo   [X] adb.exe not found
    echo       Expected at: %ADB%
    echo       Install platform-tools in Android Studio SDK Manager
    echo.
    pause
    exit /b 1
)

if not exist "%APK%" (
    echo   [X] APK not found
    echo       Expected at: %APK%
    echo       Build it first: Android Studio - Build - Make Project
    echo.
    pause
    exit /b 1
)

echo   [1/4] Checking phone connection...
"%ADB%" start-server >nul 2>&1
"%ADB%" devices
echo.

"%ADB%" devices | findstr /r "device$" >nul
if errorlevel 1 (
    echo   [X] No phone detected. Please check:
    echo       1. USB debugging enabled on phone?
    echo       2. USB cable supports data ^(some cables charge only^)?
    echo       3. Did you tap "Allow USB debugging" on the phone?
    echo       4. Xiaomi/Redmi: also enable "USB debugging ^(Security settings^)"
    echo.
    pause
    exit /b 1
)

echo   [2/4] Phone connected
echo.
echo   [3/4] Removing old version if any...
"%ADB%" uninstall com.example.myapplication 2>nul
echo.
echo   [4/4] Installing new version...
"%ADB%" install -r "%APK%"
echo.

if errorlevel 1 (
    echo   [X] Install failed. Common causes:
    echo       - INSTALL_FAILED_USER_RESTRICTED
    echo         Xiaomi/Redmi: enable "USB debugging ^(Security settings^)"
    echo       - INSTALL_FAILED_UPDATE_INCOMPATIBLE
    echo         Uninstall the old app on the phone first
    echo       - INSTALL_FAILED_INSUFFICIENT_STORAGE
    echo         Phone storage full
) else (
    echo   [OK] Install SUCCESS!
    echo.
    echo   Now open the app on your phone and:
    echo     1. Connect phone to WiFi hotspot "HealthHelper" ^(password 12345678^)
    echo     2. Tap START to begin receiving
    echo     3. Allow notification permission
)

echo.
pause
