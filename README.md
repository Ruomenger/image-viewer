# image-viewer

A cross-platform (macOS / Windows / Linux) open-source image viewer with archive
browsing — view images directly out of `.zip` / `.cbz` / `.7z` / `.rar` / `.tar`,
in natural file order. Inspired by [BandiView](https://en.bandisoft.com/bandiview/).

**Stack:** C++23 · Qt 6.12 (Widgets + QGraphicsView) · CMake + vcpkg.
Qt is consumed as a **prebuilt** install; vcpkg (manifest mode) handles the rest.

> Status: M0–M2 done (browsing core, LRU cache + prefetch, HEIC/AVIF/RAW, GIF/WebP
> animation). M2.5 async pipeline rework done: size-tiered decoding, the `DecodeScheduler`,
> the async `Browser`, and archives dispatched by their real format — zip / plain tar read
> randomly, 7z / rar / compressed tar extracted once in the background into a `ByteStore`.
> Next: M3 (UI). See [doc/TODO.md](doc/TODO.md).
> Currently targets **macOS**; Windows/Linux to follow.

---

## Prerequisites (macOS)

```bash
# 1. Build tools (you likely already have cmake/ninja/clang)
brew install cmake ninja pkg-config

# 2. Qt 6 — prebuilt, fast
brew install qt

# 3. vcpkg — for libarchive / libheif / libavif / libraw
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

## Environment

The CMake preset reads two environment variables:

```bash
export VCPKG_ROOT=~/vcpkg
export QT_ROOT=$(brew --prefix qt)   # e.g. /opt/homebrew/opt/qt
```

(Add them to your shell profile so every session has them.)

## Build & run

```bash
cmake --preset default          # configures; vcpkg installs dependencies on first run
cmake --build --preset default

# run (macOS app bundle)
./build/default/image-viewer.app/Contents/MacOS/image-viewer

# or open something directly
open -a ./build/default/image-viewer.app  ~/Pictures/example.zip
```

Use **release** instead of **default** for an optimized build.

## Tests

Unit tests use **Qt Test** and run via CTest:

```bash
cmake --build --preset default          # tests build alongside the app
ctest --test-dir build/default --output-on-failure
```

They cover the testable core (`viewer-core`): folder/archive sources, the decoder
registry, the tiered cache and decode scheduler, and the `Browser` playlist model — using
on-the-fly generated PNGs and zips (`test/support/TestData`), a blockable fake source that
counts reads (`test/support/FakeSource`), plus real format samples in
`test/data/`. `test_image_view` / `test_main_window` drive the real window on Qt's
offscreen platform (open → navigate → zoom to full resolution). Disable with
`-DBUILD_TESTING=OFF`.

## Usage

- **File ▸ Open Image / Open Folder / Open Archive**, or drag a file/folder onto the window,
  or pass a path on the command line.
- Navigate: `←` / `→` (also `PageUp/Down`, `Space`).
- Zoom: mouse wheel (around cursor), `+` / `-`. Pan: drag. `F` fit, `1` actual size.

---

## Architecture

```
src/
  main.cpp                 entry point
  MainWindow.{h,cpp}       menus, navigation, drag&drop, open-path dispatch (viewer-ui)
  ImageView.{h,cpp}        QGraphicsView in original-pixel coordinates: zoom / pan / fit (viewer-ui)
  source/                  where bytes come from: ImageSource / FolderSource /
                           ZipArchiveSource (stateless random reads) /
                           StreamArchiveSource (one background pass into a ByteStore)
  decode/                  bytes -> QImage: decoder registry (libavif / libheif / LibRaw) + QImageReader
  schedule/                DecodeScheduler: one priority queue for every read + decode
  cache/                   ImageCache: per-tier (Thumb / Fit / Full) LRU
  browse/                  Browser: playlist model (source + current index)
test/                      Qt Test unit tests for viewer-core
```

`source/` `decode/` `cache/` `browse/` compile into a GUI-free **`viewer-core`** static
library that both the app and the tests link against. `ImageSource` decouples "where the
bytes come from" from "how they're shown", so a folder and an archive look identical to
the viewer. Full design — including the M2.5 async pipeline (`DecodeScheduler`,
size-tiered decoding, per-format archive sources) — is in [doc/DESIGN.md](doc/DESIGN.md).

## Roadmap

Milestones (M0–M6) and what's next live in [doc/TODO.md](doc/TODO.md).
