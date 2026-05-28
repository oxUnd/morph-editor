# morph-editor 需求文档

## 1. 项目概述

基于 termbox2 + stb + libuv 的终端图片/视频标注编辑器，面向 LLM Agent 工具调用场景。
支持 Sixel/Kitty 协议渲染图片，纯 C 实现，Linux 内核编码规范，多级 Arena 内存管理。

## 2. 依赖

| 依赖 | 用途 | 集成方式 |
|------|------|----------|
| termbox2.h | TUI 框架 | vendored |
| stb_image.h | 图片解码（JPG/PNG/TGA/BMP/PSD/GIF/HDR） | vendored |
| stb_image_write.h | 图片编码保存（PNG/TGA/BMP/JPG） | vendored |
| stb_image_resize2.h | 图片缩放 | vendored |
| stb_truetype.h | 文字渲染 | vendored |
| stb_easy_font.h | 备用位图字体 | vendored |
| cJSON.h/cJSON.c | JSON 解析/生成 | vendored |
| libuv | 异步事件循环、进程管理、定时器 | 系统依赖，CMake find_package |
| FFmpeg（可选） | 视频帧提取、ffprobe 信息获取 | 系统依赖，运行时检测 |

## 3. 目录结构

```
morph-editor/
├── CMakeLists.txt
├── .editorconfig
├── .clang-format
├── src/
│   ├── main.c
│   ├── arena.c/h
│   ├── editor.c/h
│   ├── image.c/h
│   ├── render.c/h
│   ├── bbox.c/h
│   ├── mask.c/h
│   ├── draw.c/h
│   ├── canvas.c/h
│   ├── transform.c/h
│   ├── video.c/h
│   ├── protocol.c/h
│   ├── headless.c/h
│   ├── history.c/h
│   ├── terminal.c/h
│   ├── json_util.c/h
│   └── util.c/h
├── vendor/
│   ├── termbox2.h
│   ├── stb_image.h
│   ├── stb_image_write.h
│   ├── stb_image_resize2.h
│   ├── stb_truetype.h
│   ├── stb_easy_font.h
│   ├── cJSON.h
│   └── cJSON.c
└── tests/
    ├── test_arena.c
    ├── test_image.c
    ├── test_bbox.c
    ├── test_mask.c
    ├── test_draw.c
    ├── test_transform.c
    ├── test_canvas.c
    ├── test_protocol.c
    └── test_history.c
```

## 4. 编码规范

严格遵循 Linux 内核编码规范（Documentation/process/coding-style.rst）：

| 规则 | 要求 |
|------|------|
| 缩进 | 8 列 Tab |
| 行宽 | 严格 80 列 |
| 花括号 | 函数体开括号独占一行；控制语句开括号跟同行 |
| 命名 | 全 snake_case，禁止 camelCase |
| struct | 禁止 typedef 隐藏，用 `struct foo` 不用 `foo_t` |
| 指针 | `*` 靠变量名：`char *str`；不加 p/pp 后缀，靠语义区分 |
| 注释 | 仅 `/* */`，禁止 `//`；函数上方加 kernel-doc 风格注释 |
| 变量声明 | 块顶部声明，禁止中途声明（C89 风格） |
| 枚举/宏 | 全大写 `ENUM_VALUE` / `MACRO_NAME`；多语句宏用 `do {} while (0)` |
| 头文件守卫 | `#ifndef MORPH_EDITOR_FOO_H` |
| 返回值 | 错误用负整数，成功用 0 |
| goto | 错误清理用 goto 跳到函数末尾 cleanup 是合法的 |
| switch | case 与 switch 同级缩进 |
| 编译警告 | -Wall -Wextra -Werror -Wstrict-prototypes -Wold-style-definition -Wdeclaration-after-statement |

## 5. 内存管理

### 5.1 Arena 实现

手写实现，约 100 行，链式增长：

```c
struct arena {
	unsigned char *data;
	size_t offset;
	size_t capacity;
	struct arena *next;
};

void *arena_alloc(struct arena *a, size_t size);
void arena_reset(struct arena *a);
void arena_free(struct arena *a);
size_t arena_snapshot(struct arena *a);
void arena_rollback(struct arena *a, size_t snap);
```

### 5.2 多级 Arena

| Arena | 生命周期 | 用途 |
|-------|----------|------|
| session | 程序启动 → 退出 | 编辑器状态、配置、文件信息 |
| image | 图片打开 → 关闭 | 原始像素、缩放像素、渲染缓冲 |
| frame | 帧加载 → LRU 淘汰 | 视频帧像素缓存 |
| command | 操作执行 → undo/redo | draw 临时缓冲、mask 结果、undo snapshot |

### 5.3 规则

