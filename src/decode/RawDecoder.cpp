#include "decode/RawDecoder.h"

#include <QSet>
#include <QString>

#include <cstring>

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
}  // namespace

bool looksLikeRaw(const QByteArray& bytes, const QString& suffix) {
    return !bytes.isEmpty() && rawSuffixes().contains(suffix);
}

QImage decodeRaw(const QByteArray& bytes) {
    LibRaw raw;
    if (raw.open_buffer(bytes.constData(), static_cast<size_t>(bytes.size())) != LIBRAW_SUCCESS)
        return {};
    if (raw.unpack() != LIBRAW_SUCCESS)
        return {};
    if (raw.dcraw_process() != LIBRAW_SUCCESS)  // 去马赛克 + 白平衡 + 相机方向翻转
        return {};

    int error = 0;
    libraw_processed_image_t* processed = raw.dcraw_make_mem_image(&error);
    if (processed == nullptr)
        return {};

    QImage image;
    const int width = processed->width;  // libraw 用 ushort,先收窄成 int
    const int height = processed->height;
    if (processed->type == LIBRAW_IMAGE_BITMAP && processed->colors == 3 && processed->bits == 8 &&
        width > 0 && height > 0) {
        image = QImage(width, height, QImage::Format_RGB888);
        // LibRaw 输出行间无填充;QImage 行按 4 字节对齐,逐行拷贝。
        const qsizetype rowBytes = qsizetype{width} * 3;
        for (int y = 0; y < height; ++y)
            std::memcpy(image.scanLine(y), processed->data + (y * rowBytes),
                        static_cast<size_t>(rowBytes));
    }
    LibRaw::dcraw_clear_mem(processed);
    return image;
}
