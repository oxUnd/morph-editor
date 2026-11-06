# morph-editor

Terminal-based image/video annotation editor for LLM agent tool-call
scenarios. Pure C, renders images inline via Sixel/Kitty/iTerm2
protocols.

## Build

```bash
mkdir build && cd build && cmake .. && make
cd build && ctest --output-on-failure
```

Requires: CMake >= 3.16, a C11 compiler, pthreads.
Optional: FFmpeg/ffprobe for video support.

No libuv is required. Video frames are decoded on a background
worker thread and cached with an LRU policy.

Sanitizer build:

```bash
cmake -S . -B build-asan -DMORPH_SANITIZE=ON
cmake --build build-asan -j
cd build-asan && ctest --output-on-failure
```

## Usage

```bash
# Interactive
morph-editor open image.jpg --annotations image.json
morph-editor open image.jpg --proto kitty
morph-editor open a.jpg b.jpg

# Headless
morph-editor info image.jpg
morph-editor annotate image.jpg --bbox 10,20,100,80 --label cat
morph-editor mask image.jpg --bbox 10,20,100,80 --feather 5 --output mask.png
morph-editor crop image.jpg --bbox 10,20,100,80 --output crop.png
morph-editor draw image.jpg --rect 10,20,100,80 --output out.jpg
morph-editor transform image.jpg --resize 800x600 --rotate 90 --output out.jpg
morph-editor composite --canvas 800x600 --place fg.png --at 100,50 --output out.jpg
morph-editor diff before.jpg after.jpg --output diff.png

# Common options
--output <path>        Output file
--output-base64        Base64 to stdout
--annotations <path>   Load/save annotation JSON (atomic write, autosave)
--proto <name>         auto|kitty|sixel|iterm2
--log <path>           Log to file
--verbose
```

`crop`, `annotate` and `mask` validate and clamp `--bbox` against the
image bounds; invalid rectangles are rejected before any pixel access.

## Key Shortcuts

| Key | Action |
|---|---|
| Left drag | Draw new bbox |
| Right drag | Select / move / resize bbox |
| `a` | Add centered bbox to the active image |
| `e` | Edit label (cursor-aware, UTF-8) |
| `d` | Delete selected bbox/arrow |
| `c` | Duplicate selected object |
| `x` | Draw arrow (click start, then end) |
| `j` | Re-edit selected arrow endpoints |
| `Tab` | Cycle bboxes, then arrows |
| Arrow keys | Nudge 1px; Shift = 10px; Alt = resize |
| `s` | Save annotations (or print JSON to stderr) |
| `u` / `r` | Undo / redo |
| `?` | Help overlay |
| Mouse wheel | Scroll the sidebar |
| `q` | Quit |

Video: `Left`/`Right` step one frame, `[`/`]` jump 30 frames.

## Features

- Sixel / Kitty / iTerm2 inline image rendering
- Bbox and arrow annotations with labels and video keyframes
- Unified selection model (bbox vs. arrow cannot both be active)
- Transactional undo: one gesture = one history entry
- Atomic annotation persistence with autosave (`--annotations`)
- Asynchronous video frame loading with LRU cache
- Safe headless crop/mask/annotate, composite and diff regions
- `--proto` override plus detection for Kitty, iTerm2, WezTerm,
  Ghostty, Konsole, foot and sixel terminals

## Limitations

- `--serve` (JSON-RPC server) is not implemented. Use the CLI and
  JSON output instead; it is documented here as unsupported rather
  than advertised as a feature.
- No zoom/pan, multi-select, or numeric coordinate entry.
- Vendored `stb_image_resize2.h` triggers UBSan reports; the
  sanitizer CI job uses `UBSAN_OPTIONS=halt_on_error=0` for that
  vendor code. ASan findings are still treated as failures.
