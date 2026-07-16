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

## M1 · 浏览体验核心 ⬅ 下一个

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

## M2 · 现代格式支持

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

## M3 · UI / 交互

- [ ] 全屏模式
- [ ] 幻灯片（间隔可配）
- [ ] 缩略图 / 胶片条
- [ ] 状态栏（尺寸 / 序号 / 缩放%）+ 信息叠层
- [ ] 会话记忆（`QSettings`：窗口几何、最近打开、上次目录）
- [ ] 深 / 浅主题，可配置快捷键
- [ ] i18n（中 / 英）

## M4 · 健壮性与大图

- [ ] 错误处理：损坏图片 / 空压缩包 / 超大图 / 无权限 的友好提示（底层通道
      `openError()` 已就绪，补 UI 呈现与更多场景）
- [ ] 大压缩包**异步扫描**：`ArchiveSource` 构造时全量扫包在 UI 线程，
      大 solid 7z 打开会冻结界面 → 列表异步化或加进度提示
- [ ] 当前页缓存未命中时**异步解码 + 加载占位**：快速翻页越过预读半径时
      UI 线程不做同步 readEntry/解码
- [ ] 超大图（gigapixel）分块、按可视区域解码，避免 OOM
- [ ] 内存预算与缓存自适应
- [ ] 压缩包：非 UTF-8 包名编码探测；加密包密码输入
- [ ] 回归样本集 + 对应测试；offscreen 端到端集成测试（open → navigate）

## M5 · 跨平台

- [ ] Windows 构建（vcpkg x64-windows，aqt Qt，windeployqt 打包）+ CI windows-latest
- [ ] Linux 构建（x64-linux，AppImage）+ CI ubuntu-latest
- [ ] CI 改为三平台矩阵跑 `ctest`

## M6 · 打磨与发布

- [ ] 应用图标 / 关于页
- [ ] 版本号与 CHANGELOG
- [ ] Release workflow：打 tag 自动产出三平台产物
- [ ] 用户使用文档
