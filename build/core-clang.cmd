@echo off
REM Verify the Qt-free core compiles and its tests build with clang-cl (MSVC
REM ABI, so the msvc Qt packages still link; only the front-end changes).
REM Requires LLVM/clang-cl on PATH (`winget install LLVM.LLVM`).
setlocal
set "ROOT=%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
where clang-cl >nul 2>&1 || (echo clang-cl not found on PATH - install LLVM ^(winget install LLVM.LLVM^) & exit /b 1)
cmake --preset clang -S "%ROOT%" || exit /b 1
cmake --build "%ROOT%\out\clang" --target ^
  core platform_tests klogg_utils_tests render_tests core_link_probe api_link_probe || exit /b 1
echo Core built with clang-cl.
exit /b 0
