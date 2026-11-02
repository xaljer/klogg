# AGENTS.md - Klogg 开发指南

## 概述

Klogg 是一个高性能日志查看器，基于 C++20 和 Qt5/Qt6。本文件为 AI 开发助手提供指导。

---

## 功能设计流程

实现任何功能前，必须先提出核心架构设计：

- **数据结构**：哪些状态，存在哪里，key 格式
- **模块边界**：哪个类管什么，依赖关系
- **状态机/生命周期**：有哪些状态，合法迁移是什么
- **平台差异**：抽象到独立文件（如 `shellprocess_win.cpp` / `shellprocess_unix.cpp`），不在代码里散落 `#ifdef`
- **持久化策略**：用哪个 settings 后端（Configuration 还是 SessionInfo），同步行为

架构设计要获得用户批准后再写代码。

## 提交策略

- **严格禁止未经明确授权的提交。只有当用户明确说出"提交"或"commit"这两个字时，才可以使用 `git commit` 命令。**
- **绝对不得根据推断、推测或隐含意图来执行 commit。** 即使用户说"记录下来"、"保存"、"搞定"、"收工"、"完成"或其他任何类似表述，只要没有明确说出"提交"二字，就不得 commit。
- **用户说"提交"时，始终创建新的 commit，不使用 `--amend`。** 后续的修复/调整也要用单独的新 commit，除非用户明确说"amend"或"合并到刚才的提交"。
- 只 stage 目标文件。提交前用 `git status` 和 `git diff --cached` 确认。
- 简洁的 commit message，匹配仓库风格：`type: description`

---

## 构建项目

### 前置要求

- CMake 3.12+, C++17 编译器 (GCC 7.5+, Clang 7+, MSVC 19.14+)
- Qt 5.9+ / Qt 6.x (QtCore, QtGui, QtWidgets, QtConcurrent, QtNetwork, QtXml, QtTools)
- Boost 1.58+, Ragel 6.8+
- 可选: Hyperscan, TBB, uchardet, xxhash

### Qt6 兼容性

编写新代码或修改已有代码时注意 Qt6 兼容：

- **Signals/Slots**：用 `Q_SLOTS` 替代废弃的 `slots` 关键字
- **类型安全**：显式转换（如 `static_cast<int>(qsizetype)`）
- **Strong typedefs**：用字面量操作符（如 `0_lnum`, `1_lcount` 对应 `LineNumber`, `LinesCount` 等）

### 构建命令

项目使用**同一棵构建树** `build/`（Ninja，RelWithDebInfo），脚本都在仓库根（Windows/MSVC）：

```bash
.\build_configure.cmd   # 首次配置（Qt 路径、选项、依赖缓存）
.\build_core.cmd        # 只编 core（引擎 + Core API）+ 零 Qt server 及其测试，不碰 Qt GUI
.\build.cmd             # 编 Qt GUI + 零 Qt server + 全部测试套件
.\test.cmd              # 跑全部测试套件（offscreen）
.\build_core_clang.cmd  # 用 clang-cl 编 core（需装 LLVM；"去 MSVC" 第一半验证）
```

- **编译器缓存**：装了 `sccache`/`ccache` 会自动启用（`KLOGG_USE_COMPILER_CACHE=OFF` 关闭）。
- **PCH**：Qt UI 用预编译头（`KLOGG_USE_PCH=OFF` 关闭对比）。

- `src/core/` 零 Qt（引擎、共享状态、Core API），暴露 `core` target；分层由依赖图表达，增量自动：改 core 只重编 core、GUI 仅重链。
- 产物在 `build/output`。
- **CPM 依赖缓存在 `.cpm_cache/`（已 gitignore，但务必固定保留）**：网络不佳时离线配置/重配依赖它；清理构建目录时不要删它。首次全新拉取依赖后请勿移动该目录。
- 本项目支持 Windows / macOS / Linux 多平台构建。本地可能无法全部验证，但编码时应注意平台兼容性——平台差异抽象到独立文件，避免 `#ifdef` 散落。

### CMake 选项

