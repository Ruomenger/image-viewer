#include "decode/ImageDecoder.h"

#include "decode/HeifDecoder.h"

#include <QBuffer>
#include <QImageReader>
#include <QStringList>

namespace {

// 注册表条目:probe 用 magic bytes 判断是否交给 decode。
struct Decoder {
    bool (*probe)(const QByteArray&);
    QImage (*decode)(const QByteArray&);
    const char* extensions;  // 逗号分隔的小写后缀,汇总进 decodableExtensions()
};

constexpr Decoder kDecoders[] = {
    {.probe = looksLikeHeif, .decode = decodeHeif, .extensions = "heic,heif,hif"},
};

QImage decodeWithQt(const QByteArray& bytes) {
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);  // 按 EXIF 方向自动旋转
    return reader.read();
}

}  // namespace

QImage decodeImage(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return {};

    for (const Decoder& decoder : kDecoders) {
        if (!decoder.probe(bytes))
            continue;
        QImage image = decoder.decode(bytes);
        if (!image.isNull())
            return image;
        // 专用解码器失败:不放弃,继续走 QImageReader 兜底。
    }
    return decodeWithQt(bytes);
}

const QSet<QString>& decodableExtensions() {
    static const QSet<QString> kExtensions = [] {
        QSet<QString> set;
        for (const QByteArray& fmt : QImageReader::supportedImageFormats())
            set.insert(QString::fromLatin1(fmt).toLower());
        // QImageReader 报 "jpeg",补常见别名。
        set.insert(QStringLiteral("jpg"));
        set.insert(QStringLiteral("jpeg"));
        set.insert(QStringLiteral("jpe"));
        for (const Decoder& decoder : kDecoders) {
            const QStringList parts = QString::fromLatin1(decoder.extensions).split(u',');
            for (const QString& ext : parts)
                set.insert(ext);
        }
        return set;
    }();
    return kExtensions;
}
