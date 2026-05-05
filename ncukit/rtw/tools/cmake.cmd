@echo off
setlocal
set "CUBE_CMAKE=C:\Users\seb\.vscode\extensions\stmicroelectronics.stm32cube-ide-build-cmake-1.45.0-win32-x64\resources\cube-cmake\win32\x86_64\cube-cmake.exe"
if not exist "%CUBE_CMAKE%" (
  echo NCU error: cube-cmake.exe not found at %CUBE_CMAKE%
  exit /b 1
)
"%CUBE_CMAKE%" %*
exit /b %ERRORLEVEL%
