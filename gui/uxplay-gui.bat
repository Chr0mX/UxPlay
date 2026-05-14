@echo off
setlocal
set "UXPLAY_DIR=%~dp0"
set "GST_PLUGIN_PATH=%UXPLAY_DIR%gst-plugins"
set "GST_PLUGIN_SCANNER=%UXPLAY_DIR%gst-plugin-scanner.exe"
set "GST_REGISTRY=%UXPLAY_DIR%gstreamer-registry.bin"
set "GST_PLUGIN_SYSTEM_PATH="
set "PATH=%UXPLAY_DIR%;%PATH%"
start "" "%UXPLAY_DIR%uxplay-gui.exe"