- `-DKLOGG_USE_HYPERSCAN=OFF` - 用 Qt 正则替代 Hyperscan
- `-DKLOGG_USE_SENTRY=ON` - 启用崩溃报告
- `-DKLOGG_BUILD_TESTS=OFF` - 禁用测试构建
- `-DKLOGG_BUILD_SERVER_ONLY=ON` - 只构建零 Qt 的 `klogg_server`（不查 Qt、不带 GUI/测试）
- `-DKLOGG_USE_PERFETTO=ON` - 在 api/UI 埋 Perfetto 追踪

---

## 测试

### 运行全部测试

```bash
.\test.cmd
```

### 运行单个测试 (Catch2)

```bash
build\output\klogg_api_tests.exe "[tag]"   # 各套件为 build\output\*_tests.exe
```

### 测试位置

- 模块测试（新架构，与代码同处，独立 exe）: `src/<module>/tests/`（如 `src/core/api/tests/` → `klogg_api_tests`）
- 单元测试（旧布局）: `tests/unit/`
- UI 测试: `tests/ui/`
- 测试辅助: `tests/helpers/`
- 基准测试: `tests/bench/`

### UI 截图场景 (tests/ui/screenshot_test.cpp)

- 带 `[screenshot]` tag，仅当 `KLOGG_SCREENSHOT_DIR` 指向输出目录时执行，否则 QSKIP：
  `QT_QPA_PLATFORM=offscreen KLOGG_SCREENSHOT_DIR=<dir> build\output\klogg_itests.exe "[screenshot]"`
- Windows offscreen 平台拿不到系统字体（文本全是方框）；qtests_main 已内置
  `QT_QPA_FONTDIR=C:\Windows\Fonts` 兜底（可用同名环境变量覆盖）

### 性能回归场景 (tests/ui/perf_repro_test.cpp)

- 带 `[perf_repro]` tag，仅当 `KLOGG_PERF_REPRO_FILE` 指向大日志文件时执行，否则 QSKIP：
  `QT_QPA_PLATFORM=offscreen KLOGG_PERFETTO_TRACE=<path>.pftrace KLOGG_PERF_REPRO_FILE=<big.log> build\output\klogg_itests.exe "[perf_repro]"`
- 相位 marker（resize/search/tab）打在 "ui" category 上，配 trace_processor_shell 按窗口切片；
  详见 docs/architecture/perf-stutter-notes.md

---

## 代码风格

### 格式化 (Clang-Format)

```bash
clang-format -i src/path/to/file.cpp
clang-format --dry-run -Werror src/path/to/file.cpp  # 只检查
```

关键规则 (`.clang-format`):
- 基于 LLVM，C++20，缩进 4，列宽 100
- 指针对齐: Left
- 大括号换行: else 前，函数后
- 短函数单行: 仅空函数

### 代码分析 (Clang-Tidy)

```bash
clang-tidy src/path/to/file.cpp
```

---

## 命名规范

| 元素 | 规范 | 示例 |
|------|------|------|
| 类 | CamelCase | `MainWindow` |
| 结构体 | CamelCase | `LinePosition` |
| 函数 | camelBack | `loadFile()` |
| 变量 | camelBack | `loadingFileName` |
| 私有成员 | camelBack + `_` | `isMaximized_` |
| 参数 | camelBack | `fileName` |
| 常量 | CamelCase | `MAX_RECENT_FILES` |
| 命名空间 | lower_case | `logging` |

---

## 注释

只写必要的、简洁的英文注释：
- 解释 **为什么**，不是**什么**（代码应该自解释）
- 去掉仅仅重复描述代码的注释
- 好的示例：`// ns -> us (Chrome JSON format)`
- 坏的示例：`// Extract module name`（函数名已经说明了）
- **禁止 Phase/Step/TODO 等过程描述** — 写的是正式代码，不是演进计划。阶段性标注只允许在 commit message 中
- **禁止中文注释** — 全部英文

### 国际化 (i18n)

添加或修改 UI 元素时必须同时提供中英文翻译：
- 新增字符串到 `src/app/i18n/en.ts` (英文)
- 新增字符串到 `src/app/i18n/zh_CN.ts` (中文)
- 用 `QObject::tr()` 包裹可翻译字符串

---

## Include 顺序

```cpp
#include <QMainWindow>        // Qt
#include <QMenu>

#include <memory>             // C++ 标准库
#include <mutex>

#include "configuration.h"    // 本地
#include "mainwindow.h"
```

---

## 类型与 Qt 使用

