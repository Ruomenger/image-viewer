# 设计方案（Design）

> 本文是项目的总体技术方案。迭代任务清单见 [TODO.md](./TODO.md)。
> 面向 agent 的速查见仓库根的 [CLAUDE.md](../CLAUDE.md)。

## 1. 目标与范围

跨平台（macOS → Windows/Linux）开源看图器，体验对标
[BandiView](https://en.bandisoft.com/bandiview/)。

**核心能力**
- 快速浏览各种格式的图片。
- 直接打开图片压缩包（`zip/cbz/7z/cbr/rar/tar`），按自然顺序浏览包内图片。
- 流畅的缩放 / 平移 / 适应窗口，翻页无卡顿。
- 轻量、启动快。

**非目标（至少初期不做）**
- 图片编辑、批量转换。
- 图库管理 / 标签 / 数据库。
- 云同步。

## 2. 技术栈与原则

- **C++20 · Qt 6（Widgets + QGraphicsView）· CMake + vcpkg**；Qt 用预编译，vcpkg 管其余依赖。
- **核心与 GUI 解耦**：可测试逻辑沉到 `viewer-core`，GUI 只做展示与交互。
- **解码/格式复用成熟 C 库**：不自己写解码器（libarchive / libheif / libavif / libraw…）。
- **体验优先**：预读 + 缓存消除翻页延迟。
- **渐进跨平台**：macOS 先稳定，再铺 Windows/Linux。

## 3. 总体架构

```
            open(path)
                │
        ┌───────▼────────┐   来源层：字节从哪来
        │   ImageSource   │   FolderSource / ArchiveSource (+未来 Recursive/Nested)
        └───────┬────────┘
                │ readEntry(i) -> QByteArray
        ┌───────▼────────┐   解码层：字节 -> QImage
        │  ImageDecoder   │   QImageReader + 插件（未来 libheif/libavif/libraw 注册表）
        └───────┬────────┘
                │ QImage
        ┌───────▼────────┐   缓存层：LRU 解码缓存（代次防竞态）+ 邻页异步预读（前偏）
        │   ImageCache    │
        └───────┬────────┘
                │
        ┌───────▼────────┐   编排层：playlist（来源+当前索引），查缓存→解码→预读
        │     Browser     │   viewer-core 内、无 GUI 依赖，导航逻辑可单测
        └───────┬────────┘
                │
        ┌───────▼────────┐   视图层：缩放/平移/适应（+未来大图分块）
        │   ImageView     │   QGraphicsView
        └───────┬────────┘
                │
        ┌───────▼────────┐   应用层：菜单/快捷键/拖放（纯展示，只依赖 Browser）
        │   MainWindow    │
        └────────────────┘
```

`ImageSource` 把「字节从哪来」与「怎么显示」解耦——文件夹与压缩包对上层完全一致。
`source/` `decode/` `cache/` `browse/` 编译为无 GUI 的 `viewer-core` 静态库，App 与测试共享。

## 4. 关键子系统

### 4.1 来源 ImageSource
- **现状**：`FolderSource`、`ArchiveSource`；`QCollator` 自然排序；按 `QImageReader` 支持的后缀过滤非图片；`openError()` 错误通道区分「打开失败（含原因）」与「打开成功但没图」；Archive 顺序读快路径（持久 reader + entry 序号索引，回退才重开，互斥锁串行化并发读）。
- **演进**：大包异步扫描（打开不冻结 UI）；递归子目录；包内子目录；非 UTF-8 包名编码探测（GBK 等，用 `QStringDecoder`/ICU）；加密包密码回调。

### 4.2 解码与格式
- **现状**：统一入口 `decodeImage()`（`src/decode/`）= **解码器注册表**（magic bytes 探测选专用解码器；当前 **libheif** 解 HEIC/HEIF，libde265 仅解码）+ 兜底 `QImageReader`（内置 PNG/BMP/PPM…；插件 JPEG/GIF/WebP/TIFF via qtimageformats；EXIF 方向 autoTransform）。可解码后缀由 `decodableExtensions()` 单点提供，来源层据此过滤——「能列出的就能解码」由 decode 模块负责。
- **演进**：注册表追加 **libavif**(AVIF)/**libraw**(RAW)/(可选)**libjxl**(JXL)；动画（GIF/WebP/APNG）播放。

### 4.3 渲染
- **现状**：`QGraphicsView` + `QGraphicsPixmapItem`，滚轮缩放（跟随光标）、拖拽平移、适应/实际大小，`SmoothPixmapTransform`。
- **演进**：可选 `QOpenGLWidget` viewport 提升大图性能；超大图（gigapixel）分块、按可视区域解码；高质量下采样；HiDPI / `devicePixelRatio` 适配。

### 4.4 缓存与预读（M1，已完成）
- 解码结果 **LRU 缓存**（`ImageCache`），按字节预算（默认 256MB，构造可配）；**代次（generation）防竞态**：clear() 递增代次，在途预读任务的写入锁内校验代次，换源后旧图不会污染新缓存。
- **前偏异步预读**（`Prefetcher`，`QThreadPool` 2 线程）：向前 3 张、向后 1 张（向前翻页是主导模式）；快速翻页/换源时**取消过期任务**（epoch + 队列清空）。
- Archive **顺序读快路径**已落地（见 4.1）。
- 演进：当前页未命中时异步解码 + 加载占位（UI 线程不做同步解码）。

### 4.5 压缩包
- libarchive 读 `zip/7z/rar/tar/cb*`；顺序 reader 常驻（见 4.1）。
- 改进：包名编码探测；加密包密码输入；（可选）嵌套包。

### 4.6 导航与会话
- **现状**：`Browser`（`src/browse/`）= playlist 模型（来源 + 当前索引），环绕翻页、任意跳转、打开单文件定位到所在文件夹索引;统一编排「查缓存 → 解码 → 触发预读」及 cache/prefetcher 的换源协议。
- **演进**：跳转首/末/第 N 张的快捷键；`QSettings` 记忆窗口几何、最近打开、上次目录。

### 4.7 UI / 交互
- 全屏、幻灯片（定时）、缩略图/胶片条、状态栏（尺寸/序号/缩放%）、右键菜单、可配置快捷键、深/浅主题、i18n（中/英）。

## 5. 跨平台计划

| 平台 | vcpkg triplet | Qt 获取 | 打包 | CI |
|---|---|---|---|---|
| macOS（已通）| arm64-osx | Homebrew / aqt | `.app` → `.dmg` | macos-latest |
| Windows | x64-windows | aqt | windeployqt + Inno/NSIS | windows-latest |
| Linux | x64-linux | 系统 Qt / aqt | AppImage（linuxdeployqt）| ubuntu-latest |

最终把 CI 改成 `os: [macos, windows, ubuntu]` 矩阵跑 `ctest`。

## 6. 性能目标

- 翻页响应 **< 50ms**（命中预读缓存）。
- 普通照片解码 < 100ms；大图（>50MP）首屏 < 500ms（分块）。
- 缓存内存上限可控（默认 256MB，`ImageCache`/`Browser` 构造可配；运行时可配等 M3 的 `QSettings`）。

## 7. 测试策略

- **单元（Qt Test）**：来源 / 解码 / 缓存逻辑（来源已覆盖）。
- **集成**：`offscreen` 下端到端 open→navigate。
- **回归**：损坏/边界样本集；CI 三平台跑 `ctest`。
