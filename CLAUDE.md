# CLAUDE.md

给 Claude Code 等 agent 的仓库速查。详细设计见 `doc/DESIGN.md`，迭代计划见 `doc/TODO.md`——开工前先看 TODO 确定当前里程碑。

## 项目

跨平台开源看图器（对标 BandiView）：浏览各种格式的图片，按自然顺序翻看压缩包（zip / cbz / 7z / rar / tar）里的图片。目前只做 macOS，Windows / Linux 在 M5。

技术栈：**C++23 · Qt 6.12（Widgets + QGraphicsView）· CMake + vcpkg**。Qt 用预编译版（本地是 Homebrew），其余依赖由 vcpkg（manifest 模式）提供。

## 构建 / 运行 / 测试

```bash
export VCPKG_ROOT=/path/to/vcpkg          # CMake preset 读这两个环境变量
export QT_ROOT=$(brew --prefix qt)

cmake --preset default                    # 配置；首次会由 vcpkg 编译依赖
cmake --build --preset default            # 编译 app + viewer-core + 测试
ctest --test-dir build/default --output-on-failure      # 全部测试
ctest --test-dir build/default -R test_archive_source   # 单个测试

./build/default/image-viewer.app/Contents/MacOS/image-viewer [图片|文件夹|压缩包]
```

优化构建用 `--preset release`；无界面冒烟运行加 `QT_QPA_PLATFORM=offscreen`。

## 格式与静态检查（CI 也卡这两项）

```bash
scripts/format.sh [--check]    # clang-format；--check 只检查不改（CI 用）
scripts/tidy.sh [--strict]     # clang-tidy，需先配置构建目录；--strict 有告警即失败（CI 用）
```

代码风格：大括号不换行（Go 风格，`BreakBeforeBraces: Attach`）。脚本兼容 macOS 自带的 bash 3.2，并为 Homebrew clang-tidy 注入 SDK sysroot。

## 架构

- `src/source/`：`ImageSource`（`count` / `entryName` / `readEntry` / `openError`），文件夹与压缩包对上层一致。
- `src/decode/`：`decodeImage(bytes, nameHint, target)` 是唯一的解码入口（专用解码器注册表 + QImageReader 兜底），按目标尺寸解码并返回 `DecodedImage`（显示格式的图 + 原图尺寸 + 动画字节）；`decodableExtensions()` 是「哪些后缀算图片」的唯一来源。
- `src/schedule/`：`DecodeScheduler` 是读取 + 解码的唯一入口（P0 当前页 > P1 翻页方向 > P2 反方向 > P3 缩略图，按 `(EntryKey, 档)` 在途去重，结果写缓存后以信号回 UI 线程）。
- `src/cache/`：`ImageCache` 按档（Thumb / Fit / Full）分道 LRU，键 `ImageKey` = (来源 id, 列表下标, 档)。
- `src/browse/`：`Browser`（来源 + 当前索引）；过渡态——当前页未命中仍同步解码，预读走调度器。
- 以上编译成无 GUI 的 `viewer-core` 静态库，app 和测试都链接它；`MainWindow` / `ImageView` 只做展示。

> **M2.5 重构进行中**：改 `browse/` `cache/` `source/` 之前先读 `doc/DESIGN.md` §3、§4、§8。按尺寸档解码、`DecodeScheduler`、分档缓存已落地；同步的 `Browser::currentImage()` / `currentAnimation()`、单游标的 `ArchiveSource` 还会被替换（异步 Browser + 压缩包按格式分来源）。**不要在旧的同步接口上开发 M3 功能。**

测试用 Qt Test（`test/`）：PNG 用 `QImage` 现生成，zip 用 libarchive 现写（`test/support/TestData`），各格式真实样本放在 `test/data/`；不要手写字节。

## 约定

- 提交：约定式前缀 + 中文描述，正文用 `1. 2. 3.` 编号，**不加 `Co-Authored-By`**。
- CI 的 Qt 版本与本地 Homebrew Qt 保持一致。

## CI 的坑（`.github/workflows/ci.yml`，macOS）

- **Qt 至少 6.11**（CI 锁 `6.12.*`）：6.8 的 QtGui 链接了 `AGL`，而 Xcode 26 SDK 已移除该框架，会报 `ld: framework 'AGL' not found`。
- **clang-tidy / clang-format 没锁版本**（CI 装 Homebrew 最新版）：LLVM 新主版本可能新增检查，代码不变也会让 `--strict` 失败（LLVM 23 新增了 `readability-trailing-comma`、`bugprone-signed-bitwise`）。`llvm@23` 目前只是 `llvm` 的别名，等 LLVM 24 发布后才能锁版本。
- **vcpkg 二进制缓存**：`lukka/run-vcpkg` 会强制 `VCPKG_BINARY_SOURCES=clear;x-gha`，使缓存失效；Configure 步骤要把它覆盖成由 `actions/cache` 持久化的 `files` 目录（冷启动约 190s → 命中约 7s）。
- `vcpkg.json` 的 `builtin-baseline` 必须和 `run-vcpkg` 的 `vcpkgGitCommitId` 一致。
