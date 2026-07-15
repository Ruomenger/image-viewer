#include "decode/ImageDecoder.h"

#include <QBuffer>
#include <QImageReader>

QImage decodeImage(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return {};

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);  // 按 EXIF 方向自动旋转
    return reader.read();
}
