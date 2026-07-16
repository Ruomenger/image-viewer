#include "decode/HeifDecoder.h"

#include "decode/FtypBox.h"

#include <libheif/heif.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {
// ftyp brand 属于 HEIF 家族(HEVC 编码静态图或其结构性 brand)即交给
// libheif。注意 mif1/msf1 也可能出现在 AVIF 文件里:注册表把 AVIF 探测
// 排在 HEIF 之前消歧。
constexpr const char* kHeifBrands[] = {"heic", "heix", "hevc", "hevx",
                                       "heim", "heis", "mif1", "msf1"};
}  // namespace

bool looksLikeHeif(const QByteArray& bytes) {
    return std::ranges::any_of(kHeifBrands, [&bytes](const char* candidate) {
        return ftypContainsBrand(bytes, candidate);
    });
}

QImage decodeHeif(const QByteArray& bytes) {
    const std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(),
                                                                              &heif_context_free);
    if (!context)
        return {};

    // without_copy:bytes 在本函数期间存活,零拷贝读取。
    heif_error err = heif_context_read_from_memory_without_copy(
        context.get(), bytes.constData(), static_cast<size_t>(bytes.size()), nullptr);
    if (err.code != heif_error_Ok)
        return {};

    heif_image_handle* rawHandle = nullptr;
    err = heif_context_get_primary_image_handle(context.get(), &rawHandle);
    if (err.code != heif_error_Ok)
        return {};
    const std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle(
        rawHandle, &heif_image_handle_release);

    heif_image* rawImage = nullptr;
    err = heif_decode_image(handle.get(), &rawImage, heif_colorspace_RGB,
                            heif_chroma_interleaved_RGBA, nullptr);
    if (err.code != heif_error_Ok)
        return {};
    const std::unique_ptr<heif_image, decltype(&heif_image_release)> image(rawImage,
                                                                           &heif_image_release);

    const int width = heif_image_get_width(image.get(), heif_channel_interleaved);
    const int height = heif_image_get_height(image.get(), heif_channel_interleaved);
    int stride = 0;
    const uint8_t* plane =
        heif_image_get_plane_readonly(image.get(), heif_channel_interleaved, &stride);
    if (plane == nullptr || width <= 0 || height <= 0 || stride < width * 4)
        return {};

    // libheif 的行跨度可能大于 width*4,逐行拷贝。
    QImage result(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        std::memcpy(result.scanLine(y), plane + (static_cast<qsizetype>(y) * stride),
                    static_cast<size_t>(width) * 4);
    return result;
}
