#pragma once

#include <QByteArray>
#include <QImage>

// 字节 → QImage 的唯一解码入口(MainWindow 与 Prefetcher 共用),
// 自动应用 EXIF 方向。解码失败返回空 QImage。
// M2 计划:扩展为解码器注册表(libheif / libavif / libraw),按
// 后缀 + magic bytes 选择,兜底回落 QImageReader。
QImage decodeImage(const QByteArray& bytes);
