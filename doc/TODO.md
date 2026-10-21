# TODO / 迭代计划

总体方案见 [DESIGN.md](./DESIGN.md)。本文把工作切成里程碑，说明**每次该做什么**。

## 怎么用这份文件

每次开发按下面的循环来：

1. 从**当前里程碑**里挑最上面一条未完成任务（跨里程碑的小修可随手做）。
2. 在分支上实现，**同时补单元测试**（放 `test/`，用 Qt Test）。
3. 本地过一遍质量门禁：
   ```bash
   scripts/format.sh && scripts/tidy.sh --strict
   cmake --build --preset default && ctest --test-dir build/default --output-on-failure
   ```
4. 确认 CI（format / build / test / clang-tidy）全绿。
5. 按提交规范提交：约定式前缀 + 中文描述，正文 `1. 2. 3.` 编号，**不加 `Co-Authored-By`**。
6. 回来勾选对应复选框；一个里程碑做完再进入下一个。

> 约定：每条任务尽量有「验收方式」（单测或可观察的行为）。

---

## M0 · 工程基建 ✅ 已完成

- [x] 项目骨架（Qt6 / CMake / vcpkg），macOS 跑通
- [x] CI：format + build + test + clang-tidy（macOS）
- [x] clang-format（Go 风格附着大括号）+ clang-tidy 脚本
- [x] 抽出 `viewer-core` 静态库 + Qt Test 单元测试
- [x] vcpkg 二进制缓存（Configure 190s → 7s）
- [x] `CLAUDE.md` / `AGENTS.md` / `doc/` 设计与计划文档
- [x] 工具链升级：C++23 + Qt 6.12（本地 Homebrew 与 CI 对齐）；修复 LLVM 23 clang-tidy 新增检查的告警

## M1 · 浏览体验核心 ✅ 已完成

> 目标：翻页流畅不卡顿（命中缓存时 <50ms）。

- [x] 解码结果 **LRU 缓存**（按字节预算，上限可配）— `ImageCache`
- [x] **邻页异步预读**（`QThreadPool`），快速翻页/切换来源时取消过期任务 — `Prefetcher`，
      预读前偏（默认前 3 后 1，向前翻页是主导模式）
- [x] Archive **顺序读快路径**：持久 reader + entry 序号索引，顺序翻页 O(1) 摊还、
      回退才重开；按序号定位顺带修复同名 entry 只能读到第一个的问题 — `ArchiveSource`
- [x] EXIF 方向自动旋转 — `decodeImage()`（`src/decode/`，QImageReader autoTransform），
      所有解码统一走这一个入口（M2 解码器注册表在此扩展）
- [x] `Browser` 播放列表模型抽入 viewer-core（来源 + 当前索引，编排查缓存→解码→预读），
      `MainWindow` 退化为纯展示层 — `src/browse/`
- [x] `ImageSource::openError()` 错误通道：区分「打开失败（含原因）」与「打开成功但没有图片」
- [x] 缓存代次（generation）：修复换源后旧来源在途预读结果落入新缓存的竞态
- [x] 单测：缓存淘汰/代次、预读命中/前偏、Archive 顺序/回退/同名 entry/坏包报错、
      EXIF 方向、Browser 打开/导航环绕/错误路径
- 验收：`ArchiveSource` 读 N 张由 O(n²) 降到 ~O(n) ✅；连续翻页 <50ms（缓存命中，待实测）

