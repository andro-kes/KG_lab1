@echo off
setlocal
cd /d "%~dp0"
set "VULKAN_SDK=%~dp0.tools\VulkanSDK\1.4.321.1"
set "PATH=%~dp0.tools\cmake-4.4.4-windows-x86_64\bin;%~dp0.tools\ninja;%~dp0.tools\llvm-mingw-20260922-ucrt-x86_64\bin;%VULKAN_SDK%\Bin;%PATH%"
if not exist "%~dp0.tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe" goto missing
if not exist "%VULKAN_SDK%\Bin\glslc.exe" goto missing
set "LAB_OFFLINE=OFF"
if /i "%~1"=="--offline" set "LAB_OFFLINE=ON"
cmake --preset local -DFETCHCONTENT_FULLY_DISCONNECTED=%LAB_OFFLINE%
if errorlevel 1 exit /b 1
cmake --build --preset local
if errorlevel 1 exit /b 1
echo Build completed. Run run.cmd to open the application.
exit /b 0
:missing
echo Local tools are missing. See README.md for the required .tools layout.
exit /b 1
