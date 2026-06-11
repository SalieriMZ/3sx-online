@echo off
REM 3SX Android: install debug APK + push SF33RD.AFS into the app's
REM private storage + launch the activity. Debug builds expose run-as,
REM so we can drop the ROM straight into /data/data/<pkg>/files/.
setlocal

set "APK=%~dp0app\build\outputs\apk\debug\app-debug.apk"
set "AFS=%APPDATA%\CrowdedStreet\3SX\resources\SF33RD.AFS"
set "PKG=cl.chambeadores.threesx"

if not exist "%APK%" (
    echo [ERROR] APK not found at:
    echo   %APK%
    echo Build it first: gradlew assembleDebug
    goto :fail
)

if not exist "%AFS%" (
    echo [ERROR] SF33RD.AFS not found at:
    echo   %AFS%
    echo Launch the Windows 3SX build with your ISO once to produce it.
    goto :fail
)

echo [1/6] Checking adb device...
adb get-state 1>nul 2>nul
if errorlevel 1 (
    echo [ERROR] No device. Enable USB debugging + plug in phone + authorise.
    adb devices
    goto :fail
)
adb devices

echo.
echo [2/6] Installing APK...
adb install -r "%APK%"
if errorlevel 1 goto :fail

echo.
echo [3/6] Pushing SF33RD.AFS to /sdcard/Download/ (this is the slow step)...
adb push "%AFS%" /sdcard/Download/SF33RD.AFS
if errorlevel 1 goto :fail

echo.
echo [4/6] Preparing app private storage...
adb shell "run-as %PKG% mkdir -p files/resources"
adb shell "run-as %PKG% rm -f files/resources/SF33RD.AFS"

echo.
echo [5/6] Copying into app private storage (cat | run-as tee)...
adb shell "cat /sdcard/Download/SF33RD.AFS | run-as %PKG% sh -c 'cat > files/resources/SF33RD.AFS'"

echo Verifying size on device...
adb shell "run-as %PKG% stat -c '%%s' files/resources/SF33RD.AFS"

echo.
echo [5b/6] Staging shaders into app private storage...
adb shell "run-as %PKG% mkdir -p files/shaders/sdlgpu/spirv files/shaders/opengl"
REM Probe in order:
REM   1. shaders/ sibling to this bat (works inside any dist zip)
REM   2. %APPDATA%\CrowdedStreet\3SX\shaders\ (already staged by a desktop install)
REM   3. SHADERS env var, for power users
set "SHADERS_LOCAL=%~dp0shaders"
if not exist "%SHADERS_LOCAL%" set "SHADERS_LOCAL=%APPDATA%\CrowdedStreet\3SX\shaders"
if not exist "%SHADERS_LOCAL%" set "SHADERS_LOCAL=%SHADERS%"
if not exist "%SHADERS_LOCAL%" (
    echo [ERROR] No shader dir found. Put a shaders\ folder next to this .bat,
    echo         or set SHADERS=^<path^> in your environment.
    goto :fail
)
adb push "%SHADERS_LOCAL%/sdlgpu/spirv" /sdcard/Download/3sx-shaders-spirv >nul
for /f %%f in ('adb shell ls /sdcard/Download/3sx-shaders-spirv/') do (
    adb shell "cat /sdcard/Download/3sx-shaders-spirv/%%f | run-as %PKG% sh -c 'cat > files/shaders/sdlgpu/spirv/%%f'"
)

echo.
echo [6/6] Launching 3SX...
adb shell am start -n "%PKG%/.ThreeSXActivity"

echo.
echo Done. Stream logs in another window:
echo   adb logcat -s SDL:* 3SX:* DEBUG:* AndroidRuntime:*
endlocal
exit /b 0

:fail
endlocal
exit /b 1
