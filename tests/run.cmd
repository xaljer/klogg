@echo off
REM Run every test suite against the default out/qt tree, offscreen.
REM Fixtures are generated first: the repository stores the generator, not the
REM data (see tests/tools/gen_test_data.py).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\out\qt\output"
set "QT_DIR=C:\Qt\6.7.3\msvc2019_64"
set "PATH=%OUT%;%QT_DIR%\bin;%PATH%"
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=C:\Windows\Fonts"
set "KLOGG_TEST_DATA=%ROOT%\out\testdata"

python "%ROOT%\tests\tools\gen_test_data.py" --out "%KLOGG_TEST_DATA%" || exit /b 1

"%OUT%\platform_tests.exe" || exit /b 1
"%OUT%\klogg_utils_tests.exe" || exit /b 1
"%OUT%\render_tests.exe" || exit /b 1
"%OUT%\klogg_ansi_tests.exe" || exit /b 1
"%OUT%\klogg_regex_tests.exe" || exit /b 1
"%OUT%\klogg_logdata_tests.exe" || exit /b 1
REM Event visualization is not shipped yet; its suite compiles, but it is not run
REM (same reason its ctest entry is DISABLED in src/core/state/tests).
REM "%OUT%\klogg_state_tests.exe" || exit /b 1
"%OUT%\klogg_ui_tests.exe" || exit /b 1
"%OUT%\klogg_api_tests.exe" || exit /b 1
"%OUT%\klogg_itests.exe" || exit /b 1
"%OUT%\klogg_server_tests.exe" || exit /b 1

echo All test suites passed.
exit /b 0
