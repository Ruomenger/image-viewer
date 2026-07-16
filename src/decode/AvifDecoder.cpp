#include "decode/AvifDecoder.h"

#include "decode/FtypBox.h"

#include <avif/avif.h>

#include <memory>

bool looksLikeAvif(const QByteArray& bytes) {
    return ftypContainsBrand(bytes, "avif") || ftypContainsBrand(bytes, "avis");
}

QImage decodeAvif(const QByteArray& bytes) {
    const std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> decoder(avifDecoderCreate(),
                                                                              &avifDecoderDestroy);
    if (!decoder)
        return {};

    if (avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const uint8_t*>(bytes.constData()),
                               static_cast<size_t>(bytes.size())) != AVIF_RESULT_OK)
        return {};
    if (avifDecoderParse(decoder.get()) != AVIF_RESULT_OK)
        return {};
    if (avifDecoderNextImage(decoder.get()) != AVIF_RESULT_OK)  // 动图取首帧
        return {};

    const int width = static_cast<int>(decoder->image->width);
    const int height = static_cast<int>(decoder->image->height);
    if (width <= 0 || height <= 0)
        return {};

    // 直接解到 QImage 的像素缓冲,省一次中间拷贝。
    // 注:irot/imir 方向变换暂未应用(实拍 AVIF 极少携带,后续按需补)。
    QImage result(width, height, QImage::Format_RGBA8888);
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    rgb.pixels = result.bits();
    rgb.rowBytes = static_cast<uint32_t>(result.bytesPerLine());
    if (avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK)
        return {};
    return result;
}
