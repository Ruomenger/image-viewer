# image-viewer

A cross-platform (macOS / Windows / Linux) open-source image viewer with archive
browsing — view images directly out of `.zip` / `.cbz` / `.7z` / `.rar` / `.tar`,
in natural file order. Inspired by [BandiView](https://en.bandisoft.com/bandiview/).

**Stack:** C++20 · Qt 6 (Widgets + QGraphicsView) · CMake + vcpkg.
Qt is consumed as a **prebuilt** install; vcpkg (manifest mode) handles the rest.

> Status: early skeleton. Currently targets **macOS**; Windows/Linux to follow.

---

## Prerequisites (macOS)

```bash
# 1. Build tools (you likely already have cmake/ninja/clang)
brew install cmake ninja pkg-config

# 2. Qt 6 — prebuilt, fast
brew install qt

# 3. vcpkg — for libarchive (and future libheif/libavif/libraw)
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
cmake --preset default          # configures; vcpkg installs libarchive on first run
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

They cover the testable core (`viewer-core`): extension detection, natural-order
sorting, non-image filtering, `indexOf`, and archive/folder `readEntry` decoding —
using on-the-fly generated PNGs and zips (`test/support/TestData`). Disable with
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
  MainWindow.{h,cpp}       menus, navigation, drag&drop, open-path dispatch
  ImageView.{h,cpp}        QGraphicsView: smooth zoom / pan / fit
  source/                  -> built into the `viewer-core` static library
    ImageSource.h          abstract: count() / entryName() / readEntry()
    FolderSource.{h,cpp}   images in a directory (natural sort)
    ArchiveSource.{h,cpp}  images in an archive via libarchive
test/                      Qt Test unit tests for viewer-core
```

`source/` is compiled into a GUI-free **`viewer-core`** static library that both the
app and the tests link against. `ImageSource` decouples "where the bytes come from"
from "how they're shown", so a folder and an archive look identical to the viewer.

## Roadmap

- [ ] Prefetch + LRU decode cache; sequential fast-path for archive reads (avoid O(n) re-scan)
- [ ] Modern formats: HEIC/AVIF/JXL (libheif/libavif), RAW (libraw) — added via vcpkg
- [ ] Thumbnail strip / filmstrip
- [ ] Fullscreen & slideshow
- [ ] Windows / Linux build + CI matrix
