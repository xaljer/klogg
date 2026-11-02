# How to Build Klogg

## Overview

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.
Local builds can be faster because code can be optimized for current CPU instead of generic x86-64. Support for SSE4/AVX code paths
will be enabled if available on build machine.

## Getting the Source

This project is [hosted on GitHub](https://github.com/variar/klogg). You can clone this project directly using this command:

```
git clone https://github.com/variar/klogg
```

## Dependencies

To build Klogg:

- cmake 3.12 or later to generate build files
- C++ compiler with decent C++17 support (at least gcc 7.5, clang 7, msvc 19.14)
- Qt libraries 5.9 or later (CI builds use Qt 5.9.5/5.12.5/5.15.2):
  - QtCore
  - QtGui
  - QtWidgets
  - QtConcurrent
  - QtNetwork
  - QtXml
  - QtTools

To build Hyperscan regular expressions backend (default):

- CPU with support for [SSSE3](https://en.wikipedia.org/wiki/SSSE3) instructions (for Hyperscan backend)
- Boost (1.58 or later, header-only part)
- Ragel (6.8 or later; precompiled binary is provided for Windows; has to be installed from package managers on Linux or Homebrew on Mac)

To build installer for Windows:

- nsis to build installer for Windows
- Precompiled OpenSSl library to enable https support on Windows

Building tests:

- QtTest

All other dependencies are provided by [CPM](https://github.com/cpm-cmake/CPM.cmake) during cmake configuration stage (see 3rdparty directory).

CPM will try to find Hyperscan, TBB, uchardet and xxhash installed on build host.
If a library can't be found, the one provided by CPM will be used.

## Building

### Local build scripts (Windows)

The repository ships a small set of wrapper scripts (run them from the repository
root). They all drive `CMakePresets.json`, and every build tree lands under `out/` (Ninja, `RelWithDebInfo`):

| Script | Purpose |
|--------|---------|
| `build\configure.cmd` | Configure the `qt` preset into `out/qt` (Qt path, options, dependency cache). Run once after a clean checkout. |
| `build\core.cmd` | Build only the Qt-free core (engine + Core API), the HTTP server and their tests. |
| `build\core-clang.cmd` | Compile that core with clang-cl instead of MSVC (needs LLVM on `PATH`). |
| `build\all.cmd` | Build the Qt GUI, the HTTP server and all test suites. |
| `tests\run.cmd` | Generate the test fixtures, then run every test suite (offscreen Qt). |
| `package_release.cmd` | Assemble a portable release under `release/`. |

**Compiler cache & PCH.** If `sccache` or `ccache` is on `PATH`, CMake uses it as
the compiler launcher automatically (`-DKLOGG_USE_COMPILER_CACHE=OFF` disables).
The Qt UI library also uses a precompiled header (`-DKLOGG_USE_PCH=OFF` disables).

**clang-cl.** For the Windows build the compiler can be swapped to clang-cl
(MSVC ABI, so the msvc Qt packages still link). `build\core-clang.cmd` configures a
separate `out/clang` tree and builds only the core; it requires LLVM
(`winget install LLVM.LLVM`).

Binaries land in `out/qt/output`.

**HTTP server (zero Qt).** `klogg_server` is the temporary host of
`klogg_server_core`, the HTTP/SSE endpoint over the Core API (the product CLI will
host the same core later). It links no Qt library.

```bash
# target-only build (MSVC: run from a Developer PowerShell / after vcvars64)
cmake --build build --target klogg_server klogg_server_tests

out/server/output/klogg_server.exe --port 8080
out/server/output/klogg_server.exe --port 8080 --token secret --root C:/logs
out/qt/output/klogg_server_tests.exe          # or: ctest -R klogg_server_tests
```

- `--port 0` binds an ephemeral port and prints the actual URL.
- `--listen` defaults to `127.0.0.1`; a non-loopback address requires `--token`.
- `--root` restricts `file.open` to that directory; `--web-root` serves a built UI.
- `--cors` allows any origin (development only); `--max-body <bytes>` caps request bodies.
- The server's third-party code (`cpp-httplib`, `nlohmann/json`, and the
  `libarchive` + `zlib`/`bzip2`/`liblzma` stack) is fetched by CPM and cached in
  `.cpm_cache/`; the repository only carries the pinned URLs and the
  `libarchive` patches under `3rdparty/patches/libarchive-3.7.4/`.
- The protocol contract is `docs/architecture/web-api.md`.

To configure a server-only tree on a machine without Qt (also how Web E2E CI
builds it). `cmake --preset server` is the short form; the explicit equivalent is:

```bash
cmake -S . -B out/server -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKLOGG_BUILD_SERVER_ONLY=ON \
  -DKLOGG_BUILD_TESTS=OFF \
  -DBUILD_TESTING=OFF \
  -DKLOGG_USE_LTO=OFF \
  -DKLOGG_USE_HYPERSCAN=OFF \
  -DKLOGG_USE_VECTORSCAN=OFF \
  -DKLOGG_USE_SENTRY=OFF
cmake --build out/server --target klogg_server
```

`KLOGG_BUILD_SERVER_ONLY` skips the Qt GUI targets and their third-party Qt
packages. `BUILD_TESTING=OFF` also keeps third-party test dependencies out of the
configure. The binary is `out/server/output/klogg_server` (`.exe` on Windows).

**Web UI.** `src/shell/web/` is a Vite + Svelte 5 + TypeScript app (xterm viewport). The
Electron shell and `klogg_server` both load the built UI from `out/web`; nothing reads the
source tree at runtime.

```bash
cd src/shell/web
npm ci
npm run build        # writes out/web (repo root); required before e2e and packaging
npm run check        # svelte-check --fail-on-warnings (CI gate for .svelte/.ts/a11y)
npm test             # unit tests: pure logic and i18n
npm run test:e2e     # Playwright against a real klogg_server
npm run dev          # Vite on 127.0.0.1:5173, proxies /api to 127.0.0.1:8080
```

Serve the built UI and the API from one process:

```bash
out/server/output/klogg_server.exe --port 8080 --web-root out/web
# open http://127.0.0.1:8080/
```

`npm run test:e2e` starts the server itself (`cmake --preset server` builds it) and
reads:

| Variable | Default | Meaning |
|---|---|---|
| `KLOGG_SERVER_BIN` | `out/server/output/klogg_server[.exe]` | Server binary under test |
| `KLOGG_WEB_ROOT` | `out/web` | Built UI served to the browser |
| `KLOGG_E2E_PORT` | `18080` | Server port |
| `PLAYWRIGHT_CHANNEL` | unset | `msedge` uses the installed Edge instead of a downloaded Chromium |

Rules:

- `npm run check` must report zero errors and zero warnings. It catches missing imports,
  wrong event payloads and a11y warnings that the bundler tolerates.
- `out/web` is the only artifact the server and Electron read. Rebuild it after any source
  change before e2e or packaging.
- Unit tests cover pure logic; layout, focus, selection and clipboard behaviour belongs to
  e2e. E2E runs against the real server with `src/shell/web/e2e/fixtures` as its file root.

The package layout is described in `src/shell/web/README.md`; the HTTP/SSE contract is
`docs/architecture/web-api.md`; desktop parity is `docs/architecture/web-ui-parity.md`.

**Core / GUI split.** `src/core/` is Qt-free (engine, shared state, the Core API)
and exposes the `core` target. The dependency graph already encodes the layering,
so incremental builds are automatic:

- Building `core` after a core change recompiles only the affected core sources
  and relinks the core static libraries; the Qt GUI is not touched.
- Building `klogg` afterwards only relinks; no UI translation unit is recompiled.

**Dependency cache.** All third-party sources are cached under `.cpm_cache/`
(gitignored). Keep this directory: with poor or no network access, configuration
and rebuilds rely on it. Do not delete or move it after the first fetch.

### Configuration options

By default Klogg is built without support for reporting crash dumps. This can be enabled via cmake option `-DKLOGG_USE_SENTRY=ON`.

Klogg uses Hyperscan regular expressions library which requires CPU with SSSE3 support, ragel and boost headers.
Klogg can be built with only Qt reqular expressions backend by passing `-DKLOGG_USE_HYPERSCAN=OFF` to cmake.

Klogg can use custom memory allocator. By default it uses TBB memory allocator for Windows, mimalloc on Linux and default system allocator on MacOS.
Memory allocator override can be turned off by passing `-DKLOGG_OVERRIDE_MALLOC`. If you want to use TBB allocator on Linux then pass
`-DKLOGG_USE_MIMALLOC=OFF`.

### Building on Linux

Here is how to build klogg on Ubuntu 18.04.

Install dependencies:

```
sudo apt-get install build-essential cmake qtbase5-dev libboost-all-dev ragel
```

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
```

**_If cmake gives error about missing "Qt5LinguistTools" configuration files, try running:_**

```bash
sudo apt-get install qttools5-dev
```

Binaries are placed into `build_root/output`.

See `.github/workflows/ci-build.yml` for more information on build process.

### Building on Windows

Install Microsoft Visual Studio 2017 or 2019 with C++ support.
Community edition can be downloaded from [Microsoft](https://visualstudio.microsoft.com/vs/).

Intall latest Qt version using [online installer](https://www.qt.io/download-qt-installer).
Make sure to select version matching Visual Studio installation. 64-bit libraries are recommended.

Install CMake from [Kitware](https://cmake.org/download/).
Use version 3.14 or later for Visual Studio 2019 support.

Download the Boost source code from http://www.boost.org/users/download/.
Extract to some folder. Directory structure should be something like `C:\Boost\boost_1_63_0`.
Then add `BOOST_ROOT` environment variable pointing to main directory of Boost sources so CMake is able to fine it.

Prepare build environment for CMake. Open command prompt window and depending on version of Visual Studio run either

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\vsdevcmd" -arch=x64
```

or

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\Common7\Tools\vsdevcmd" -arch=x64
```

Next setup Qt paths:

```
<path_to_qt_installation>\bin\qtenv2.bat
```

Then add CMake to PATH:

```
set PATH=<path_to_cmake_bin>:$PATH
```

Configure klogg solution (use CMake generator matching Visual Studio version):

```
cd <path_to_project_root>
md build_root
cd build_root
cmake -G "Visual Studio 16 2019 Win64" -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

CMake should generate `klogg.sln` file in `<path_to_project_root>\build_root` directory. Open solution and build it.

Binaries are placed into `build_root/output`.

For https network urls support download precompiled openssl library https://mirror.firedaemon.com/OpenSSL/openssl-1.1.1l-dev.zip.
Put libcrypto-1_1 and libssl-1_1 for desired architecture near klogg binaries.

### Building on Mac OS

Klogg requires macOS High Sierra (10.13) or higher.

Install [Homebrew](https://brew.sh/) using terminal:

```
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Homebrew installer should also install xcode command line tools.

Download and install build dependencies:

```
brew install cmake ninja qt boost ragel
```

Usually path to qt installation looks like `/usr/local/Cellar/qt/5.14.0/lib/cmake/Qt5`

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQt5_DIR=<path_to_qt_install> ..
cmake --build .
```

Binaries are placed into `build_root/output`.

By default, klogg will rely on cmake to figure out target MacOS version. Usually it uses build host version.
To override default cmake value pass an option `-DKLOGG_OSX_DEPLOYMENT_TARGET=<target>` to cmake during configuration step,
`<target>` is one of `10.14`, `10.15`, `11`, `12`. Klogg's traget must be greater or equal to target used by Qt libraries.

## Electron Windows package

The Electron shell lives in `src/shell/electron`. It consumes the Web build and the
Qt-free server binary; it does not build Qt.

```powershell
cd src/shell/web
npm ci
npm run build

cd ../electron
npm ci
npm test              # main-process unit tests
npm run dist          # NSIS installer
npm run dist:zip      # portable zip
```

`npm run dist` prepares `out/web` and `out/server/output/klogg_server.exe` as application
resources, then writes `out/release/electron/klogg-<version>-setup.exe`. `npm run dist:zip`
adds `klogg-<version>-win.zip`. `npm run pack` stops after
`out/release/electron/win-unpacked`, which is the fastest loop for manual testing: replace
`resources/web` there and restart.

See `src/shell/electron/README.md` for server mode and token details, and
`docs/architecture/electron-packaging.md` for the packaging design.

## Running tests

Tests are built by default. To turn them off pass `-DKLOGG_BUILD_TESTS:BOOL=OFF` to cmake.
Tests use catch2 (bundled with klogg sources) and require Qt5Test module. On Windows the
wrapper script `tests\run.cmd` runs every suite; tests can also be run using the ctest tool
provided by CMake:

```
cd <path_to_klogg_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```