- 禁止裸 `malloc/free`，统一 `arena_alloc`
- 例外：arena 自身 data 指针用系统 malloc；超大像素缓冲（>64MB）可单独 malloc
- 需分配内存的函数，第一个参数传 `struct arena *`
- 字符串用 `arena_alloc` + `memcpy`，禁止 `strdup`
- 错误清理优先 `arena_reset`/`arena_rollback`

## 6. 模块设计

### 6.1 main.c — 入口与 CLI

```
morph-editor [command] [options]

命令：
  open <path>                   交互模式打开图片/视频
  info <path>                   输出图片元信息 JSON
  annotate <path> --bbox x,y,w,h [--label text]   非交互标注
  mask <path> --bbox x,y,w,h [--feather N] [--invert]   生成 mask
  crop <path> --bbox x,y,w,h   裁剪区域
  draw <path> --rect/text/arrow/circle ...   绘图原语
  transform <path> --resize/rotate/flip/pad ...   变换
  composite --canvas WxH --place img --at x,y ...   合成
  convert <path> --format png  格式转换
  diff <path1> <path2>         图片差异比较

全局选项：
  --output <path>               输出文件路径
  --output-base64               输出 base64 到 stdout
  --base64                      输入从 stdin 读取 base64
  --no-tui                      纯非交互模式
  --serve / --jsonrpc           JSON-RPC 服务模式
  --log <path>                  日志输出到文件（默认 stderr）
  --verbose                     详细日志
```

### 6.2 editor.c — 编辑器核心

**事件循环（libuv + termbox2）：**

```
libuv event loop
  ├── uv_poll_t (tty fd)    → termbox2 事件
  ├── uv_poll_t (stdin)     → JSON-RPC 消息
  ├── uv_timer_t (video)    → 帧切换定时器
  ├── uv_process_t (ffmpeg) → 帧数据
  └── uv_idle_t             → 渲染刷新
```

**编辑器状态机：**

```
VIEW ──鼠标按下──→ SELECT ──释放──→ CONFIRM ──Enter──→ VIEW
  ↑                                   │
  └──────────── Esc 取消 ─────────────┘
```

**UI 布局：**

```
┌──────────────────────────────────────────┐
│                                          │
│         [图片区域 - Sixel/Kitty]          │
│                                          │
│    ┌──── BBox 1: "cat" ───┐             │
│    │                       │             │
│    └───────────────────────┘             │
│                                          │
├──────────────────────────────────────────┤
│ image.jpg [640x480] | Frame: 30/300     │
│ BBox: 2 | Mode: SELECT | Undo: 3        │
│ [s]ave [d]elete [u]ndo [tab] [q]uit     │
└──────────────────────────────────────────┘
```

**快捷键：**

| 按键 | 功能 |
|------|------|
| s | 保存标注 |
| d | 删除选中 BBox |
| q | 退出 |
| Tab | 切换选中 BBox |
| u | Undo |
| r | Redo |
| +/- | 视频帧步进 |
| ←/→ | 视频逐帧 |
| Space | 视频播放/暂停 |
| [ / ] | 视频跳帧 |

### 6.3 render.c — 图片渲染

**终端检测优先级：**

1. 查询 Kitty 支持
2. 查询 Sixel 支持
3. 检查环境变量 TERM_PROGRAM / TERM
4. 不支持则报错退出（不回退 half-block）

**Sixel 编码流程：**
```
原始像素 → 量化 256 色（中位切分法）→ Sixel 编码 → 写入 tty fd
```

**Kitty 协议流程：**
```
原始像素 → PNG 编码（stb_image_write 到内存）→ base64 →
Kitty escape sequence → 通过 placement id 管理
```

**坐标映射：**
```c
void term_to_pixel(int tx, int ty, int *px, int *py,
		   int img_w, int img_h, int term_w, int term_h,
		   int offset_x, int offset_y, float scale);

void pixel_to_term(int px, int py, int *tx, int *ty, ...);
```

### 6.4 bbox.c — BBox 交互

```c
struct bbox {
	int x, y, w, h;
	char label[64];
	uint32_t color;
	int id;
};

struct bbox_manager {
	struct bbox *boxes;
	int count;
	int selected;
	int next_id;
	int drag_state;
	int drag_handle;
	int start_x, start_y;
};
```

- 鼠标拖拽画框，多 BBox 管理
- 坐标转换：终端字符坐标 ↔ 原图像素坐标
- 叠加渲染：Kitty 用 placement id 删除重绘；Sixel 整帧重绘

### 6.5 mask.c — Mask 生成

```c
struct mask_options {
	int feather;
	int invert;
};

unsigned char *mask_from_bboxes(struct bbox *boxes, int count,
				int img_w, int img_h,
				struct mask_options opts);
unsigned char *mask_from_base64(const char *b64, int *w, int *h);
unsigned char *mask_combine(unsigned char *a, unsigned char *b,
			    int w, int h, int op);
```