> 注：M1 的同步取图（`Browser::currentImage()`）、独立 `Prefetcher`、Archive 单游标快路径
> 将在 M2.5 被替换，原因见 [DESIGN §8](./DESIGN.md#8-修订记录)。

## M2 · 现代格式支持 ✅ 主体完成（余下为可选项）

- [x] `ImageDecoder` 注册表：magic bytes 探测选专用解码器，失败兜底回退 `QImageReader`；
      可解码后缀集合由 decode 模块单点提供（`decodableExtensions()`），来源层据此过滤
- [x] vcpkg 引入 **libheif**（HEIC/HEIF）：`default-features: false` 仅解码
      （libde265，LGPL），避开默认特性里的 x265（GPL 编码器）
- [x] **libavif**（AVIF）：dav1d 解码器（BSD,解码专用）；ftyp 探测抽出共享的
      `FtypBox`(扫主 + 兼容 brand)，注册表中 AVIF 排在 HEIF 前消歧 mif1 主 brand
- [x] **libraw**（RAW：CR2/NEF/ARW/DNG…）：链 `libraw::raw_r` 线程安全变体（预读并发）；
      RAW 无统一 magic（多为 TIFF 变体），探测按后缀——`decodeImage()` 增加 nameHint 参数
- [x] 动画（GIF/WebP）播放：`Browser::currentAnimation()` 暴露动画条目原始字节
      （后缀预筛 + `QImageReader` 多帧探测），视图层 `QMovie` 播放;缓存/预读仍只存首帧。
      APNG 取决于 Qt png 插件（当前按静态首帧显示）
- [ ] （可选）**libjxl**（JXL）
- [ ] AVIF 的 irot/imir 方向变换（当前未应用，实拍样本罕见）
- [x] 单测：各格式样本解码、解码器选择逻辑
      （HEIC/AVIF：真实 sips 编码样本；RAW：raw.pixls.us 的 CC0 DNG 样本，均在 `test/data/`）

## M2.5 · 管线异步化 ✅ 代码完成（余下：真实 RAR 样本、真机手测）

> 目标：UI 线程永不等待读取 / 解码；当前页、预读、缩略图共用一个调度器；按尺寸档解码；
> 压缩包按实际格式分读取策略。设计见 DESIGN §3、§4，原因见 [DESIGN §8](./DESIGN.md#8-修订记录)。
> 为什么排在 M3 之前：M3 的胶片条、幻灯片、状态栏都直接建在这一层上。
>
> 顺序：解码档 → 调度器 + 缓存 → Browser 异步化 → App 层切换 → 清理。两条压缩包任务只依赖
> 「`readEntry` 只在工作线程调用」这一契约，可与 Browser / App 层并行做。

- [x] **解码按目标尺寸**（DESIGN §4.2）：`decodeImage(bytes, nameHint, target)` 返回 `DecodedImage`
      （`image` / `originalSize` / `animation`），原图不大于目标时直接返回原图；JPEG 走
      `QImageReader::setScaledSize`（EXIF 旋转 90° 时目标随之转置）；RAW 取内嵌 JPEG 预览（长边覆盖
      目标才用；`dcraw_make_mem_thumb` 按 `sizes.flip` 补 EXIF 方向，交给 Qt 路径摆正），不够大再
      `dcraw_process`（半尺寸输出仍覆盖目标才开 `half_size`，免得大屏 Fit 档发糊）；HEIF 取覆盖目标的
      最小内嵌缩略图；其余解原图后在工作线程平滑缩放；输出统一 `RGB32` / `ARGB32_Premultiplied`
      （HEIF / AVIF 按有无 alpha 出 RGBA / RGBX）；动画判定随解码完成、字节随结果返回。
      `Browser` 暂仍按原图请求（Fit 目标为空），由下面的 Browser / App 层任务切换
      - 验收 ✅：大 JPEG 按目标解码（含 EXIF 旋转）不超过目标且 `originalSize` 正确；输出格式断言
        （alpha / 不透明各有样本）；RAW 小目标与直接解内嵌预览逐像素一致、预览不够大时走全流程；
        HEIF 小目标解内嵌缩略图；动画条目 `animation` 非空、静态为空。新增样本：`sample.crw`
        （raw.pixls.us CC0，带 1440×960 预览）、`sample-thumb.heic`（heif-enc）、`sample-opaque.avif`（sips）
      - 实测（Debug）：CRW 走预览约 4ms / 张，全流程约 1s；无预览的 DNG 开 `half_size` 后约快 5 倍
- [x] **`DecodeScheduler` 取代 `Prefetcher`**（§4.4）：自有优先级队列（P0 当前页 > P1 翻页方向
      3 页 > P2 反方向 1 页 > P3 缩略图，方向取最近一次翻页）；按 `(EntryKey, 档)` 在途去重
      （排队中提优先级、执行中等其结果）；导航后丢弃窗口外的排队任务、换源丢弃旧来源任务；
      `EntryKey` = (来源 id, 列表下标)，来源 id 每个 `ImageSource` 实例唯一；线程数 2、栈 8MB 保留；
      不环绕预读。结果先写缓存、再排队回 UI 线程发 `ready` / `failed`，送达前在 UI 线程按来源 id /
      Fit 目标版本再校验；执行中的任务按票号登记，换源 / 改 Fit 目标即作废，迟到结果不入缓存。
      `Prefetcher` 已删除；`Browser` 处于过渡态：当前页未命中仍同步解码，预读走调度器
      - 验收 ✅（`test/support/FakeSource`：可阻塞、记录读取次数与先后）：P0 插到排队的 P1/P2/P3 前面；
        同一 key 排队 / 执行中重复请求都只读一次、只送达一次；`focus` 丢弃窗口外的排队任务（执行中的
        跑完入缓存，P3 不受影响）；换源后旧来源结果不入缓存、不送达；改 Fit 目标后在途 Fit 另起任务；
        RAW 工作线程栈回归测试迁移保留（去掉 8MB 栈即 SIGBUS，已验证）
- [x] **`ImageCache` 按档分预算**（§4.4）：键 `(EntryKey, 档)`，值为 `DecodedImage`（动画字节计入占用）；
      Fit 预算至少容纳整个预读窗口（`setFitTarget` 时取构造预算 256MB 与「窗口张数 × Fit 目标像素 × 4B」
      的较大值）；Full 只留一张、不设字节上限；Thumb 独立 32MB；代次机制随之删除（正确性靠键里的来源 id）
      - 验收 ✅：插入 Full 不挤掉 Fit、刷 Thumb 不挤掉 Fit；5K 屏 Fit 目标下预算容纳完整预读窗口（5 张）
- [x] **`Browser` 异步化**（§4.6）：`QObject` + 信号（`opened` / `openFailed` / `listingProgress` /
      `pageChanged` / `pageReady` / `pageFailed` / `reachedEnd`）+ `setFitTarget` / `requestFull`；
      `open` 后台建列表、成功前保留原来源；命中时同步发 `pageReady`，未命中排队发回，过期结果丢弃；
      `Page` 结构就位但先只有单页布局；末页改为「停住 + `reachedEnd`」，去掉无条件环绕。
      列表在 Browser 自有的单线程里建，不排进调度器（调度器不可抢占，P0 也得等执行中的 RAW 解完）；
      连续 open 只有最后一次生效；`openSource()` 打开已建好的来源（测试注入假来源，M3 相邻来源也走它）；
      同一页的 `pageReady` 只在更清晰时重发（Full 到位、Fit 目标调大）；`listingProgress` 待
      `StreamArchiveSource` 落地后发出。`currentImage()` / `currentAnimation()` 与只为二次读取存在的
      `maybeAnimatedName` / `isAnimatedImage` 已删除，`MainWindow` 改接信号
      - 验收 ✅：`QSignalSpy` / 信号记录器断言 `opened → pageChanged → pageReady` 顺序、`open` 返回时尚无
        结果、末页 / 首页 `reachedEnd`、已翻走的页不送达、缓存命中同步发出、后发的 `open` 胜出；
        **UI 线程守卫**（`FakeSource::forbidReadsOn`，故意在调用线程读一次即失败，已验证）；
        `requestFull` 换原图且原图不大于 Fit 时不重解；Fit 目标调大重解、调小不重发；
        原有单测（打开 / 单文件定位 / 压缩包 / 失败保留原来源 / 坏图 / 动画）迁移到新接口
- [x] **App 层切到异步接口**（§4.3）：未就绪时保留上一页画面；场景坐标 = 原图像素，Fit → Full
      只换 pixmap；放大超过 Fit 分辨率时 `requestFull`；状态栏用 `originalSize`；动画字节取自
      `DecodedImage`；按屏幕物理像素调 `setFitTarget`（换屏时更新）。`ImageView` 在显示图像素被拉伸
      （视图缩放 × 图元放大 × 设备像素比 > 1）时发 `needsSharperImage`，每张图一次；动画帧本身是原图
      尺寸，不发。状态栏显示原图尺寸与相对原图的缩放%；当前页解码失败时清空画面。图元改用
      `SmoothTransformation`（默认 Fast 会盖过视图的平滑提示）。`MainWindow` / `ImageView` 抽成
      `viewer-ui` 静态库，供离屏测试链接
      - 验收 ✅：`test_main_window`（离屏，真实 MainWindow + Browser）——6 张无预览 DNG 连续翻页，每次
        `next()` 立即返回（实测 0ms；故意在 UI 线程解码时 526ms、测试失败，已验证）；3000×2000 图先出
        Fit 档，放到 100% 后换成原图且缩放不动。`test_image_view`：场景 = 原图矩形、换图不动变换与中心、
        拉伸判定只发一次、原图 / 动画不发。**待真机手测**：24MP 照片在 Retina 屏上放大后的清晰度
- [x] **压缩包 · 按实际格式分派 + `ZipArchiveSource`**（§4.5）：`openArchive()` 用 libarchive 读第一个
      header 后按 `archive_filter_code()` / `archive_format()` 分派，不信后缀：无压缩过滤器且格式为 zip /
      tar → `ZipArchiveSource`，其余（7z / rar / 压缩 tar）暂走旧的单游标 `ArchiveSource`，由下一条替换。
      `ZipArchiveSource` 每次 `readEntry` 独立打开、按包内序号 `next_header` 前进到目标再读——seekable zip
      读取器下前进只是 seek（`read_data_skip` 为空操作，条目按本地头偏移排序，重开后序号稳定），未压缩
      tar 的跳过走 lseek；无共享游标、无锁。libarchive 句柄封装为 `ArchiveReader`（RAII，出错即作废；
      读 header 的 `ARCHIVE_WARN` 视为成功），列表逻辑 `listArchiveImages()` 与旧类共用
      - 验收 ✅：两线程 + 主线程按三种不同顺序并发读全部 24 个条目都正确；`.cbr` 里装 zip → 随机读路径、
        `.zip` 里装 7z → 流式路径、空包也能判定、垃圾文件 → `Unreadable` 且 `openError` 非空；未压缩 tar
        可随机读；7z / tar.gz 由旧类读取（含回退重开）正确。**千页 cbz 实测**（1000 × 20KB stored，Debug，
        Apple Silicon）：建列表 7.3ms，随机读（含最后一页，要前进过 999 个条目头）最大 5.4ms、平均 3.9ms，
        在 10ms 阈值内，不换 libzip；测试留 200ms 宽松上限只拦数量级退化
- [x] **压缩包 · `StreamArchiveSource` + `ByteStore`**（§4.5）：构造时打开文件、启动抽取线程即返回；线程按
      包内顺序走一遍，图片条目的编码字节进 `ByteStore`（内存预算默认 512MB，超出的条目逐个写入
      `QTemporaryDir`，来源销毁即删）；全程不重开、每个条目只解压一次。这些格式拿到列表本就要解压到底，
      所以列表与抽取是同一遍、走完才排序——新增 `ImageSource::waitForListing(progress)`：`Browser` 的后台
      线程在其中等列表完成，进度去重后转发为 `listingProgress`（状态栏显示「已扫描 N 个条目」）；本次
      `open` 被后来的 `open` 取代（或 `Browser` 析构）时 progress 返回 false 放弃等待，来源销毁即取消抽取
      （按条目检查，最多多解一条）。`readEntry` 等列表完成后从 `ByteStore` 取。`openArchive()` 现在只在
      Zip / Stream 之间分派，打不开的文件也走 Stream（列表即刻完成，`openError` 照报）
      - 验收 ✅：7z / tar.gz 任意顺序读全部正确，**删掉包文件后仍能读到每一条**（证明只抽一遍、不重开）；
        进度只增不减、最后一次是最终计数；放弃等待立即返回且销毁不等整包抽完；预算只够两条时其余三条落盘、
        内存 / 落盘两条路读取都对、销毁后临时目录消失，预算 0 时全部落盘；同名条目各读各的；截断 tar.gz
        保留完整的前缀、不算打开失败；垃圾文件 `openError` 非空且列表即刻完成；`Browser` 打开 7z 时
        `listingProgress` 至少一次、最后一次等于条目数，然后才 `opened → pageChanged → pageReady`
      - [ ] 真实 RAR 小样本：macOS 本机没有 rar / WinRAR，libarchive 自带的 100 个 RAR 测试包都不含图片条目，
        不要再在 macOS 上找工具；等在 Windows 上用 WinRAR 构造 `test/data/sample.rar`（含 2–3 张小 PNG，
        建议同时出一个 solid 包）后，在 `test_stream_archive_source` 加用例
- [x] **清理与文档**：单游标 + 锁的 `ArchiveSource` 类已删除，`ArchiveSource.h` 只剩 `openArchive()` 入口；
      libarchive 调用集中在 `ArchiveReader`；回归点迁移：同名 entry（Zip / Stream 各一）、坏包报错
      （Zip / Stream / 工厂各一）、截断包可浏览（stored zip 截断后中央目录丢失仍能读前缀；tar.gz 截断保留
      完整前缀——gzip 过滤器按 64KB 输出块解压，截断所在的块整块作废）；`CLAUDE.md` / DESIGN「现状」已同步
- 验收（里程碑）：viewer-core 在调用线程零读取 / 解码（守卫测试）✅；打开大 solid 包不冻结界面 ✅（后台抽取
  + 进度）；流式格式顺序翻 + 回翻每个条目只解压一次 ✅；连续翻页命中 <50ms（`test_main_window` 的
  `next()` 实测 0ms，缓存命中路径）✅

## M3 · UI / 交互 ⬅ 进行中

> 依赖 M2.5：以下功能都建在异步 `Browser` + 调度器上（DESIGN §4.6、§4.7）。

- [x] **全屏模式**：「视图 ▸ 全屏」（`Enter`，或系统全屏键：macOS `⌃⌘F`、Windows `F11`）与双击画面切换，
      `Esc` 退出（只在全屏时生效，平时不吞 Esc）；按位翻转 `WindowFullScreen`，退出后回到进入前的最大化状态。
      `changeEvent` 按窗口实际状态显隐菜单栏 / 状态栏，所以 macOS 绿灯按钮进出全屏同样生效（macOS 的原生菜单栏
      不在窗口里，由系统在全屏时隐藏）；菜单动作同时挂到窗口上，菜单栏隐藏后快捷键照常可用。全屏背景纯黑，
      光标静止 1.5s 后隐藏、一动即恢复（按住拖拽时不隐藏）。Fit 目标按屏幕定，进出全屏不重解
      - 验收 ✅：`test_main_window`（离屏，窗口内菜单栏）——`Enter` 进全屏、菜单栏 / 状态栏隐藏、`→` 仍能翻页、
        `Esc` 退出并恢复、非全屏时 `Esc` 无作用；最大化窗口双击进出全屏后仍是最大化。`test_image_view`——全屏背景
        纯黑；光标静止后隐藏、移动即恢复、退出全屏后不再隐藏；只有左键双击发 `doubleClicked`。以上断言都做过变异
        验证（去掉动作挂窗口、改用 `showFullScreen` / `showNormal`、Esc 常开、视口不跟踪鼠标、不分左右键，测试均失败）。
        **待真机手测**：macOS 原生全屏（绿灯按钮、独立 Space 的过渡动画）下的显隐与光标隐藏
- [ ] 幻灯片（间隔可配；下一张即 P1 预读）
- [ ] 缩略图 / 胶片条（调度器 P3 + Thumb 档，只为可见格子请求）
- [ ] 状态栏（原图尺寸 / 序号 / 缩放%）+ 信息叠层（全屏时状态栏隐藏，序号、末页提示、加载 / 错误信息
      目前都看不到，要靠叠层呈现）
- [ ] 双页（跨页）+ 右到左阅读：`PageLayout` 相对当前位置配对、横图单独成页、封面单独、「错开一页」
- [ ] 末页行为可配（停住 / 环绕 / 下一个来源；默认提示后再翻进入下一个）+ 相邻来源导航
      （同目录按自然排序的下一个文件夹 / 压缩包，跳过没有图片的）
- [ ] 会话记忆（`QSettings`：窗口几何、最近打开、上次目录、每个来源的阅读位置与单双页 / 方向）
- [ ] 深 / 浅主题，可配置快捷键
- [ ] i18n（中 / 英）

## M4 · 健壮性与大图

> 原「大压缩包异步扫描」「当前页未命中时异步解码 + 加载占位」两条已并入 M2.5。

- [ ] 错误处理：损坏图片 / 空压缩包 / 超大图 / 无权限 的友好提示（底层通道
      `openError()` 与 M2.5 的 `openFailed` / `pageFailed` 信号，补 UI 呈现与更多场景）
- [ ] 超大图（gigapixel）分块、按可视区域解码，避免 OOM——作为 Full 档的替代挂在调度器请求上
- [ ] 按系统可用内存自适应缓存预算（M2.5 已按屏幕尺寸定 Fit 预算下限）
- [ ] 压缩包：非 UTF-8 包名编码探测；加密包密码输入
- [ ] 回归样本集 + 对应测试；offscreen 端到端集成测试（open → navigate；`test_main_window` 已有
      文件夹翻页与放大换原图的冒烟，待补压缩包、坏图、换屏等场景）

## M5 · 跨平台

- [ ] Windows 构建（vcpkg x64-windows，aqt Qt，windeployqt 打包）+ CI windows-latest
- [ ] Linux 构建（x64-linux，AppImage）+ CI ubuntu-latest
- [ ] CI 改为三平台矩阵跑 `ctest`
- [ ] 确认 MSVC / GCC 对项目实际用到的 C++23 特性（`std::expected` 等）的支持

## M6 · 打磨与发布

- [ ] 选定开源协议并加 `LICENSE`：静态链接了 libde265（LGPL-3.0）、LibRaw（LGPL-2.1 / CDDL），
      协议需与之兼容（宜早做，可随手完成）
- [ ] 应用图标 / 关于页
- [ ] 版本号与 CHANGELOG
- [ ] Release workflow：打 tag 自动产出三平台产物
- [ ] 用户使用文档
