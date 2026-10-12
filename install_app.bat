@echo off
chcp 65001 >nul
title 501 Health App - ADB Install
rem ============================================================
rem  把健康监测 App 装到手机上（通过 adb）
rem
rem  用法：
rem      install_app.bat  [APK路径]
rem    或先设置环境变量再运行：
rem      set APK_PATH=D:\xxx\app-debug.apk
rem
rem  adb 会自动探测：ANDROID_HOME / ANDROID_SDK_ROOT / 常见安装目录
rem  找不到时可以用 set ADB=... 自己指定
rem ============================================================

echo.
echo ============================================================
echo      501 Health Helper - Install App via ADB
echo ============================================================
echo.

rem ---------- 找 adb ----------
if not defined ADB call :find_adb
if not defined ADB (
    echo   [X] 找不到 adb.exe
    echo.
    echo       1. 装 Android SDK Platform-Tools（Android Studio 的 SDK Manager 里勾）
    echo       2. 装完还找不到，就自己指定：
    echo            set ADB=D:\Android\Sdk\platform-tools\adb.exe
    echo.
    pause
    exit /b 1
)

rem ---------- 找 APK ----------
if not "%~1"=="" set "APK_PATH=%~1"
if not defined APK_PATH (
    echo   [X] 没指定 APK 路径
    echo.
    echo       用法： install_app.bat  D:\路径\app-debug.apk
    echo       或者： set APK_PATH=D:\路径\app-debug.apk   然后再运行本文件
    echo.
    echo       APK 不在本仓库里，需要你用 Android Studio 先构建：
    echo         Build - Build Bundle(s) / APK(s) - Build APK(s)
    echo.
    pause
    exit /b 1
)
if not exist "%APK_PATH%" (
    echo   [X] APK 不存在: %APK_PATH%
    echo       先用 Android Studio 构建：Build - Build APK(s)
    echo.
    pause
    exit /b 1
)

echo   adb : %ADB%
echo   apk : %APK_PATH%
echo.

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
"%ADB%" install -r "%APK_PATH%"
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
exit /b 0

rem ============================================================
:find_adb
if defined ANDROID_HOME if exist "%ANDROID_HOME%\platform-tools\adb.exe" set "ADB=%ANDROID_HOME%\platform-tools\adb.exe" & exit /b
if defined ANDROID_SDK_ROOT if exist "%ANDROID_SDK_ROOT%\platform-tools\adb.exe" set "ADB=%ANDROID_SDK_ROOT%\platform-tools\adb.exe" & exit /b
if exist "%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" set "ADB=%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" & exit /b
if exist "T:\TOOL\Android\Sdk\platform-tools\adb.exe" set "ADB=T:\TOOL\Android\Sdk\platform-tools\adb.exe" & exit /b
rem 最后兜底：PATH 里有没有
for %%A in (adb.exe) do if not "%%~$PATH:A"=="" set "ADB=%%~$PATH:A"
exit /b
