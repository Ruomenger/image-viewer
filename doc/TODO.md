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

- [x] 解码结果 **LRU 缓存**（按字节预算，上限可配）— `ImageCache`，已接入 `MainWindow`
- [ ] **邻页异步预读**（`QThreadPool`/`QtConcurrent`），快速翻页时取消过期任务
- [ ] Archive **顺序读快路径**：避免每次 `readEntry` O(n) 重开扫描，建立 entry 索引
- [ ] EXIF 方向自动旋转
- [ ] 单测：缓存淘汰、预读命中、Archive 顺序读正确性
- 验收：连续翻页响应 <50ms（缓存命中）；`ArchiveSource` 读 N 张的耗时由 O(n²) 降到 ~O(n)

## M2 · 现代格式支持

- [ ] vcpkg 引入 **libheif**（HEIC/HEIF）
- [ ] **libavif**（AVIF）
- [ ] **libraw**（RAW：CR2/NEF/ARW…）
- [ ] （可选）**libjxl**（JXL）
- [ ] `ImageDecoder` 注册表：后缀 + magic bytes 选解码器，兜底回退 `QImageReader`
- [ ] 动画（GIF/WebP/APNG）播放
- [ ] 单测：各格式样本解码、解码器选择逻辑

## M3 · UI / 交互

- [ ] 全屏模式
- [ ] 幻灯片（间隔可配）
- [ ] 缩略图 / 胶片条
- [ ] 状态栏（尺寸 / 序号 / 缩放%）+ 信息叠层
- [ ] 会话记忆（`QSettings`：窗口几何、最近打开、上次目录）
- [ ] 深 / 浅主题，可配置快捷键
- [ ] i18n（中 / 英）

## M4 · 健壮性与大图

- [ ] 错误处理：损坏图片 / 空压缩包 / 超大图 / 无权限 的友好提示
- [ ] 超大图（gigapixel）分块、按可视区域解码，避免 OOM
- [ ] 内存预算与缓存自适应
- [ ] 压缩包：非 UTF-8 包名编码探测；加密包密码输入
- [ ] 回归样本集 + 对应测试

## M5 · 跨平台

- [ ] Windows 构建（vcpkg x64-windows，aqt Qt，windeployqt 打包）+ CI windows-latest
- [ ] Linux 构建（x64-linux，AppImage）+ CI ubuntu-latest
- [ ] CI 改为三平台矩阵跑 `ctest`

## M6 · 打磨与发布

- [ ] 应用图标 / 关于页
- [ ] 版本号与 CHANGELOG
- [ ] Release workflow：打 tag 自动产出三平台产物
- [ ] 用户使用文档
