# TinyMakerWiFi PlatformIO wrapper - isolated toolchain, safe in the shared env.
#
# ~/.platformio is shared by every ESP32 project on this machine, and the Arduino
# core (framework-arduinoespressif32) lives in ONE unversioned directory, so
# projects on different core versions (RLCD 3.x, eInkWeather 2.0.17, this 2.0.14)
# overwrite each other's framework - a build fails for everyone (see the global
# CLAUDE.md section "PlatformIO: bendra aplinka"). This project keeps its OWN
# core_dir, so it never touches the shared packages.
#
# Usage (Windows PowerShell / cmd, never Git Bash):
#   powershell -ExecutionPolicy Bypass -File scripts\dev\pio.ps1 run -e tinymaker
#   powershell -ExecutionPolicy Bypass -File scripts\dev\pio.ps1 run -t upload -e tinymaker
#
# core_dir is an env var, not a platformio.ini key: an absolute Windows path in
# platformio.ini would break CI (that path is not on the runner) and other
# machines. The first build with a fresh core_dir downloads ~1.5 GB (~20 min);
# after that the shared build.lock is not needed (isolated packages cannot clash).
$env:PLATFORMIO_CORE_DIR = "C:/PIO-core/TinyMakerWiFi"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" @args
exit $LASTEXITCODE
