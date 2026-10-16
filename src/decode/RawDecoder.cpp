#include "decode/RawDecoder.h"

#include <QSet>
#include <QString>

#include <algorithm>
#include <cstring>
#include <memory>

#include <libraw/libraw.h>

namespace {
// 常见相机 RAW 后缀(与 LibRaw 支持范围对齐的主流子集)。
const QSet<QString>& rawSuffixes() {
    static const QSet<QString> kSuffixes = {
        QStringLiteral("dng"), QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"),
        QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("arw"), QStringLiteral("srf"),
        QStringLiteral("sr2"), QStringLiteral("orf"), QStringLiteral("rw2"), QStringLiteral("raf"),
        QStringLiteral("pef"), QStringLiteral("srw"), QStringLiteral("x3f"), QStringLiteral("mrw"),
        QStringLiteral("kdc"), QStringLiteral("dcr"), QStringLiteral("erf"), QStringLiteral("3fr"),
        QStringLiteral("fff"), QStringLiteral("iiq"), QStringLiteral("rwl"), QStringLiteral("mos"),
        QStringLiteral("mdc"),
    };
    return kSuffixes;
}

using MemImage = std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)>;

int longEdge(QSize size) {
    return std::max(size.width(), size.height());
}

// 全流程输出的尺寸(计入像素宽高比与相机方向),open 之后即可取,不必真跑。
QSize processedSize(const LibRaw& raw) {
    int width = 0;
    int height = 0;
    int colors = 0;
    int bps = 0;
    raw.get_mem_image_format(&width, &height, &colors, &bps);
    return {width, height};
}

// 内嵌 JPEG 预览长边不小于 want 时解出并缩到 target,否则返回空结果。
// 预览没有自带 EXIF 时,dcraw_make_mem_thumb 会按 sizes.flip 补一段方向标记,
// 交给 decodeImage 的 QImageReader 路径即可摆正方向、走 DCT 缩放。
// 位图缩略图多为 160x120 级别,不考虑。
DecodedImage decodePreview(LibRaw& raw, QSize want, QSize target) {
    const libraw_thumbnail_t& thumb = raw.imgdata.thumbnail;
    if (longEdge(QSize(thumb.twidth, thumb.theight)) < longEdge(want))
        return {};
    if (raw.unpack_thumb() != LIBRAW_SUCCESS || thumb.tformat != LIBRAW_THUMBNAIL_JPEG)
        return {};

    int error = 0;
    const MemImage preview(raw.dcraw_make_mem_thumb(&error), &LibRaw::dcraw_clear_mem);
    if (!preview || preview->type != LIBRAW_IMAGE_JPEG)
        return {};
    const QByteArray jpeg(reinterpret_cast<const char*>(preview->data),
                          static_cast<qsizetype>(preview->data_size));
    return decodeImage(jpeg, QString(), target);
}

QImage toQImage(const libraw_processed_image_t& processed) {
    const int width = processed.width;  // libraw 用 ushort,先收窄成 int
    const int height = processed.height;
    if (processed.type != LIBRAW_IMAGE_BITMAP || processed.colors != 3 || processed.bits != 8 ||
        width <= 0 || height <= 0)
        return {};

    QImage image(width, height, QImage::Format_RGB888);
    // LibRaw 输出行间无填充;QImage 行按 4 字节对齐,逐行拷贝。
    const qsizetype rowBytes = qsizetype{width} * 3;
    for (int y = 0; y < height; ++y)
        std::memcpy(image.scanLine(y), processed.data + (y * rowBytes),
                    static_cast<size_t>(rowBytes));
    return image;
}
}  // namespace

bool looksLikeRaw(const QByteArray& bytes, const QString& suffix) {
    return !bytes.isEmpty() && rawSuffixes().contains(suffix);
}

DecodedImage decodeRaw(const QByteArray& bytes, QSize target) {
    LibRaw raw;
    if (raw.open_buffer(bytes.constData(), static_cast<size_t>(bytes.size())) != LIBRAW_SUCCESS)
        return {};
    const QSize original = processedSize(raw);

    if (!target.isEmpty()) {
        const QSize want = fitWithin(original, target);
        DecodedImage preview = decodePreview(raw, want, target);
        if (!preview.image.isNull()) {
            preview.originalSize = original;  // 预览的尺寸不代表原图
            return preview;
        }
        // 只在半尺寸仍覆盖目标时开:否则 Fit 档在大屏上会比屏幕还糊。
        if (longEdge(original) / 2 >= longEdge(want))
            raw.imgdata.params.half_size = 1;
    }

    if (raw.unpack() != LIBRAW_SUCCESS)
        return {};
    if (raw.dcraw_process() != LIBRAW_SUCCESS)  // 去马赛克 + 白平衡 + 相机方向翻转
        return {};

    int error = 0;
    const MemImage processed(raw.dcraw_make_mem_image(&error), &LibRaw::dcraw_clear_mem);
    if (!processed)
        return {};
    return {.image = toQImage(*processed), .originalSize = original};
}
