#pragma once

#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QString>

// 字节 → QImage 的唯一解码入口(Browser 与 Prefetcher 共用)。
// 内部是解码器注册表:按 magic bytes 探测选择专用解码器(当前:libheif
// 的 HEIC/HEIF;后续 libavif/libraw 在此追加),不匹配或专用解码失败时
// 兜底回退 QImageReader(自动应用 EXIF 方向)。解码失败返回空 QImage。
QImage decodeImage(const QByteArray& bytes);

// 全部可解码格式的小写文件后缀:QImageReader 插件格式 + 常见别名 +
// 注册表专用解码器的格式。来源层(ImageSource)按它过滤图片文件,
// 保证「能列出的就能解码」由 decode 模块单点负责。
const QSet<QString>& decodableExtensions();
