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
        ┌───────▼────────┐   缓存层：LRU 解码缓存 + 邻页异步预读
        │   ImageCache    │
        └───────┬────────┘
                │
        ┌───────▼────────┐   视图层：缩放/平移/适应（+未来大图分块）
        │   ImageView     │   QGraphicsView
        └───────┬────────┘
                │
        ┌───────▼────────┐   应用层：导航/菜单/快捷键/拖放/会话
        │   MainWindow    │
        └────────────────┘
```

`ImageSource` 把「字节从哪来」与「怎么显示」解耦——文件夹与压缩包对上层完全一致。
当前 `source/` 三个文件编译为无 GUI 的 `viewer-core` 静态库，App 与测试共享。

## 4. 关键子系统

### 4.1 来源 ImageSource
- **现状**：`FolderSource`、`ArchiveSource`；`QCollator` 自然排序；按 `QImageReader` 支持的后缀过滤非图片。
- **演进**：抽象顺序读快路径接口；递归子目录；包内子目录；非 UTF-8 包名编码探测（GBK 等，用 `QStringDecoder`/ICU）；加密包密码回调。

### 4.2 解码与格式
- **现状**：`QImageReader`（内置 PNG/BMP/PPM…；插件 JPEG/GIF/WebP/TIFF via qtimageformats）。
- **演进**：经 vcpkg 接 **libheif**(HEIC)/**libavif**(AVIF)/**libraw**(RAW)/(可选)**libjxl**(JXL)；建立 `ImageDecoder` 注册表，按后缀 + magic bytes 选解码器，兜底回退 `QImageReader`；动画（GIF/WebP/APNG）播放；读 EXIF 方向并自动旋转。

### 4.3 渲染
- **现状**：`QGraphicsView` + `QGraphicsPixmapItem`，滚轮缩放（跟随光标）、拖拽平移、适应/实际大小，`SmoothPixmapTransform`。
- **演进**：可选 `QOpenGLWidget` viewport 提升大图性能；超大图（gigapixel）分块、按可视区域解码；高质量下采样；HiDPI / `devicePixelRatio` 适配。

### 4.4 缓存与预读（M1 重点）
- 解码结果 **LRU 缓存**，按字节/像素预算（默认上限可配）。
- 当前索引**前后 N 张异步预解码**（`QThreadPool`/`QtConcurrent`）；快速翻页时**取消过期任务**。
- 来源字节读取：Archive **顺序读快路径**（保持句柄顺序读，避免每次 O(n) 重开扫描），必要时建立 entry→偏移索引。

### 4.5 压缩包
- libarchive 读 `zip/7z/rar/tar/cb*`。
- 改进：顺序 reader 缓存；包名编码探测；加密包密码输入；（可选）嵌套包。

### 4.6 导航与会话
- playlist 模型（来源 + 当前索引），自然排序，环绕翻页，跳转首/末/第 N 张。
- `QSettings` 记忆窗口几何、最近打开、上次目录。

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
- 缓存内存上限可控（默认约 512MB）。

## 7. 测试策略

- **单元（Qt Test）**：来源 / 解码 / 缓存逻辑（来源已覆盖）。
- **集成**：`offscreen` 下端到端 open→navigate。
- **回归**：损坏/边界样本集；CI 三平台跑 `ctest`。
