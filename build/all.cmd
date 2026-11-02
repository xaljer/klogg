@echo off
REM Build everything in the default out/qt tree: Qt shell, server host and all
REM test suites.
setlocal
set "ROOT=%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cmake --build "%ROOT%\out\qt" --target ^
  platform_tests klogg_utils_tests render_tests core_link_probe api_link_probe ^
  klogg_api klogg_api_tests klogg_ansi_tests klogg_regex_tests klogg_logdata_tests klogg_state_tests klogg_ui_tests klogg_itests klogg klogg_grep ^
  klogg_server klogg_server_tests || exit /b 1
