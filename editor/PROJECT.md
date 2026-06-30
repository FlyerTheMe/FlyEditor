# mini-editor — 终端里的 VSCode 风格编辑器

用 C + ncursesw 写的命令行文本编辑器，界面类似 VS Code，支持鼠标操作、语法高亮、多 Tab、文件侧栏和集成终端。

## 功能一览

- **多 Tab 编辑** — 同时打开多个文件，点击标签切换
- **文件侧栏** — 目录文件列表，点击展开/折叠，单击打开文件
- **语法高亮** — 支持 10 种语言：C/C++、Go、Rust、Python、JavaScript、Shell、Makefile、JSON、HTML、Markdown
- **集成终端** — `Ctrl+T` 切换出内嵌终端，使用本地 shell
- **鼠标支持** — 点击定位光标、切换 Tab、打开文件；滚轮滚动
- **括号匹配** — `() [] {}` 匹配高亮
- **256 色 + 透明背景** — 随终端主题自适应
- **Gap Buffer** — 文本存储，O(1) 摊还插入/删除
- **UTF-8** — 中文等宽字符支持

## 键盘快捷键

| 按键 | 功能 |
|------|------|
| `Ctrl+Q` | 退出 |
| `Ctrl+S` | 保存当前文件 |
| `Ctrl+W` | 关闭当前 Tab |
| `Ctrl+N` | 新建空白 Tab |
| `Ctrl+O` | 打开文件（输入路径） |
| `Ctrl+T` | 切换集成终端 |
| 方向键 / Home / End | 光标移动 |
| Page Up / Page Down | 翻页 |
| Backspace / Delete | 删除 |
| 鼠标左键 | 点击定位、切换 Tab、打开文件 |
| 鼠标滚轮 | 滚动编辑器 / 侧栏 |

## 构建

```bash
# 依赖
# Ubuntu/Debian: sudo apt install libncursesw5-dev
# Arch: ncurses 已内置 wide-char 支持

make          # 编译
./editor      # 启动（空白文件）
./editor foo.c  # 打开指定文件
```

Makefile 会自动处理编译，链接 `-lncursesw -ltinfo -lutil`。

## 架构

```
src/
├── main.c          # 入口 + poll 主循环
├── editor.h        # 公共类型、结构体、函数声明
├── gap_buffer.c    # Gap Buffer 文本存储
├── file.c          # 文件读写、目录递归扫描
├── input.c         # 键盘/鼠标事件分发
├── terminal.c      # PTY 派生（forkpty）、读写、resize
├── ui.c            # 窗口布局与全部渲染
└── highlight.c     # 10 语言语法高亮引擎
```

### 数据结构

- `GapBuffer` — 带 gap 的字符数组，光标处插入/删除 O(1)
- `Tab` — 一个打开的文件：GapBuffer + 路径 + 语法语言 + 光标/滚动状态
- `SidebarEntry` — 侧栏一个文件/目录条目
- `Editor` — 全局状态：Tabs、侧栏、终端、ncurses 窗口句柄

### 主循环

```
poll(stdin, pty_fd) → wgetch() → input_handle() → ui_render()
```

每次循环：
1. 读取 PTY 输出
2. 渲染界面（5 个子窗口 wnoutrefresh + doupdate）
3. poll 等待输入（终端可见时 50ms 超时用于刷新）
4. 读取键盘/鼠标事件
5. 分发到输入处理函数

### 渲染

5 个 ncurses 子窗口：

```
┌─Tab Bar────────────────────────────────┐
│   foo.c  ×│  bar.py  ×│  Untitled  ×   │
├──────┬──────────────────────────────────┤
│      │  Line 1                          │
│ Side │  Line 2                          │
│ bar  │  Line 3 (← cursor here)          │
│  24  │                                  │
│ cols │  [terminal panel, Ctrl+T]        │
├──────┴──────────────────────────────────┤
│  foo.c ● C  │ NORMAL │ Ln 3, Col 10    │
└─────────────────────────────────────────┘
```

- `draw_tabs` — 文件标签 + 关闭按钮 ×
- `draw_sidebar` — 目录树 + 展开/折叠三角
- `draw_sep` — 分隔线
- `draw_editor` — 行号 + 语法高亮文本 + 光标
- `draw_term` — PTY 输出（ANSI 剥离）
- `draw_status` — 文件名、语言、模式、行列号

### 鼠标事件

ncurses `mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION)` 开启全鼠标事件。根据 `(y, x)` 坐标分发到不同区域：Tab 栏、侧栏、编辑器、终端。滚轮事件检查 `BUTTON4/BUTTON5 (PRESSED | CLICKED)`。

### 终端集成

`forkpty()` 派生 shell 子进程。`poll()` 在 stdin 和 PTY fd 上多路复用。读入的 PTY 输出写入 128KB 环形缓冲区，渲染时剥离 ANSI 转义序列后显示。

## 待改进

- 全角字符列宽跟踪（当前 cx 按字节计数，中文显示后光标偏移不准）
- 跨 Tab 复制粘贴
- 搜索/查找
- 行号区域点击选择行
- 水平滚动条
- 更多语言语法高亮
