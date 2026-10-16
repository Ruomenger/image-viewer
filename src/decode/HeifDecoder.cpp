#include "decode/HeifDecoder.h"

#include "decode/FtypBox.h"

#include <libheif/heif.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace {
// ftyp brand 属于 HEIF 家族(HEVC 编码静态图或其结构性 brand)即交给
// libheif。注意 mif1/msf1 也可能出现在 AVIF 文件里:注册表把 AVIF 探测
// 排在 HEIF 之前消歧。
constexpr const char* kHeifBrands[] = {
    "heic", "heix", "hevc", "hevx", "heim", "heis", "mif1", "msf1",
};

using HandlePtr = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>;

int longEdge(const heif_image_handle* handle) {
    return std::max(heif_image_handle_get_width(handle), heif_image_handle_get_height(handle));
}

// 长边不小于 want 的内嵌缩略图里最小的一张;没有则为空。
HandlePtr pickThumbnail(const heif_image_handle* primary, QSize want) {
    HandlePtr best(nullptr, &heif_image_handle_release);
    const int count = heif_image_handle_get_number_of_thumbnails(primary);
    if (count <= 0)
        return best;

    std::vector<heif_item_id> ids(static_cast<size_t>(count));
    const int listed = heif_image_handle_get_list_of_thumbnail_IDs(primary, ids.data(), count);
    ids.resize(static_cast<size_t>(std::clamp(listed, 0, count)));

    const int wantEdge = std::max(want.width(), want.height());
    for (const heif_item_id id : ids) {
        heif_image_handle* rawThumb = nullptr;
        if (heif_image_handle_get_thumbnail(primary, id, &rawThumb).code != heif_error_Ok)
            continue;
        HandlePtr thumb(rawThumb, &heif_image_handle_release);
        const int edge = longEdge(thumb.get());
        if (edge >= wantEdge && (!best || edge < longEdge(best.get())))
            best = std::move(thumb);
    }
    return best;
}

QImage decodeHandle(heif_image_handle* handle) {
    heif_image* rawImage = nullptr;
    const heif_error err = heif_decode_image(handle, &rawImage, heif_colorspace_RGB,
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

    // 无 alpha 时 libheif 把第 4 字节填 0xFF,标成 RGBX 让后续按不透明图处理。
    const QImage::Format format = heif_image_handle_has_alpha_channel(handle) != 0
                                      ? QImage::Format_RGBA8888
                                      : QImage::Format_RGBX8888;
    // libheif 的行跨度可能大于 width*4,逐行拷贝。
    QImage result(width, height, format);
    for (int y = 0; y < height; ++y)
        std::memcpy(result.scanLine(y), plane + (static_cast<qsizetype>(y) * stride),
                    static_cast<size_t>(width) * 4);
    return result;
}
}  // namespace

bool looksLikeHeif(const QByteArray& bytes) {
    return std::ranges::any_of(kHeifBrands, [&bytes](const char* candidate) {
        return ftypContainsBrand(bytes, candidate);
    });
}

DecodedImage decodeHeif(const QByteArray& bytes, QSize target) {
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
    const HandlePtr primary(rawHandle, &heif_image_handle_release);
    const QSize original(heif_image_handle_get_width(primary.get()),
                         heif_image_handle_get_height(primary.get()));

    if (!target.isEmpty()) {
        // 缩略图解坏了不要紧,下面接着解主图。
        const HandlePtr thumb = pickThumbnail(primary.get(), fitWithin(original, target));
        QImage image = thumb ? decodeHandle(thumb.get()) : QImage();
        if (!image.isNull())
            return {.image = std::move(image), .originalSize = original};
    }
    return {.image = decodeHandle(primary.get()), .originalSize = original};
}
