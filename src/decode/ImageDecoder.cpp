#include "decode/ImageDecoder.h"

#include "decode/AvifDecoder.h"
#include "decode/HeifDecoder.h"
#include "decode/RawDecoder.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

namespace {

// 注册表条目:probe 按 magic bytes(和小写后缀)判断是否交给 decode。
struct Decoder {
    bool (*probe)(const QByteArray&, const QString& suffix);
    QImage (*decode)(const QByteArray&);
    const char* extensions;  // 逗号分隔的小写后缀,汇总进 decodableExtensions()
};

bool probeAvif(const QByteArray& bytes, const QString& /*suffix*/) {
    return looksLikeAvif(bytes);
}

bool probeHeif(const QByteArray& bytes, const QString& /*suffix*/) {
    return looksLikeHeif(bytes);
}

constexpr Decoder kDecoders[] = {
    // AVIF 在前:mif1 主 brand 的 AVIF 也会命中 HEIF 的结构性 brand。
    {.probe = probeAvif, .decode = decodeAvif, .extensions = "avif"},
    {.probe = probeHeif, .decode = decodeHeif, .extensions = "heic,heif,hif"},
    {.probe = looksLikeRaw,
     .decode = decodeRaw,
     .extensions = "dng,cr2,cr3,crw,nef,nrw,arw,srf,sr2,orf,rw2,raf,pef,srw,x3f,mrw,kdc,dcr,erf,"
                   "3fr,fff,iiq,rwl,mos,mdc"},
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

QImage decodeImage(const QByteArray& bytes, const QString& nameHint) {
    if (bytes.isEmpty())
        return {};

    const QString suffix = QFileInfo(nameHint).suffix().toLower();
    for (const Decoder& decoder : kDecoders) {
        if (!decoder.probe(bytes, suffix))
            continue;
        QImage image = decoder.decode(bytes);
        if (!image.isNull())
            return image;
        // 专用解码器失败:不放弃,继续走 QImageReader 兜底。
    }
    return decodeWithQt(bytes);
}

bool maybeAnimatedName(const QString& name) {
    static const QSet<QString> kAnimatable = {
        QStringLiteral("gif"),
        QStringLiteral("webp"),
        QStringLiteral("png"),  // APNG 同后缀
        QStringLiteral("apng"),
    };
    return kAnimatable.contains(QFileInfo(name).suffix().toLower());
}

bool isAnimatedImage(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return false;

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;

    QImageReader reader(&buffer);
    return reader.supportsAnimation() && reader.imageCount() > 1;
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
