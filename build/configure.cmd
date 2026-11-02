@echo off
REM Configure the workspace. Presets live in CMakePresets.json; every build
REM tree is written under out/ (gitignored), never into build/.
setlocal
set "ROOT=%~dp0.."
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=qt"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cmake --preset %PRESET% -S "%ROOT%"
exit /b %ERRORLEVEL%