### 6.6 draw.c — 绘图原语

```c
enum draw_type {
	DRAW_RECT,
	DRAW_FILLED_RECT,
	DRAW_CIRCLE,
	DRAW_FILLED_CIRCLE,
	DRAW_LINE,
	DRAW_ARROW,
	DRAW_TEXT,
	DRAW_FILL
};

struct draw_command {
	enum draw_type type;
	int x, y, w, h;
	char text[256];
	uint32_t color;
	int thickness;
	int font_size;
};

int draw_execute(unsigned char *pixels, int w, int h,
		 int channels, struct draw_command *cmd);
```

### 6.7 canvas.c — 画布合成

```c
struct canvas_layer {
	char *image_path;
	int x, y;
	float opacity;
	int blend_mode;
};

unsigned char *canvas_composite(int canvas_w, int canvas_h,
				struct canvas_layer *layers,
				int layer_count,
				uint32_t bg_color);
```

### 6.8 transform.c — 图片变换

```c
struct transform_options {
	int resize_w, resize_h;
	int rotate;
	int flip_h, flip_v;
	int pad_w, pad_h;
	uint32_t pad_bg;
	int pad_mode;
};

int transform_apply(unsigned char **pixels, int *w, int *h,
		    int channels, struct transform_options opts);
```

### 6.9 video.c — 视频帧

```c
struct video_state {
	char *path;
	int total_frames;
	double fps;
	int current_frame;
	unsigned char *frame_cache[32];
	int cache_frame_ids[32];
	uv_process_t ffmpeg_proc;
	uv_pipe_t ffmpeg_stdout;
	uv_pipe_t ffmpeg_stdin;
};
```

- FFmpeg pipe 异步帧提取（libuv uv_process_t + uv_pipe_t）
- LRU 32 帧缓存（PNG 压缩格式存储，命中时 stb_image 解码）
- ffprobe 获取帧率、总帧数

### 6.10 protocol.c — JSON-RPC

**方法表：**

| 方法 | 说明 |
|------|------|
| editor/open | 打开图片（path 或 base64） |
| editor/close | 关闭当前图片 |
| editor/info | 获取图片元信息 + 统计（直方图、主色调） |
| editor/getBBoxes | 获取所有 bbox |
| editor/addBBox | 添加 bbox |
| editor/removeBBox | 删除 bbox |
| editor/mask | 从 bbox 生成 mask，返回 base64 |
| editor/crop | 裁剪区域，返回 base64 |
| editor/draw | 绘制原语（rect/text/arrow/circle） |
| editor/transform | resize/rotate/flip/pad |
| editor/composite | 合成多图 |
| editor/export | 导出图片（path 或 base64） |
| editor/undo | 撤销 |
| editor/redo | 重做 |
| editor/getHistory | 查询撤销栈状态 |
| video/open | 打开视频 |
| video/close | 关闭视频 |
| video/setFrame | 跳转帧 |
| video/getInfo | 视频信息（帧率/总帧数） |

**事件通知（编辑器 → Agent）：**

```json
{"jsonrpc":"2.0","method":"editor/onBBoxCreated","params":{"bbox":{...}}}
{"jsonrpc":"2.0","method":"editor/onBBoxModified","params":{"bbox":{...}}}
{"jsonrpc":"2.0","method":"editor/onFrameChanged","params":{"frame":31}}
```

**传输：**
- stdin/stdout 通信，stderr 日志
- 支持 LSP 风格 Content-Length header（大 base64 消息）
- 行缓冲模式（`\n` 分隔 JSON）

### 6.11 headless.c — 纯非交互模式

`--no-tui` 时跳过 termbox2，按 CLI 参数执行操作链：

```
load image → [transform] → [draw] → [add bbox] → [mask] → [crop] → export
```

### 6.12 history.c — 撤销栈

```c
enum op_type {
	OP_ADD_BBOX,
	OP_REMOVE_BBOX,
	OP_MOVE_BBOX,
	OP_RESIZE_BBOX,
	OP_DRAW,
	OP_TRANSFORM,
	OP_COMPOSITE
};

struct history_op {
	enum op_type type;
	cJSON *snapshot;
};

struct history_manager {
	struct history_op *undo_stack;
	struct history_op *redo_stack;
	int max_depth;
};
```

### 6.13 json_util.c — JSON 工具

```c
char *base64_encode(const unsigned char *data, int len);
unsigned char *base64_decode(const char *str, int *out_len);
uint32_t parse_color(const char *str);
cJSON *bbox_to_json(struct bbox *bbox);
struct bbox bbox_from_json(cJSON *json);
```

### 6.14 terminal.c — 终端检测

