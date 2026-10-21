# 设计方案（Design）

> 本文是项目的总体技术方案。迭代任务清单见 [TODO.md](./TODO.md)。
> 面向 agent 的速查见仓库根的 [CLAUDE.md](../CLAUDE.md)。
>
> **2026-10-16 修订**：管线异步化（M2.5）——异步 Browser + 统一解码调度器、按尺寸档解码、压缩包按格式分读取策略、页模型（双页 / RTL / 跨来源）。原因与改动对照见 [§8](#8-修订记录)。
> §3、§4 描述的是**目标结构**；各子系统的「现状」写当前代码，「问题」写与目标的差距。

## 1. 目标与范围

跨平台（macOS → Windows/Linux）开源看图器，体验对标
[BandiView](https://en.bandisoft.com/bandiview/)。

**核心能力**
- 快速浏览各种格式的图片。
- 直接打开图片压缩包（`zip/cbz/7z/cbr/rar/tar`），按自然顺序浏览包内图片。
- 流畅的缩放 / 平移 / 适应窗口，翻页无卡顿——UI 线程从不等待读取与解码。
- 漫画阅读：双页（跨页）显示、右到左阅读；读到末尾可接着进入同目录的下一个文件夹 / 压缩包。
- 轻量、启动快。

**非目标（至少初期不做）**
- 图片编辑、批量转换。
- 图库管理 / 标签 / 数据库。
- 云同步。

## 2. 技术栈与原则

- **C++23 · Qt 6.12（Widgets + QGraphicsView）· CMake + vcpkg**；Qt 用预编译，vcpkg 管其余依赖。
  - C++23 标准库：本地 AppleClang 21 已验证 `std::expected` / `std::ranges::to` / `std::print` / `std::flat_map` / deducing this 可用，M2.5 的错误通道（打开失败、解码失败）用 `std::expected`；libc++ 尚无 `std::move_only_function`，调度器任务用 `std::function` 或自定义包装。CI 与将来 Windows / Linux 的编译器可能更旧，新特性以 CI 实际编译通过为准。
- **核心与 GUI 解耦**：可测试逻辑沉到 `viewer-core`，GUI 只做展示与交互。
- **解码/格式复用成熟 C 库**：不自己写解码器（libarchive / libheif / libavif / libraw…）。
- **UI 线程不读字节、不解码**：所有读取与解码经同一个调度器在工作线程完成，UI 只发请求、收结果。
- **按需解到够用的尺寸**：适应窗口只解到屏幕尺寸，放大才解原图——内存预算与首屏速度都靠它。
- **体验优先**：预读 + 缓存消除翻页延迟。
- **渐进跨平台**：macOS 先稳定，再铺 Windows/Linux。

## 3. 总体架构

```
   MainWindow / ImageView / 胶片条            GUI：发请求、收信号；不读字节、不解码
        │ open / goTo / next / prev   ▲ opened / pageChanged / pageReady
   ┌────▼─────────────────────────────┴───┐
   │  Browser (QObject) + PageLayout      │   会话：当前来源 + 相邻来源 + 当前页
   └────┬─────────────────────────────▲───┘   单 / 双页、LTR / RTL、末页行为
        │ request(key, tier, prio)    │ 完成（排队回 UI 线程）
   ┌────▼─────────────────────────────┴───┐
   │  DecodeScheduler                     │   唯一解码入口：优先级队列、在途去重、取消
   └────┬────────────────────┬────────────┘   工作线程池（栈 8MB）
        │ get / put          │ 工作线程：readEntry → decodeImage
   ┌────▼───────────┐   ┌────▼─────────────────┐
   │ ImageCache     │   │ ImageSource          │   Folder / ZipArchive（无状态随机读）
   │ (key, tier)    │   │  + ByteStore         │   StreamArchive（后台顺序抽取一遍）
   └────────────────┘   └──────────────────────┘
```

各层职责：
- **ImageSource**（`source/`）：字节从哪来；文件夹与压缩包对上层一致，压缩包按实际格式分两种读取策略（§4.1、§4.5）。
- **decodeImage**（`decode/`）：字节 → 图，按目标尺寸解码（§4.2）。
- **DecodeScheduler**（`schedule/`）：读取 + 解码的唯一入口；当前页、预读、缩略图共用一条优先级队列（§4.4）。
- **ImageCache**（`cache/`）：解码结果，按尺寸档分预算（§4.4）。
- **Browser**（`browse/`）：会话与导航，异步 API，调用线程上不做任何 I/O（§4.6）。
- **ImageView / MainWindow**：纯展示与交互（§4.3、§4.7）。

`source/` `decode/` `schedule/` `cache/` `browse/` 编译为无 GUI 的 `viewer-core` 静态库，App 与测试共享。

> **现状与目标**：M2.5 进行中。已落地：`decodeImage()` 按目标尺寸解码；`DecodeScheduler` + 按档分道的 `ImageCache` 取代了 `Prefetcher`（§4.4）；`Browser` 全异步（§4.6），viewer-core 在调用线程上不再读取或解码。App 层已切到异步接口（§4.3）：场景坐标 = 原图像素、按屏幕物理像素定 Fit 目标、放大时换 Full。压缩包已按实际格式分派（§4.5）：zip / 未压缩 tar 走 `ZipArchiveSource` 无状态随机读；7z / rar / 压缩 tar 仍是旧的单游标 `ArchiveSource`，待 `StreamArchiveSource` 替换。M2.5 剩余任务落地上图其余部分。

## 4. 关键子系统

### 4.1 来源 ImageSource
- **现状**：`FolderSource`、`ArchiveSource`；`QCollator` 自然排序；按 `decodableExtensions()` 过滤非图片；`openError()` 错误通道区分「打开失败（含原因）」与「打开成功但没图」；构造时在调用线程建列表；Archive 顺序读快路径（持久 reader + entry 序号索引，回退才重开，互斥锁串行化并发读——问题见 §4.5）。
- **目标（M2.5）**：接口（`count` / `entryName` / `readEntry` / `openError`）保留，契约收紧：
  - `readEntry` **只在调度器工作线程调用**，允许阻塞（流式压缩包要等后台抽取到位），必须支持并发调用。
  - 每个条目有**稳定 id**：缓存与在途去重按 `EntryKey = (来源 id, 条目 id)`。**已落地的取法**：来源 id = `ImageSource::id()`，每个来源实例唯一（同一路径重开也换新 id）；条目 id = 该实例列表中的下标——列表建成后不再变化，下标在实例内即稳定；列表要变（如重扫目录）就换新实例。若以后要在流式包列表完成前先显示第一张（§4.5 可选优化），届时再给条目单独的 id（如包内序号）。
  - 列表在后台建，`Browser::open` 不在 UI 线程扫描（已落地：来源在 `Browser` 自有的单线程里构造——不排进调度器，因为调度器不可抢占，P0 也要等执行中的任务跑完，RAW 全流程约 1s）；流式压缩包由来源自己的抽取线程边扫边报进度。
  - 压缩包按实际格式分 `ZipArchiveSource` 与 `StreamArchiveSource`，见 §4.5。
- **演进**：递归子目录；包内子目录；非 UTF-8 包名编码探测（GBK 等，用 `QStringDecoder`/ICU）；加密包密码回调。

### 4.2 解码与格式
- **现状**：统一入口 `decodeImage(bytes, nameHint, target)`（`src/decode/`）= **解码器注册表**（magic bytes + 后缀探测选专用解码器：**libavif**+dav1d 解 AVIF、**libheif**+libde265 解 HEIC/HEIF、**LibRaw** 解相机 RAW——RAW 无统一 magic 按后缀判定；ftyp 探测共用 `FtypBox`，AVIF 排在 HEIF 前消歧 mif1 主 brand；均仅解码）+ 兜底 `QImageReader`（内置 PNG/BMP/PPM…；插件 JPEG/GIF/WebP/TIFF via qtimageformats；EXIF 方向 autoTransform）。可解码后缀由 `decodableExtensions()` 单点提供，来源层据此过滤——「能列出的就能解码」由 decode 模块负责。按目标尺寸解码、显示格式输出、动画字节随结果返回均已落地（见下方「目标」，M2.5 第一步）；专用解码器可返回大于目标的图，由 `decodeImage` 统一缩放与转格式。
- **动画（现状）**：`decodeImage` 在 Qt 路径顺手判定多帧并把字节放进 `DecodedImage.animation`，随 `pageReady` 交给视图，`ImageView` 用 `QMovie` 播放，不再二次读取；缓存存整个 `DecodedImage`（Fit / Full 档含动画字节，Thumb 档只留首帧）。APNG 取决于 Qt png 插件支持（当前按静态首帧）。
- **已解决的问题**（M2.5）：原先永远解原图，2400 万像素照片解成 `RGB32` 约 96MB，默认 256MB 预算只放得下 2 张，而预读窗口要放 5 张（当前 + 前 3 + 后 1）——后入的预读图把先入的挤掉，翻到下一张反而未命中；动画判定要在 UI 线程二次读取字节。现在 App 层按屏幕物理像素传入 Fit 目标、Fit 预算按窗口张数抬高，动画字节随解码结果返回。

**目标（M2.5）**：解码按**目标尺寸**——`decodeImage(bytes, nameHint, target)`，`target` 为空表示原图。调度 / 缓存层用三个**尺寸档**换算 `target`：

| 档 | 用途 | 目标尺寸 |
|---|---|---|
| `Thumb` | 胶片条 / 缩略图 | 长边约 256px |
| `Fit` | 适应窗口显示（默认） | 屏幕物理像素（App 层传入；与窗口大小无关，缩放窗口不重解） |
| `Full` | 放大超过 Fit 档分辨率时 | 原图（超大图以后改分块，见 §4.3） |

原图不大于目标时直接返回原图（此时 Fit 即 Full，不再另解）。

- 各解码器按目标尺寸走捷径：
  - JPEG：`QImageReader::setScaledSize`（libjpeg DCT 1/2、1/4、1/8 缩放，解码量成倍下降）。`ScaledSize` 作用在 EXIF 旋转之前，带 90° 旋转的图按转置后的目标换算。
  - RAW：取**内嵌 JPEG 预览**（LibRaw `unpack_thumb`），长边覆盖目标才用；预览缺 EXIF 时 `dcraw_make_mem_thumb` 会按 `sizes.flip` 补一段方向标记，交给 QImageReader 路径摆正并走 DCT 缩放。预览不够大才走 `dcraw_process`：半尺寸输出仍覆盖目标时开 `half_size`（输出边长减半、快很多），否则全尺寸再缩——无条件开会让大屏的 Fit 档比屏幕还糊；只有 Full 跑完整流程且不用预览。`originalSize` 恒为全流程输出尺寸（`get_mem_image_format`，open 后即可取）。
  - HEIF：取长边覆盖目标的最小内嵌缩略图（iPhone 约 320px，实际只有 Thumb 档会命中）。
  - 其余格式：解原图后在工作线程平滑缩放。
- 输出统一为 `RGB32`（不透明）/ `ARGB32_Premultiplied`（有 alpha），格式转换留在工作线程。HEIF / AVIF 无 alpha 时标成 `RGBX8888`，按不透明处理。
- 返回结构如下；动画判定在工作线程顺手完成（字节已在手），取代 `currentAnimation()` 的二次读取：

```cpp
struct DecodedImage {
    QImage image;          // 已按目标尺寸缩放
    QSize originalSize;    // 原图尺寸：状态栏、缩放百分比、双页配对都用它
    QByteArray animation;  // 多帧动画的原始字节（供 QMovie），静态图为空
};
```

- **演进**：（可选）注册表追加 **libjxl**(JXL)；AVIF 的 irot/imir 方向变换。

### 4.3 渲染
- **现状**：`QGraphicsView` + `QGraphicsPixmapItem`，滚轮缩放（跟随光标）、拖拽平移、适应/实际大小；动画由 `QMovie` 逐帧替换 pixmap。下方「目标」除双页外均已落地（`ImageView`、`MainWindow`，编进 `viewer-ui` 静态库）。补充：
  - 「Fit 像素被放大显示」的判定 = 视图缩放 × 图元放大（原图 / 显示图）× `devicePixelRatio` > 1（留 1% 浮点容限），每张图只发一次 `needsSharperImage`；已是原图或动画（`QMovie` 帧本身是原图尺寸）时不发。Fit 目标 = 屏幕物理像素，所以适应窗口时不会触发。
  - 同一页再次 `pageReady`（Full 到位、Fit 目标调大）只换 pixmap；新的一页才重铺场景、重新适应窗口。当前页解码失败时清空画面，免得上一页的图与标题对不上。
  - Fit 目标在窗口构造时按初始屏幕设定，原生窗口建好后跟随 `QWindow::screenChanged` 更新。
  - 图元用 `SmoothTransformation`：`QGraphicsPixmapItem` 默认的 Fast 会盖过视图的 `SmoothPixmapTransform` 提示。
- **目标（M2.5）**：
  - **场景坐标 = 原图像素坐标**：pixmap item 按 `originalSize / image.size()` 缩放放置，视图变换的缩放系数就是缩放百分比（100% = 原图 1:1）。Fit 档换成 Full 档只替换 pixmap、重设 item 缩放，视图的缩放与平移位置不动。
  - 实际放大倍数超过 Fit 档分辨率（Fit 像素被放大显示）时，向 Browser 请求 Full 档；到位前先显示放大的 Fit 档。
  - 当前页未就绪时保留上一页画面（可加轻量加载提示），不清屏、不阻塞。
  - 双页时两张按等高并排放进同一场景（RTL 时左右对调），整体适配。
- **演进**：可选 `QOpenGLWidget` viewport 提升大图性能；超大图（gigapixel）分块、按可视区域解码——作为 Full 档的替代挂在同一套请求上；HiDPI / `devicePixelRatio` 适配。

### 4.4 调度与缓存
- **现状（M2.5 过渡态）**：下方「目标」中的 `DecodeScheduler`（`src/schedule/`）与按档分道的 `ImageCache` 已落地，`Prefetcher` 已删除。实现要点：
  - API：`setSource` / `setFitTarget` / `focus(index, direction)`（导航：排 P0–P2 窗口、丢弃窗口外的排队任务）/ `request(index, tier, priority)`（单个请求，如 Full、缩略图）；缓存命中时这两个调用直接返回结果，否则稍后发 `ready` / `failed` 信号。
  - 执行中的任务按票号登记在 `(EntryKey, 档)` 下；换源、改 Fit 目标即注销，跑完发现票号不在就丢弃结果（不入缓存、不送达），同键的新请求另起任务而不是挂到作废的任务上。
  - 结果在工作线程写缓存，再排队回调度器所在线程（UI），送达前按来源 id、Fit 目标版本再校验一次——与 `setSource` / `setFitTarget` 在同一线程串行，没有「校验后、送达前换源」的窗口。
  - 每次完成都发信号（含预读）：用户翻到一张正在预读的页，`Browser` 只需按键匹配，无需单独登记等待者。
  - 当前页也走调度器（`Browser` 异步化后），当前页与预读之间天然在途去重。
- **目标（M2.5）**：**`DecodeScheduler`** 取代 `Prefetcher`，成为读取 + 解码的唯一入口。
  - 请求 = `(EntryKey, 档, 优先级)`。
  - 优先级：**P0 当前页**（Fit；需要时 Full）> **P1 翻页方向的邻页**（默认 3 页）> **P2 反方向**（默认 1 页）> **P3 缩略图**。「翻页方向」取最近一次翻页的方向（RTL 只反转键位，不改变方向语义）。
  - **在途去重**：同一 `(EntryKey, 档)` 已在队列中则只提升优先级，已在执行则挂上等待者，绝不重复读 / 解。
  - **取消**：导航后不在新窗口内的排队任务直接丢弃；正在执行的跑完入缓存（多半还有用）；换源时丢弃旧来源的全部排队任务，迟到结果因 key 带来源 id 不会串到新来源。
  - 自有优先级队列（`QThreadPool` 只当线程用，或自管 `QThread`），以支持去重与改优先级；线程数默认 2，**栈 8MB 保留**。
  - 结果先写缓存，再排队通知回 UI 线程（`Browser`）。
  - 末页不环绕预读（末页行为见 §4.6）；跨来源暂不预读。
- **缓存（M2.5）**：
  - `ImageCache` 键 = `(EntryKey, 档)`，**按档分预算**：
    - Fit 预算至少容纳整个预读窗口（≈ 窗口张数 × 屏幕物理像素 × 4B；5K 屏 5 张约 300MB），默认取它与 256MB 的较大值；
    - Full 只保留当前页；
    - Thumb 独立小预算。

    放大看原图、刷胶片条都不会挤掉预读结果。
  - 换源时清掉旧来源条目以释放内存；正确性靠 key 中的来源 id，而不是 `clear()` 的时序（代次机制随之简化）。
- **演进**：按系统可用内存自适应预算（M4）。

### 4.5 压缩包
- **现状**：`openArchive()` 用 libarchive 读第一个 header 后按 `archive_filter_code()` / `archive_format()` 分派，不信后缀。无压缩过滤器且格式为 zip / tar → `ZipArchiveSource`：列表从中央目录 / 条目头一次扫出；每次 `readEntry` 独立打开、按包内序号前进到目标再读——seekable zip 读取器的 `read_data_skip` 是空操作、条目按本地头偏移排序（重开后序号稳定），未压缩 tar 的跳过走 lseek——无共享游标、无锁，天然可并发；千页 stored cbz 实测单次随机读最大 5.4ms（Debug）。其余格式（7z / rar / 压缩 tar）仍走旧的 `ArchiveSource`：构造时全量扫包建列表（在 `Browser` 的后台线程），读取走单个持久 reader（游标）+ 互斥锁，目标在游标之后就接着扫，回退才重开。libarchive 句柄封装为 `ArchiveReader`（RAII，出错即作废句柄），列表逻辑两类来源共用。
- **问题**（zip / 未压缩 tar 已解决，7z / rar / 压缩 tar 仍存在）：「单游标 + 一把锁」只在访问单调向前时成立，而实际访问经常回退：
  - 两个预读线程同时抢 i+1、i+2，i+2 先拿到锁则 i+1 变成回退 → 重开；
  - 动画条目 `currentAnimation()` 二次读取同一 entry，游标已越过 → 每个包内 GIF 都从头重扫；
  - 打开时第 0 页的「向后 1 张」环绕到最后一张，一打开就扫到包尾；
  - 包内物理顺序与自然排序不一致（未补零的 1/2/10 命名很常见）时，顺序翻页也频繁回退。

  扫描全程持锁，UI 线程未命中时排在后面等。zip 重开只是重读头（seekable 读取器走中央目录，跳过只是 seek）；solid 7z、rar、压缩 tar 重开等于把前面的内容全部重新解压。此外大 solid 包在构造时全量扫描会冻结界面。

**目标（M2.5）**：按 libarchive 探测到的**实际格式**（`archive_format()` / `archive_filter_code()`）分两种来源，不信后缀——`.cbr` 里装的是 zip 很常见。

| 来源 | 适用格式 | 列表 | 读取 |
|---|---|---|---|
| `ZipArchiveSource` | zip / cbz、未压缩 tar | 中央目录 / 头部，毫秒级 | **无状态随机读**：每次读取独立打开、按序号跳到目标（跳过即 seek，不解压）；无共享游标、无全局锁，天然可并发 |
| `StreamArchiveSource` | 7z、rar、tar.gz / bz2 / xz / zst 等需顺序解压的 | 后台线程完成 | **后台顺序抽取一遍**：按包内顺序解压，每个条目的编码字节存入 `ByteStore`；`readEntry` 等目标条目抽取到位即返回；**全程不重开、每个条目只解压一次** |

- `ByteStore`：内存预算（默认 512MB）内常驻，超出写入会话临时目录（`QTemporaryDir`，关闭来源即删）。漫画图片本身已压缩，整本编码字节通常 100–300MB。
- 需要解压才能拿到列表的格式（solid rar、压缩 tar），列表与抽取是同一遍；完成前 UI 显示进度、不冻结。7z 头部在包尾、列表很快，随后抽取在后台进行。
- 随机读的性能需实测：千页 cbz 单次随机读若超过约 10ms，换 libzip / minizip-ng（真随机访问、每线程一个句柄）。
- 可选优化：流式包列表未完成前先显示已抽出的第一张。
- **演进**：包名编码探测；加密包密码输入；（可选）嵌套包。

### 4.6 导航与会话
- **现状**：下方「目标」的异步 `Browser` 已落地（`src/browse/`），单页布局，末页「停住 + `reachedEnd`」。与目标的细节差异与补充：
  - 另有 `openSource(source, entry)` 打开已建好的来源（测试注入假来源；M3 进入相邻来源也走它），结果信号在调用返回前同步发出。连续 `open` 时只有最后一次生效。
  - 同一页的 `pageReady` 可能再次发出，且只在更清晰时发：`requestFull()` 换成原图，或 `setFitTarget()` 调大后按新尺寸重解（调小不重发，已有的更清晰）。原图不大于 Fit 目标、或动画条目时 `requestFull()` 不做任何事。
  - `listingProgress` 已声明，`StreamArchiveSource` 落地后才会发出。
- **目标（M2.5）**：`Browser` 改为 `QObject`，**全部异步**，调用线程上不做任何读取、列表与解码：

```cpp
struct Page { int first; int count; };  // 从第 first 个条目起的 1 或 2 个条目

class Browser : public QObject {
    Q_OBJECT
public:
    void open(const QString& path);           // 列表在后台建；新来源生效前保留原来源
    void goTo(int entry);                     // 跳到包含该条目的页
    void next();                              // 步长由 PageLayout 决定（1 或 2 个条目）
    void prev();
    void setFitTarget(QSize physicalPixels);  // App 层传入屏幕物理像素，换算 Fit 档
    void requestFull();                       // 视图放大超过 Fit 档分辨率时
signals:
    void opened(int entryCount);              // 新来源生效（含进入相邻来源）
    void openFailed(QString reason);          // 原来源保持不变
    void listingProgress(int scanned);        // 流式压缩包后台列表进度
    void pageChanged(Page page);              // 立即发出：序号 / 文件名先更新
    void pageReady(Page page, QList<DecodedImage> images);  // 页内条目全部就绪
    void pageFailed(Page page, QString reason);
    void reachedEnd(bool forward);            // 末页向后 / 首页向前继续翻
};
```

- 缓存命中时 `pageReady` 在 `goTo` / `next` 返回前同步发出（<50ms 目标）；未命中时由调度器完成后排队发回；用户已翻走的页的结果不再发出。
- **页模型**：导航位置仍是条目下标，「页」由 `PageLayout` 从条目序列派生，含 1 或 2 个条目。M2.5 先只有单页布局；双页 / RTL 在 M3。
  - 单页 / 双页、LTR / RTL 可切换；RTL 时页内左右对调、翻页键方向对调。
  - 双页**相对当前位置配对**：当前条目与下一条目都是竖图才合成一页，横图（宽 > 高）单独成页；封面（第一张）可设单独；提供「错开一页」修正奇偶。只依赖相邻条目的 `originalSize`，预读本来就会拿到；向前翻时同理看前两个条目。
  - 代价是前后翻的配对可能不一致，换来不必为整本预先探测全部尺寸；不一致时用「错开一页」修正。
- **末页行为**：可配置「停住 / 环绕 / 进入下一个来源」。默认：末页继续翻 → `reachedEnd`，界面提示「已是最后一页」；再翻一次进入**相邻来源**（同目录下按自然排序的下一个文件夹 / 压缩包，跳过没有图片的），首页向前同理。取代现在的无条件环绕。M2.5 先实现「停住 + `reachedEnd`」，相邻来源在 M3。
- **演进**：跳转首 / 末 / 第 N 张的快捷键；`QSettings` 记忆窗口几何、最近打开、上次目录、每个来源的阅读位置与单双页 / 方向；跨来源预读。

### 4.7 UI / 交互
- 全屏、幻灯片（定时）、缩略图/胶片条、状态栏（尺寸/序号/缩放%）、右键菜单、可配置快捷键、深/浅主题、i18n（中/英）。
- 建在 M2.5 的约定上：胶片条 = 调度器 P3 + Thumb 档，只为可见格子发请求；状态栏尺寸与缩放% 用 `originalSize`（屏上显示的可能是 Fit 档）；幻灯片的下一张就是 P1 预读，定时到点时多半已在缓存；双页 / RTL 切换与末页提示见 §4.6。

## 5. 跨平台计划

| 平台 | vcpkg triplet | Qt 获取 | 打包 | CI |
|---|---|---|---|---|
| macOS（已通）| arm64-osx | Homebrew / aqt | `.app` → `.dmg` | macos-latest |
| Windows | x64-windows | aqt | windeployqt + Inno/NSIS | windows-latest |
| Linux | x64-linux | 系统 Qt / aqt | AppImage（linuxdeployqt）| ubuntu-latest |

最终把 CI 改成 `os: [macos, windows, ubuntu]` 矩阵跑 `ctest`。

## 6. 性能目标

- **UI 线程不等 I/O 与解码**：打开、翻页、缩放时事件循环单次阻塞 < 16ms（一帧）；viewer-core 不在调用线程调用 `readEntry` / `decodeImage`（有测试守卫，见 §7）。
- 翻页响应 **< 50ms**（命中预读缓存）。
- 普通照片 Fit 档解码 < 100ms（JPEG DCT 缩放 / RAW 内嵌预览）；大图（>50MP）首屏 < 500ms（先出 Fit 档，Full / 分块随后）。
- 压缩包：流式格式每个条目最多解压一次；打开 zip / 7z 到首图的时间与包大小基本无关。
- 缓存内存上限可控：Fit 预算按屏幕尺寸保证容纳预读窗口，Full 只留当前页（§4.4）；默认值 `Browser` 构造可配，运行时可配等 M3 的 `QSettings`。

## 7. 测试策略

- **单元（Qt Test）**：来源 / 解码 / 调度 / 缓存 / Browser 逻辑。
  - 调度器用**可控的假来源**（可阻塞、记录读取次数与所在线程）测：优先级插队、同 key 只读一次、换源后无迟到结果。
  - **UI 线程守卫**：测试来源在主线程被 `readEntry` 时直接失败，保证 Browser 不在调用线程做 I/O。
  - Browser 用 `QSignalSpy` 断言 `pageChanged` → `pageReady` 的顺序与内容。
  - 压缩包：libarchive 现场写 zip / 7z / tar.gz（RAR 写不了，需真实小样本）；来源暴露抽取 / 重开计数，断言任意访问顺序下流式格式每个条目只解压一次、zip 并发乱序读结果正确。
- **集成**：`offscreen` 下端到端 open→navigate。
- **回归**：损坏/边界样本集；CI 三平台跑 `ctest`。

## 8. 修订记录

### 2026-10-16 · 管线异步化（M2.5）

M1/M2 完成后复查发现，几处原本列为「演进」的事项其实是骨架级决定：不在 M3（UI）之前改，胶片条、幻灯片、状态栏都会建在要被推翻的同步接口上。

| 问题 | 原设计 | 修订 |
|---|---|---|
| UI 线程同步读 + 解码；当前页与预读两条路径、无在途去重 | `Browser::currentImage()` 同步拉取 + 独立 `Prefetcher` | 异步 `Browser` + 唯一的 `DecodeScheduler`（§4.4、§4.6） |
| 压缩包单游标 + 锁，回退即重开；solid 包重开 = 重新解压；打开时 UI 线程全量扫描 | 顺序读快路径 | 按实际格式：zip 无状态随机读 / 流式格式后台抽取一遍（§4.5） |
| 永远解原尺寸，大照片下预算装不下预读窗口；UI 线程整图转格式 | `decodeImage(bytes, nameHint)` | 目标尺寸 + Thumb / Fit / Full 三档，按档分预算，工作线程出显示格式（§4.2、§4.4） |
| 对标 BandiView 却没有双页 / RTL / 跨来源翻页，而它们都会改变导航模型 | 条目下标 + 无条件环绕 | 页模型 + 末页行为 + 相邻来源（§4.6） |

保留不动：`ImageSource` 抽象、解码器注册表与 `decodableExtensions()` 单点、QGraphicsView 渲染、viewer-core 与 GUI 解耦、测试与 CI。
