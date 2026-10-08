@echo off
setlocal
cd /d "%~dp0"
set "VULKAN_SDK=%~dp0.tools\VulkanSDK\1.4.321.1"
rem Relative path also works with older Vulkan loaders and Cyrillic folder names.
set "VK_LAYER_PATH=.tools\VulkanSDK\1.4.321.1\Bin"
set "PATH=%~dp0.tools\llvm-mingw-20260922-ucrt-x86_64\bin;%VULKAN_SDK%\Bin;%PATH%"
if not exist "%~dp0build\lab1.exe" (
    echo Application is not built. Run build.cmd first.
    exit /b 1
)
"%~dp0build\lab1.exe"
exit /b %errorlevel%
