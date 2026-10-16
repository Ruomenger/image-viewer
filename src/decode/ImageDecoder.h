#pragma once

#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>

// decodeImage() 的结果(DESIGN §4.2)。
struct DecodedImage {
    QImage image;          // 已按目标尺寸缩放;不透明为 RGB32,有 alpha 为 ARGB32_Premultiplied
    QSize originalSize;    // 原图尺寸(已应用方向):状态栏、缩放百分比、双页配对都用它
    QByteArray animation;  // 多帧动画的原始字节(供 QMovie),静态图为空
};

// 字节 → 图的唯一解码入口。内部是解码器注册表:按 magic bytes(必要时辅以
// nameHint 的后缀,如 RAW 家族无统一 magic)探测选择专用解码器,不匹配或
// 专用解码失败时兜底回退 QImageReader(自动应用 EXIF 方向)。
// nameHint 传来源的 entryName 即可,留空则只按 magic bytes 探测。
// target 为空(isEmpty)表示解原图;否则按比例缩到放得进 target,原图不大于
// target 时直接返回原图。JPEG、RAW 内嵌预览、HEIF 内嵌缩略图按目标尺寸走
// 捷径,其余格式解原图后平滑缩放。解码失败时 image 为空。
DecodedImage decodeImage(const QByteArray& bytes, const QString& nameHint = QString(),
                         QSize target = QSize());

// 把 size 按比例缩到放得进 target 的尺寸;不放大,target 为空时原样返回。
QSize fitWithin(QSize size, QSize target);

// 全部可解码格式的小写文件后缀:QImageReader 插件格式 + 常见别名 +
// 注册表专用解码器的格式。来源层(ImageSource)按它过滤图片文件,
// 保证「能列出的就能解码」由 decode 模块单点负责。
const QSet<QString>& decodableExtensions();

// 后缀是否属于可能承载动画的格式(gif/webp/png)。廉价预筛:避免为
// 判定动画而重读静态大图(如几十 MB 的 RAW)的字节。
bool maybeAnimatedName(const QString& name);

// 字节流是否为多帧动画(QImageReader 探测:supportsAnimation 且帧数>1)。
// 注:APNG 取决于 Qt png 插件是否支持,当前通常按静态首帧处理。
bool isAnimatedImage(const QByteArray& bytes);