```c
enum term_proto {
	TERM_PROTO_SIXEL,
	TERM_PROTO_KITTY,
	TERM_PROTO_UNKNOWN
};

enum term_proto terminal_detect(void);
```

## 7. 实施阶段

### Phase 1 — 基础框架 + 图片渲染 + BBox 交互

**目标：** 能在终端中打开图片、显示、圈选 bbox、输出坐标。

- CMake 构建系统、vendor 集成
- arena 多级内存管理实现
- CLI 参数解析框架（open/info 子命令）
- libuv 事件循环 + termbox2 集成
- 终端检测（Sixel/Kitty）
- Sixel 编码器实现
- Kitty 图片协议实现
- 图片加载（stb_image）+ 缩放（stb_image_resize2）+ 渲染管线
- 坐标映射（终端 ↔ 原图像素）
- BBox 鼠标拖拽交互、多框管理
- BBox 叠加渲染
- 编辑器 UI（状态栏、快捷键）
- headless 模式：open/info/annotate 子命令
- JSON 标注输出 + base64 I/O
- 图片保存（stb_image_write）

**验收：**
```bash
# 交互：打开图片，鼠标画框，s 保存，q 退出
morph-editor open test.jpg

# 非交互：标注 bbox 输出 JSON
morph-editor annotate test.jpg --bbox 10,20,100,80 --label cat

# 图片信息
morph-editor info test.jpg
```

### Phase 2 — 绘图/变换/Mask/合成 + 撤销栈 + JSON-RPC

**目标：** 完整的图片编辑能力，JSON-RPC 协议让 LLM Agent 远程操控。

- 绘图原语：rect/circle/line/arrow/text/fill
- 文字渲染（stb_truetype）
- 图片变换：resize/rotate/flip/pad
- Mask 生成：bbox→二值 mask、羽化、多框合并、反转
- 区域裁剪（crop + base64 输出）
- 画布合成：多图层叠加、透明度混合
- 格式转换
- 撤销栈：操作快照、undo/redo
- JSON-RPC 协议：消息解析分发、全部 editor/* 方法实现
- 事件通知
- headless 模式：mask/crop/draw/transform/composite/convert 子命令
- 语义 mask 导入（SAM 输出的 base64 mask）

**验收：**
```bash
# 非交互绘图
morph-editor draw test.jpg --rect 10,20,100,80 --color red --output out.jpg

# 生成 mask
morph-editor mask test.jpg --bbox 10,20,100,80 --feather 5 --output mask.png

# 裁剪 + base64 输出
morph-editor crop test.jpg --bbox 10,20,100,80 --output-base64

# JSON-RPC 服务
echo '{"jsonrpc":"2.0","id":1,"method":"editor/open","params":{"path":"test.jpg"}}' | \
  morph-editor --serve
```

### Phase 3 — 视频帧 + 图片 Diff + 批处理 + EXIF

**目标：** 视频标注能力、图片对比、批量处理、元数据保留。

- FFmpeg 异步帧提取（libuv uv_process_t）
- 帧导航：逐帧、跳帧、播放/暂停
- LRU 帧缓存
- 视频帧上的 BBox 标注
- JSON-RPC video/* 方法
- 图片 Diff：像素差异对比、差异区域 bbox 输出
- 批处理：目录遍历 + 相同操作链
- EXIF 保留/修改
- 完整测试覆盖
- 使用文档

**验收：**
```bash
# 视频帧标注
morph-editor open video.mp4
# → 交互模式下 ←/→ 逐帧浏览，画框标注

# 图片差异
morph-editor diff before.jpg after.jpg
# → {"diff_bboxes":[{"x":50,"y":30,"w":200,"h":150}],"similarity":0.87}

# 批量生成 mask
morph-editor mask ./images/ --bbox 10,20,100,80 --output-dir ./masks/

# JSON-RPC 视频操控
echo '{"jsonrpc":"2.0","id":1,"method":"video/setFrame","params":{"frame":30}}' | \
  morph-editor --serve
```

## 8. 关键技术点

| 问题 | 方案 |
|------|------|
| Sixel 下叠加 BBox | 整帧重绘（编辑操作非高频，性能可接受） |
| Kitty 下叠加 BBox | placement id 删除重绘 |
| JSON-RPC 与 TUI 事件共存 | libuv uv_poll_t 统一监听 tty fd + stdin |
| FFmpeg 非阻塞 | libuv uv_process_t + uv_pipe_t 异步管道 |
| 帧缓存 | LRU 32 帧，PNG 压缩格式缓存，命中时 stb_image 解码 |
| Sixel 颜色量化 | 中位切分法（median cut） |
| 大 base64 传输 | LSP 风格 Content-Length header |
| 文字渲染 | stb_truetype 光栅化到像素缓冲区，作为图片层叠加 |
