# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Cross-platform (macOS → Windows/Linux) open-source image viewer with archive browsing,
inspired by BandiView: view many image formats and page through images **inside** archives
(`.zip` / `.cbz` / `.7z` / `.rar` / `.tar`) in natural file order.

Stack: **C++20 · Qt 6 (Widgets + QGraphicsView) · CMake + vcpkg**. Currently macOS-only;
Windows/Linux come later. Qt is consumed as a **prebuilt** install; vcpkg (manifest mode)
provides everything else.

Full design in `doc/DESIGN.md`; the milestone/iteration plan (what to do next) in
`doc/TODO.md` — consult it before starting feature work.

## Build / run / test

Requires two environment variables (the CMake preset reads them):

```bash
export VCPKG_ROOT=/path/to/vcpkg          # a microsoft/vcpkg checkout
export QT_ROOT=$(brew --prefix qt)        # prebuilt Qt 6 (Homebrew locally)

cmake --preset default                    # configure; vcpkg installs deps on first run
cmake --build --preset default            # builds app + viewer-core + tests
ctest --test-dir build/default --output-on-failure   # run all tests
ctest --test-dir build/default -R test_archive_source # run ONE test target

# run the app
./build/default/image-viewer.app/Contents/MacOS/image-viewer [path-to-image|folder|archive]
```

Use `--preset release` for an optimized build. Headless smoke-run: prefix with
`QT_QPA_PLATFORM=offscreen`.

### Lint / format (also gated in CI)

```bash
scripts/format.sh            # clang-format -i over src/ and test/
scripts/format.sh --check    # verify only (CI uses this)
scripts/tidy.sh              # clang-tidy over src/ (needs a configured build dir)
scripts/tidy.sh --strict     # warnings-as-errors (CI uses this)
```

Scripts target macOS bash 3.2 and inject the SDK sysroot so Homebrew `clang-tidy` can parse
AppleClang's `compile_commands.json`. Code style is **Go-like: opening braces stay on the
same line** (`.clang-format`, `BreakBeforeBraces: Attach`).

## Architecture

The core abstraction is **`ImageSource`** (`src/source/ImageSource.h`): `count()` /
`entryName()` / `readEntry()`, plus `openError()` to distinguish "failed to open (with a
reason)" from "opened but empty". It decouples *where image bytes come from* from *how
they're shown*, so a folder and an archive look identical to the viewer.

- `FolderSource` — images in a directory, natural-sorted (`QCollator` numeric mode).
- `ArchiveSource` — images inside an archive via **libarchive**, with a sequential
  fast-path: a persistent reader + per-entry ordinal index makes forward paging O(1)
  amortized (only backward seeks reopen); concurrent reads serialize on an internal mutex.

Decoding goes exclusively through **`decodeImage()`** (`src/decode/`) — a decoder
registry: probes (magic bytes, plus the name-hint suffix for RAW which has no unified
magic) pick a specialized decoder — **libavif**+dav1d for AVIF, **libheif**+libde265 for
HEIC/HEIF, **LibRaw** (`raw_r`, thread-safe) for camera RAW; AVIF is probed before HEIF
to disambiguate mif1-branded files — falling back to QImageReader (EXIF auto-rotation).
`decodableExtensions()` is the single source of truth for which suffixes count as images;
sources filter with it. **`Browser`** (`src/browse/`) is the
playlist model — source + current index — orchestrating cache lookup → decode → prefetch
over **`ImageCache`** (byte-budget LRU, generation-guarded against stale inserts after a
source switch) and **`Prefetcher`** (forward-biased, default 3 ahead / 1 behind). All of
this compiles into a **GUI-free `viewer-core` static library** (see top-level
`CMakeLists.txt`) that **both the app and the tests link against** — so core logic is
tested without the GUI. The app layer (`src/MainWindow`, `src/ImageView`) is
presentation-only: `MainWindow` talks to `Browser`; `ImageView` is a `QGraphicsView`
subclass handling zoom/pan/fit.

Tests use **Qt Test** (`test/`), generating real PNGs via `QImage` and real zips via
libarchive (`test/support/TestData`) — never hand-crafted bytes.

## Conventions

- **Commits**: Conventional-commit prefix + **Chinese** description; body as a `1. 2. 3.`
  numbered list; **no `Co-Authored-By` lines**.
- Keep the CI Qt version aligned with local Homebrew Qt.

## CI gotchas (`.github/workflows/ci.yml`, macOS)

- **Qt must be 6.11+**. Qt 6.8's QtGui links the `AGL` framework, which was removed from the
  Xcode 26 SDK that `macos-latest` ships → `ld: framework 'AGL' not found`. 6.11 dropped it.
- **vcpkg binary cache**: `lukka/run-vcpkg` forces `VCPKG_BINARY_SOURCES=clear;x-gha` and
  relocates the default cache, which defeats caching `~/.cache/vcpkg/archives`. The working
  setup overrides `VCPKG_BINARY_SOURCES` on the Configure step to a `files` provider dir that
  `actions/cache` persists (Configure: ~190s cold → ~7s warm).
- `vcpkg.json`'s `builtin-baseline` must match `run-vcpkg`'s `vcpkgGitCommitId`.