- Qt 类用 Qt 命名（`QString`, `QVector`, `QVariant`）
- 智能指针优先 `std::make_unique`, `std::make_shared`
- 容器用 STL（`std::vector`, `std::map`）
- 字符串：UI 用 `QString`，内部数据用 `std::string`

---

## 错误处理

- 尽量少用异常；优先用错误码或 Qt 的错误处理
- 日志：用项目日志（`src/logging/include/log.h`）
- 断言：用 `Q_ASSERT` 做 debug 检查
- 错误恢复：返回合理的默认值或 `std::optional`

---

## Commit Message 格式

- **标题**：`<type>: <description>`，简练概括，一行
- **正文**：空一行后写具体内容，分条列出改动点

```
<type>: <description>

- change 1
- change 2
```

类型: feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert, tr

示例:
```
feat: add new filter option

- Add boolean expression filter in search bar
- Support AND/OR/NOT logic between patterns
```

---

## 项目结构

```
src/
├── core/          - 零 Qt 核心：引擎 + 共享状态 + Core API
│   ├── api/       - Core API 接口层
│   ├── ansi/      - ANSI 转义解析
│   ├── filewatch/ - 文件监控
│   ├── logdata/   - 日志数据结构
│   ├── regex/     - 正则表达式
│   ├── render/    - 渲染核心（折行/高亮合成/布局，零 Qt）
│   └── state/     - 共享状态（高亮/过滤器/搜索历史/最近文件）
├── app/           - 程序入口（Qt）
├── crash_handler/ - 崩溃报告
├── klogg_version/ - 版本信息
├── logging/       - 日志基础设施
├── platform/      - 平台机制（settings/paths/userdata/log，零 Qt）
├── server/        - 零 Qt HTTP/SSE 服务（klogg_server_core + 临时 klogg_server）
├── settings/      - Qt 配置（Configuration/PersistentInfo）
├── ui/            - Qt UI 组件
├── utils/         - 工具函数
├── web/           - Web UI（Vite + Svelte + xterm；spike/ 是 P0 验证）
└── versioncheck/  - 版本检查
```

---

## 第三方补丁

CPM 拉取的第三方库需要打补丁时，把改好的完整源文件放在
`3rdparty/patches/<package>-<version>/` 下，并在 `3rdparty/CMakeLists.txt` 对应包的
`if(<pkg>_ADDED)` 块里用 `configure_file(... COPYONLY)` 在配置期覆盖 `_deps` 中的同名文件。
包版本由 GIT_TAG 固定，补丁文件不会漂移；升级包版本时必须同步审查补丁。

现有补丁：
- `efsw-1.4.1/WatcherWin32.cpp` — (1) `DestroyWatch` 不提前释放 OVERLAPPED/通知缓冲区：
  被取消的 ReadDirectoryChangesW IRP 会异步完成、内核在完成时才写这两个结构，
  提前释放会让内核写已释放堆；改为把 watch 移入 graveyard，由 watcher 线程从 IOCP
  收到最终完成包后再释放（同步等待方案会被 I/O 栈中的过滤驱动卡死，故不采用）。
  **v2（2026-08-29）**：删除 ERROR_NOT_FOUND 立即释放快路径，一律 defer——
  IRP 已完成但完成包未收割时 CancelIoEx 报 NOT_FOUND，立即释放会让陈旧完成包
  被误配给复用了同一地址的新 watch（mWatches.find 命中）→ 多 arm 一个 IRP →
  graveyard 只收一个包即释放 → 第二个 IRP 完成时内核写已释放堆（P0 残留 ~0.3%
  fastfail 的根因）。defer 使地址在收割前不被复用，陈旧包必命中 graveyard。
  (2) `WatchCallback` 修复零字节完成（缓冲区溢出）时重复 arm 两次 ReadDirectoryChangesW
  导致同一 OVERLAPPED 上挂多个 IRP 的问题，每次完成只 re-arm 一次
- `efsw-1.4.1/FileWatcherWin32.cpp` / `FileWatcherWin32.hpp` — graveyard 集合与收割逻辑；
  run 循环同时处理失败完成包（取消的 IRP 以 res==FALSE 返回）

---

## 关键文件

- **CMakeLists.txt** - 根构建配置
- **BUILD.md** - 详细构建说明
- **.clang-format** - 代码格式化规则
- **.clang-tidy** - 静态分析配置
