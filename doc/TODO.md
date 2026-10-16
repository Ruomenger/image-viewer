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

## M2.5 · 管线异步化 ⬅ 进行中

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
- [ ] **App 层切到异步接口**（§4.3）：未就绪时保留上一页画面；场景坐标 = 原图像素，Fit → Full
      只换 pixmap；放大超过 Fit 分辨率时 `requestFull`；状态栏用 `originalSize`；动画字节取自
      `DecodedImage`；按屏幕物理像素调 `setFitTarget`（换屏时更新）
      - 验收：`QT_QPA_PLATFORM=offscreen` 冒烟——打开文件夹连续翻页不阻塞；手测 24MP 照片放大后
        切到原图清晰度
- [ ] **压缩包 · 按实际格式分派 + `ZipArchiveSource`**（§4.5）：按 `archive_format()` /
      `archive_filter_code()` 分派，不信后缀；zip / cbz / 未压缩 tar 无状态随机读，无共享游标、无全局锁
      - 验收：两线程乱序并发读全部正确；后缀为 `.cbr` 的 zip 走随机读路径；千页 cbz 单次随机读耗时
        实测并记录（超过约 10ms 则换 libzip / minizip-ng）
- [ ] **压缩包 · `StreamArchiveSource` + `ByteStore`**（§4.5）：7z / rar / 压缩 tar 后台顺序抽取一遍，
      编码字节进 `ByteStore`（内存 512MB 内常驻，超出落会话临时目录、关闭即删）；`readEntry` 等目标
      条目抽取到位；需解压才能列表的格式，列表与抽取同一遍并报进度
      - 验收：libarchive 现场写 7z / tar.gz 测试包，任意访问顺序下每个条目只解压一次、重开次数为 0；
        `open` 在抽取完成前返回；超预算落盘、关闭后临时目录被删；RAR 无法现场生成，补一个真实小样本
- [ ] **清理与文档**：删除 `ArchiveSource` 游标 + 锁（`Prefetcher`、`currentImage()` /
      `currentAnimation()` 已在前面的任务中删除）；原有回归点（同名 entry、坏包报错、截断包可浏览）迁移保留；同步
      `CLAUDE.md` 架构说明与 DESIGN 各节「现状」
- 验收（里程碑）：viewer-core 在调用线程零读取 / 解码（守卫测试）；连续翻页命中 <50ms；
  打开大 solid 包不冻结界面；流式格式顺序翻 + 回翻每个条目只解压一次

## M3 · UI / 交互

> 依赖 M2.5：以下功能都建在异步 `Browser` + 调度器上（DESIGN §4.6、§4.7）。

- [ ] 全屏模式
- [ ] 幻灯片（间隔可配；下一张即 P1 预读）
- [ ] 缩略图 / 胶片条（调度器 P3 + Thumb 档，只为可见格子请求）
- [ ] 状态栏（原图尺寸 / 序号 / 缩放%）+ 信息叠层
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
- [ ] 回归样本集 + 对应测试；offscreen 端到端集成测试（open → navigate）

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
