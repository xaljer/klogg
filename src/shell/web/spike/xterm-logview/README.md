# xterm.js 日志视口 spike

P0 可行性验证：`Session::renderViewport()` 的输出（文本 + 颜色 chunks + 折行 parts +
gutter flags）能否用 xterm.js 画出来，以及选中/滚动/横向窗口是否可映射回文件行列。

## 运行

```bash
cd src/web/spike/xterm-logview
npm install
npm start          # http://127.0.0.1:5174/
```

无构建步骤：直接加载 `node_modules` 里的 UMD 包；`server.mjs` 是零依赖静态服务器。

## 要验证的点

1. **颜色保真**：时间戳灰、级别色（INFO/WARN/ERROR）、`timeout` 红底、整行背景。
2. **折行**：长行按 100 列折行；gutter 只在首段显示行号，续段显示 `↳`。
3. **gutter 对齐**：外部 DOM gutter 与 xterm 行高严格对齐（无滚动条、无 padding）。
4. **选中映射**：拖选文本，底部状态栏显示 `L行:列 → L行:列`（文件行列）。
5. **无折行 + 横向窗口**：取消“折行”，拖动水平偏移；模拟后端
   `ViewportOptions.firstColumn/visibleColumns` 的列窗口（xterm 本身没有水平滚动）。
6. **控制字符**：含 `\x07 \x08 \x00 \x1b` 的行必须显示为可见占位符，不能触发终端行为。
7. **CJK**：宽字符行正常显示；观察列窗口/选中映射是否因“UTF-16 列 vs 显示 cell”产生偏移。
8. **性能**：点“性能测试 300 帧”，看每帧解析耗时（不含 GPU 绘制）。
9. **跳转/跟随**：改首行、勾选跟随，确认是整窗重写而不是终端 scrollback。

## 已知的 xterm 约束（spike 要确认影响）

- 它是终端，不是文本控件：**没有水平滚动**。方案是用后端列窗口
  （`wrapColumns=0` + `firstColumn/visibleColumns`）实现横向滚动，前端每次重取一窗。
- **控制字符**会移动光标/响铃/清屏；`renderViewport` 去掉了 ANSI，但其它 C0
  字节还在，前端必须 sanitize。
- 缓冲区是行式的：随机跳转要清屏重写整窗。视口大小（40–100 行）可接受。
- **列语义不一致**：API 的列是 UTF-16 code unit，xterm 用显示 cell（CJK 占 2 格）。
  横向窗口与选中映射在含 CJK 的行上会有偏移，需要宽度换算或接受近似。
- 颜色只有 RGB，没有 alpha：半透明高亮需要先与背景混合。
- 整行背景必须用空格填满到右边界，否则只画到文本末尾。

## 结论（2026-09-27 实测）

结论：**xterm.js 可用，但必须加一层"显示 cell ↔ UTF-16 列"适配层**；不需要自研渲染器。

已验证：

- **颜色/背景/粗体**：从 `chunks` 生成 SGR 后，xterm 缓冲区读回的属性正确
  （时间戳 `0x6C7086`、ERROR `0xF38BA8` + bold、`timeout` 背景 `0x5A1D1D`、
  整行背景 `0x313244` 填满到右边界）。
- **gutter**：外部 DOM gutter 与 `.xterm-screen` 顶部（54px）、行高（19.2px）、
  40 行完全一致；不需要 addon。
- **选中映射**：`getSelectionPosition()` 的 `{x,y}` 经
  `visStart + utf16IndexAtCell(row, x)` 映射回文件行列。ASCII 与含 CJK 的行都正确：
  20 个 cell 选中 `} 服务已启动，等待连` → 19 cells / 11 个 UTF-16 单元，
  状态显示 UTF-16 列 `314 → 325`。
- **无折行横向窗口**：`wrapColumns=0` + `firstColumn=50` 时，每行一个终端行、
  40 行不滚动，首行从第 50 个**显示 cell**开始（模拟后端列窗口）。
- **性能**：40 行 × 300 帧 = 1113 ms，**3.71 ms/帧**（WebGL，解析吞吐；
  后台标签页未计 GPU 绘制）。视口级 60fps 有充足余量。
- **控制字符**：`\x07 \x08 \x00 \x1b` 显示为 `␇ ␈ ␀ ␛`，未触发电终端行为。

踩到并解决的两个坑（必须写进 Web 端实现）：

1. **CJK 宽字符**：API 的列是 UTF-16 code unit，xterm 用显示 cell（CJK=2）。
   不换算时，一条 100 字符含中文的行在 101 列终端里会自动再折行，把顶部行挤出视口。
   解决：前端按显示 cell 折行（spike 的 `splitByCells`），选中时
   cell → UTF-16 反算（`utf16IndexAtCell`）。生产环境用可靠的 wcwidth 实现
   （xterm 内部 provider 或 `wcwidth` 包），不要用 spike 里的近似区间。
2. **末列自动换行**：一行正好等于终端宽度时，xterm 进入 wrap-pending，
   随后的 `\r\n` 会多走一行。解决：行宽 ≤ `cols - 2`；整行背景用空格填充时按
   **cell 数**填到 `cols - 2`。

其他注意点：

- xterm 没有水平滚动：横向滚动 = 后端 `firstColumn/visibleColumns` 列窗口重取。
  `visibleColumns=0` 不是"无限"，后端会按 1 列处理；传 `FileInfo.maxLineLength`
  或一个明确的窗口宽度。
- 固定视口行数，不要逐帧 `resize(rows)`；否则会滚出顶部。
- 半透明颜色需先与背景混合（xterm 只有 RGB）。
- emoji/ZWJ 的宽度与 xterm 的 wcwidth 可能仍不一致，P2 用真实日志抽样验证。
