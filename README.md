# morph-editor

Terminal-based image/video annotation editor for LLM agent tool-call scenarios. Pure C, renders images inline via Sixel/Kitty protocol.

## Build

```bash
mkdir build && cd build && cmake .. && make
```

Requires: CMake >= 3.16, C11 compiler, libuv. Optional: FFmpeg (video support).

## Usage

```bash
# Interactive
morph-editor open image.jpg

# Headless
morph-editor info image.jpg
morph-editor annotate image.jpg --bbox 10,20,100,80 --label cat
morph-editor mask image.jpg --bbox 10,20,100,80 --feather 5 --output mask.png
morph-editor crop image.jpg --bbox 10,20,100,80 --output-base64
morph-editor draw image.jpg --rect 10,20,100,80 --color red --output out.jpg
morph-editor transform image.jpg --resize 800x600 --rotate 90 --output out.jpg
morph-editor composite --canvas 800x600 --place fg.png --at 100,50 --output out.jpg
morph-editor diff before.jpg after.jpg

# JSON-RPC server (for LLM agents)
morph-editor --serve
```

## Key Shortcuts (Interactive)

| Key | Action |
|---|---|
| Left drag | Draw new bbox |
| Right drag | Select/move/resize bbox |
| `s` Save | `d` Delete | `u` Undo | `r` Redo |
| `e` Edit label | `Tab` Cycle bbox | `q` Quit |

## Features

- Sixel/Kitty inline image rendering
- Interactive bbox annotation with labeling
- Drawing primitives, transforms, masking, compositing
- Video frame navigation (FFmpeg + LRU cache)
- JSON-RPC server for programmatic control
- Headless mode with base64 I/O
- Multi-level arena memory management

## Dependencies

All vendored except libuv: termbox2, stb_image, stb_image_write, stb_image_resize2, stb_truetype, cJSON.
