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
// decode 可按 target 走捷径,返回的图允许大于目标,由 decodeImage 统一收尾。
struct Decoder {
    bool (*probe)(const QByteArray&, const QString& suffix);
    DecodedImage (*decode)(const QByteArray&, QSize target);
    const char* extensions;  // 逗号分隔的小写后缀,汇总进 decodableExtensions()
};

bool probeAvif(const QByteArray& bytes, const QString& /*suffix*/) {
    return looksLikeAvif(bytes);
}

bool probeHeif(const QByteArray& bytes, const QString& /*suffix*/) {
    return looksLikeHeif(bytes);
}

// AVIF 没有按尺寸解码的捷径:解原图,缩放留给 decodeImage。
DecodedImage decodeAvifFull(const QByteArray& bytes, QSize /*target*/) {
    const QImage image = decodeAvif(bytes);
    return {.image = image, .originalSize = image.size()};
}

constexpr Decoder kDecoders[] = {
    // AVIF 在前:mif1 主 brand 的 AVIF 也会命中 HEIF 的结构性 brand。
    {.probe = probeAvif, .decode = decodeAvifFull, .extensions = "avif"},
    {.probe = probeHeif, .decode = decodeHeif, .extensions = "heic,heif,hif"},
    {
        .probe = looksLikeRaw,
        .decode = decodeRaw,
        .extensions = "dng,cr2,cr3,crw,nef,nrw,arw,srf,sr2,orf,rw2,raf,pef,srw,x3f,mrw,kdc,dcr,erf,"
                      "3fr,fff,iiq,rwl,mos,mdc",
    },
};

// QMovie 能播的多帧动画:插件支持动画且帧数 > 1。APNG 取决于 Qt png 插件,通常按静态首帧。
bool readerIsAnimated(QImageReader& reader) {
    return reader.supportsAnimation() && reader.imageCount() > 1;
}

DecodedImage decodeWithQt(const QByteArray& bytes, QSize target) {
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);  // 按 EXIF 方向自动旋转

    DecodedImage result;
    if (readerIsAnimated(reader))  // 字节已在手,顺手判定,省掉上层二次读取
        result.animation = bytes;

    // size() 与 ScaledSize 都在旋转前的存储方向上,带 90° 旋转的图按转置后的目标换算。
    const QSize stored = reader.size();
    const bool transposed =
        reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    // 只交给原生支持缩放解码的插件(JPEG 走 libjpeg 的 DCT 缩放,解码量成倍下降);
    // 其余插件解原图,由 decodeImage 统一平滑缩放。
    if (!target.isEmpty() && stored.isValid() &&
        reader.supportsOption(QImageIOHandler::ScaledSize)) {
        const QSize want = fitWithin(stored, transposed ? target.transposed() : target);
        if (want != stored)
            reader.setScaledSize(want);
    }

    result.image = reader.read();
    if (result.image.isNull())
        return {};
    if (stored.isValid())
        result.originalSize = transposed ? stored.transposed() : stored;
    else
        result.originalSize = result.image.size();
    return result;
}

DecodedImage decodeWithRegistry(const QByteArray& bytes, const QString& suffix, QSize target) {
    for (const Decoder& decoder : kDecoders) {
        if (!decoder.probe(bytes, suffix))
            continue;
        DecodedImage result = decoder.decode(bytes, target);
        if (!result.image.isNull())
            return result;
        // 专用解码器失败:不放弃,继续走 QImageReader 兜底。
    }
    return {};
}

// 收尾:缩到目标尺寸,并在工作线程里转成显示格式,UI 线程 QPixmap::fromImage 时免转换。
void toDisplayImage(QImage& image, QSize target) {
    const QSize want = fitWithin(image.size(), target);
    if (want != image.size())
        image = image.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage::Format format =
        image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (image.format() != format)
        image.convertTo(format);
}

}  // namespace

DecodedImage decodeImage(const QByteArray& bytes, const QString& nameHint, QSize target) {
    if (bytes.isEmpty())
        return {};

    const QString suffix = QFileInfo(nameHint).suffix().toLower();
    DecodedImage result = decodeWithRegistry(bytes, suffix, target);
    if (result.image.isNull())
        result = decodeWithQt(bytes, target);
    if (result.image.isNull())
        return {};

    toDisplayImage(result.image, target);
    return result;
}

QSize fitWithin(QSize size, QSize target) {
    if (target.isEmpty() || size.isEmpty())
        return size;
    if (size.width() <= target.width() && size.height() <= target.height())
        return size;
    // 极端长宽比按比例缩时短边可能截断成 0,至少留 1px。
    return size.scaled(target, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
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
