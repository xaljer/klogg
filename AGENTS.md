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

```bash
# 始终使用项目构建脚本
& .\build_release.cmd
```

本项目支持 Windows / macOS / Linux 多平台构建。本地可能无法全部验证，但编码时应注意平台兼容性——平台差异抽象到独立文件，避免 `#ifdef` 散落。

### CMake 选项

- `-DKLOGG_USE_HYPERSCAN=OFF` - 用 Qt 正则替代 Hyperscan
- `-DKLOGG_USE_SENTRY=ON` - 启用崩溃报告
- `-DBUILD_TESTS=OFF` - 禁用测试构建

---

## 测试

### 运行全部测试

```bash
cd build_root
ctest --build-config RelWithDebInfo --verbose
```

### 运行单个测试 (Catch2)

```bash
./tests/unit/klogg_test "[test_name]"
```

### 测试位置

- 单元测试: `tests/unit/`
- UI 测试: `tests/ui/`
- 测试辅助: `tests/helpers/`

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
├── app/           - 程序入口
├── crash_handler/ - 崩溃报告
├── filewatch/     - 文件监控
├── klogg_version/ - 版本信息
├── logdata/       - 日志数据结构
├── logging/       - 日志基础设施
├── regex/         - 正则表达式
├── settings/      - 配置
├── ui/            - Qt UI 组件
└── utils/         - 工具函数
```

---

## 关键文件

- **CMakeLists.txt** - 根构建配置
- **BUILD.md** - 详细构建说明
- **.clang-format** - 代码格式化规则
- **.clang-tidy** - 静态分析配置
