@echo off
REM Assemble the Qt release directory from the built tree. Everything is read
REM from out\qt\output and written to out\release\qt.
setlocal enabledelayedexpansion

set "ROOT=%~dp0..\.."
set "BUILD_OUTPUT=%ROOT%\out\qt\output"
set "RELEASE_DIR=%ROOT%\out\release\qt"
set "QTDIR=C:\Qt\6.7.3\msvc2019_64"

if not exist "%BUILD_OUTPUT%\klogg.exe" (
  echo klogg.exe not found in %BUILD_OUTPUT%
  echo Build it with: build\all.cmd
  exit /b 1
)

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"

echo [1/5] Copying klogg binaries...
xcopy "%BUILD_OUTPUT%\klogg.exe" "%RELEASE_DIR%\" /y >nul || exit /b 1
xcopy "%BUILD_OUTPUT%\klogg_portable.exe" "%RELEASE_DIR%\" /y >nul
xcopy "%BUILD_OUTPUT%\klogg_grep.exe" "%RELEASE_DIR%\" /y >nul
xcopy "%BUILD_OUTPUT%\*.pdb" "%RELEASE_DIR%\" /y >nul 2>nul

echo [2/5] Copying TBB DLLs...
xcopy "%BUILD_OUTPUT%\tbb12.dll" "%RELEASE_DIR%\" /y >nul
xcopy "%BUILD_OUTPUT%\tbbmalloc.dll" "%RELEASE_DIR%\" /y >nul
xcopy "%BUILD_OUTPUT%\tbbmalloc_proxy.dll" "%RELEASE_DIR%\" /y >nul

echo [3/5] Copying documentation...
xcopy "%ROOT%\COPYING" "%RELEASE_DIR%\" /y >nul
xcopy "%ROOT%\NOTICE" "%RELEASE_DIR%\" /y >nul
xcopy "%ROOT%\README.md" "%RELEASE_DIR%\" /y >nul
xcopy "%ROOT%\DOCUMENTATION.md" "%RELEASE_DIR%\" /y >nul

echo [4/5] Deploying Qt dependencies...
pushd "%RELEASE_DIR%"
"%QTDIR%\bin\windeployqt.exe" --release klogg.exe >nul 2>&1
"%QTDIR%\bin\windeployqt.exe" --release klogg_portable.exe >nul 2>&1
"%QTDIR%\bin\windeployqt.exe" --release klogg_grep.exe >nul 2>&1
popd

echo [5/5] Copying VC++ runtime...
if exist "%VCToolsRedistDir%" (
  xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\*.dll" "%RELEASE_DIR%\" /y >nul 2>&1
  echo     VC++ runtime copied.
) else (
  echo     WARNING: VC++ runtime not found automatically.
)

echo.
echo Packaging complete: %RELEASE_DIR%
dir /b "%RELEASE_DIR%"
exit /b 0
