#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageIOHandler>
#include <QImageWriter>

#include <memory>

#include <libraw/libraw.h>

#include "decode/AvifDecoder.h"
#include "decode/HeifDecoder.h"
#include "decode/ImageDecoder.h"
#include "decode/RawDecoder.h"
#include "support/TestData.h"

namespace {
// test/data/ 下的样本均为真实编码器产物,非手工构造字节:
// - sample.heic(320x240)/ sample.avif(32x24):macOS `sips -s format heic|avif`
//   从生成的 RGBA PNG 转出,带 alpha;sample-opaque.avif(32x24)同法从不透明 PNG 转出;
// - sample-thumb.heic(320x240,内嵌 64x48 缩略图):`heif-enc -t 64` 编码;
// - sample.dng / sample.crw:raw.pixls.us 的 CC0 样本,见各自用例;
// - sample-anim.{gif,webp}:ImageMagick 生成的 2 帧动画。
QByteArray readSample(const QString& name) {
    QFile file(QFINDTESTDATA(QStringLiteral("data/") + name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray readSampleHeic() {
    return readSample(QStringLiteral("sample.heic"));
}

QByteArray readSampleAvif() {
    return readSample(QStringLiteral("sample.avif"));
}

// width×height 的白色 JPEG;rotate90 时写入「旋转 90°」的 EXIF 方向标记。
QByteArray makeJpeg(int width, int height, bool rotate90 = false) {
    QImage source(width, height, QImage::Format_RGB32);
    source.fill(Qt::white);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    if (rotate90)
        writer.setTransformation(QImageIOHandler::TransformationRotate90);
    writer.write(source);
    return bytes;
}

// 绕开被测代码、直接用 LibRaw 取出的内嵌 JPEG 预览,作「走了预览路径」的比对基准。
QByteArray embeddedJpegPreview(const QByteArray& raw) {
    LibRaw libraw;
    if (libraw.open_buffer(raw.constData(), static_cast<size_t>(raw.size())) != LIBRAW_SUCCESS ||
        libraw.unpack_thumb() != LIBRAW_SUCCESS)
        return {};
    int error = 0;
    const std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> preview(
        libraw.dcraw_make_mem_thumb(&error), &LibRaw::dcraw_clear_mem);
    if (!preview || preview->type != LIBRAW_IMAGE_JPEG)
        return {};
    return {reinterpret_cast<const char*>(preview->data),
            static_cast<qsizetype>(preview->data_size)};
}

bool hasJpegSupport() {
    return QImageWriter::supportedImageFormats().contains("jpeg");
}
}  // namespace

class TestImageDecoder : public QObject {
    Q_OBJECT
private slots:
    void decodesPng();
    void appliesExifOrientation();
    void rejectsGarbage();
    void decodesHeic();
    void decodesAvif();
    void decodesRawDng();
    void fitWithinKeepsAspectAndNeverUpscales();
    void decodesJpegToTarget();
    void targetFollowsExifOrientation();
    void returnsOriginalWhenNotLargerThanTarget();
    void scalesRegistryFormatsToTarget();
    void outputsDisplayFormats();
    void heifSmallTargetUsesEmbeddedThumbnail();
    void rawSmallTargetUsesEmbeddedPreview();
    void rawProcessesWhenPreviewTooSmall();
    void returnsAnimationBytes();
    void heifProbeSelectsByMagicBytes();
    void avifProbeSelectsByMagicBytes();
    void rawProbeSelectsBySuffix();
    void extensionsIncludeRegistryFormats();
    void detectsAnimation();
};

void TestImageDecoder::decodesPng() {
    const DecodedImage decoded = decodeImage(testdata::makePng(12, 9));
    QVERIFY(!decoded.image.isNull());
    QCOMPARE(decoded.image.size(), QSize(12, 9));
    QCOMPARE(decoded.originalSize, QSize(12, 9));
}

void TestImageDecoder::appliesExifOrientation() {
    if (!hasJpegSupport())
        QSKIP("当前 Qt 没有 jpeg 插件");

    // 4x2 横图带「旋转 90°」标记:autoTransform 应用 EXIF 方向 → 宽高互换。
    const DecodedImage decoded = decodeImage(makeJpeg(4, 2, true));
    QVERIFY(!decoded.image.isNull());
    QCOMPARE(decoded.image.size(), QSize(2, 4));
    QCOMPARE(decoded.originalSize, QSize(2, 4));
}

void TestImageDecoder::rejectsGarbage() {
    QVERIFY(decodeImage({}).image.isNull());
    QVERIFY(decodeImage(QByteArrayLiteral("definitely not an image")).image.isNull());
}

void TestImageDecoder::decodesHeic() {
    const QByteArray bytes = readSampleHeic();
    QVERIFY(!bytes.isEmpty());

    // 直测 libheif 路径:decodeImage 的 QImageReader 兜底在装有 qmacheif
    // 插件的环境(如本地 Homebrew Qt)也能解 HEIC,会掩盖专用解码器故障。
    // 样本用常规尺寸:libheif 1.23 把解码上限收紧到「ispe 尺寸+64」,而
    // Apple 编码器会把过小的图(如 32x24)填充到远超该余量的编码尺寸。
    const DecodedImage viaLibheif = decodeHeif(bytes);
    QVERIFY(!viaLibheif.image.isNull());
    QCOMPARE(viaLibheif.image.size(), QSize(320, 240));
    QCOMPARE(viaLibheif.originalSize, QSize(320, 240));

    const DecodedImage decoded = decodeImage(bytes);  // 经注册表命中 libheif
    QCOMPARE(decoded.image.size(), QSize(320, 240));
}

void TestImageDecoder::decodesAvif() {
    const QByteArray bytes = readSampleAvif();
    QVERIFY(!bytes.isEmpty());

    const QImage viaLibavif = decodeAvif(bytes);  // 直测,不受兜底掩盖
    QVERIFY(!viaLibavif.isNull());
    QCOMPARE(viaLibavif.size(), QSize(32, 24));

    const DecodedImage decoded = decodeImage(bytes);  // 经注册表命中 libavif/dav1d
    QCOMPARE(decoded.image.size(), QSize(32, 24));
    QCOMPARE(decoded.originalSize, QSize(32, 24));
}

void TestImageDecoder::decodesRawDng() {
    // test/data/sample.dng:Canon EOS 5D Mark III,raw.pixls.us 的 CC0 样本
    // (sha256 1d77dcc6…),MLV App 生成,无内嵌预览。
    const QByteArray bytes = readSample(QStringLiteral("sample.dng"));
    QVERIFY(!bytes.isEmpty());

    // 经注册表命中 LibRaw
    const DecodedImage decoded = decodeImage(bytes, QStringLiteral("sample.dng"));
    QVERIFY(!decoded.image.isNull());
    QCOMPARE(decoded.image.size(), QSize(1920, 818));  // 该样本为 5D3 视频裁切模式(2.35:1)
    QCOMPARE(decoded.originalSize, QSize(1920, 818));
}

void TestImageDecoder::fitWithinKeepsAspectAndNeverUpscales() {
    QCOMPARE(fitWithin(QSize(3000, 2000), QSize(800, 800)), QSize(800, 533));
    QCOMPARE(fitWithin(QSize(2000, 3000), QSize(800, 800)), QSize(533, 800));
    QCOMPARE(fitWithin(QSize(100, 50), QSize(800, 800)), QSize(100, 50));    // 不放大
    QCOMPARE(fitWithin(QSize(800, 600), QSize(800, 600)), QSize(800, 600));  // 恰好放得下
    QCOMPARE(fitWithin(QSize(3000, 2000), QSize()), QSize(3000, 2000));      // 空目标 = 原图
    QCOMPARE(fitWithin(QSize(100000, 10), QSize(256, 256)), QSize(256, 1));  // 短边至少 1px
}

void TestImageDecoder::decodesJpegToTarget() {
    if (!hasJpegSupport())
        QSKIP("当前 Qt 没有 jpeg 插件");

    const DecodedImage decoded = decodeImage(makeJpeg(3000, 2000), QString(), QSize(800, 800));
    QCOMPARE(decoded.image.size(), QSize(800, 533));
    QCOMPARE(decoded.originalSize, QSize(3000, 2000));
}

void TestImageDecoder::targetFollowsExifOrientation() {
    if (!hasJpegSupport())
        QSKIP("当前 Qt 没有 jpeg 插件");

    // 存储为 3000x2000 横图、EXIF 标记旋转 90°:原图与目标都按摆正后的竖图算。
    const QByteArray bytes = makeJpeg(3000, 2000, true);
    const DecodedImage decoded = decodeImage(bytes, QString(), QSize(800, 800));
    QCOMPARE(decoded.originalSize, QSize(2000, 3000));
    QCOMPARE(decoded.image.size(), QSize(533, 800));
}

void TestImageDecoder::returnsOriginalWhenNotLargerThanTarget() {
    const DecodedImage small = decodeImage(testdata::makePng(12, 9), QString(), QSize(100, 100));
    QCOMPARE(small.image.size(), QSize(12, 9));
    QCOMPARE(small.originalSize, QSize(12, 9));

    const DecodedImage exact = decodeImage(testdata::makePng(40, 30), QString(), QSize(40, 30));
    QCOMPARE(exact.image.size(), QSize(40, 30));
}

void TestImageDecoder::scalesRegistryFormatsToTarget() {
    const DecodedImage heic = decodeImage(readSampleHeic(), QString(), QSize(100, 100));
    QCOMPARE(heic.image.size(), QSize(100, 75));
    QCOMPARE(heic.originalSize, QSize(320, 240));

    const DecodedImage avif = decodeImage(readSampleAvif(), QString(), QSize(16, 16));
    QCOMPARE(avif.image.size(), QSize(16, 12));
    QCOMPARE(avif.originalSize, QSize(32, 24));

    // 无内嵌预览的 RAW:走全流程(半尺寸 960x409 仍覆盖目标,开 half_size)后缩放。
    const DecodedImage dng = decodeImage(readSample(QStringLiteral("sample.dng")),
                                         QStringLiteral("sample.dng"), QSize(800, 800));
    QCOMPARE(dng.image.size(), QSize(800, 340));
    QCOMPARE(dng.originalSize, QSize(1920, 818));
}

void TestImageDecoder::outputsDisplayFormats() {
    // 有 alpha 通道 → 预乘 ARGB32;不透明 → RGB32,缩放与否都一样。
    const QImage::Format alpha = QImage::Format_ARGB32_Premultiplied;
    const QImage::Format opaque = QImage::Format_RGB32;
    QCOMPARE(decodeImage(testdata::makePng(12, 9)).image.format(), alpha);
    QCOMPARE(decodeImage(testdata::makePng(120, 90), QString(), QSize(12, 12)).image.format(),
             alpha);
    QCOMPARE(decodeImage(readSampleHeic()).image.format(), alpha);  // sips 从 RGBA PNG 转出
    QCOMPARE(decodeImage(readSample(QStringLiteral("sample-thumb.heic"))).image.format(), opaque);
    QCOMPARE(decodeImage(readSampleAvif()).image.format(), alpha);
    QCOMPARE(decodeImage(readSample(QStringLiteral("sample-opaque.avif"))).image.format(), opaque);
    QCOMPARE(decodeImage(readSample(QStringLiteral("sample.dng")), QStringLiteral("sample.dng"),
                         QSize(400, 400))
                 .image.format(),
             opaque);
    if (hasJpegSupport()) {
        QCOMPARE(decodeImage(makeJpeg(64, 48)).image.format(), opaque);
        QCOMPARE(decodeImage(makeJpeg(640, 480), QString(), QSize(64, 64)).image.format(), opaque);
    }
}

void TestImageDecoder::heifSmallTargetUsesEmbeddedThumbnail() {
    const QByteArray bytes = readSample(QStringLiteral("sample-thumb.heic"));
    QVERIFY(!bytes.isEmpty());

    // 目标 48 以内:64x48 的缩略图够用,解它而不解主图(decodeHeif 自身不缩放)。
    const DecodedImage thumb = decodeHeif(bytes, QSize(48, 48));
    QCOMPARE(thumb.image.size(), QSize(64, 48));
    QCOMPARE(thumb.originalSize, QSize(320, 240));

    // 缩略图不够大:回到主图。
    const DecodedImage primary = decodeHeif(bytes, QSize(100, 100));
    QCOMPARE(primary.image.size(), QSize(320, 240));

    const DecodedImage decoded = decodeImage(bytes, QString(), QSize(48, 48));
    QCOMPARE(decoded.image.size(), QSize(48, 36));
    QCOMPARE(decoded.originalSize, QSize(320, 240));
}

void TestImageDecoder::rawSmallTargetUsesEmbeddedPreview() {
    // test/data/sample.crw:Canon EOS D30,raw.pixls.us 的 CC0 样本(sha256 4ee482f2…),
    // 原图 2176x1448,内嵌 1440x960 JPEG 预览。
    const QByteArray bytes = readSample(QStringLiteral("sample.crw"));
    const QByteArray preview = embeddedJpegPreview(bytes);
    QVERIFY(!preview.isEmpty());

    for (const QSize target : {QSize(256, 256), QSize(1024, 1024)}) {
        const DecodedImage decoded = decodeImage(bytes, QStringLiteral("sample.crw"), target);
        QCOMPARE(decoded.originalSize, QSize(2176, 1448));  // 仍报 RAW 原图尺寸
        // 与直接解内嵌预览的结果逐像素一致 → 确实走了预览、没跑全流程。
        QCOMPARE(decoded.image, decodeImage(preview, QString(), target).image);
        QVERIFY(decoded.image.width() <= target.width() &&
                decoded.image.height() <= target.height());
    }
}

void TestImageDecoder::rawProcessesWhenPreviewTooSmall() {
    // 目标长边 2000 超过预览的 1440:走全流程(半尺寸 1088 不够,不开 half_size)后缩放。
    const QByteArray bytes = readSample(QStringLiteral("sample.crw"));
    const QSize target(2000, 2000);
    const DecodedImage decoded = decodeImage(bytes, QStringLiteral("sample.crw"), target);
    QCOMPARE(decoded.originalSize, QSize(2176, 1448));
    QCOMPARE(decoded.image.size(), QSize(2000, 1330));
    QVERIFY(decoded.image != decodeImage(embeddedJpegPreview(bytes), QString(), target).image);
}

void TestImageDecoder::returnsAnimationBytes() {
    const QByteArray gif = readSample(QStringLiteral("sample-anim.gif"));
    const QByteArray webp = readSample(QStringLiteral("sample-anim.webp"));
    QCOMPARE(decodeImage(gif).animation, gif);
    QCOMPARE(decodeImage(webp).animation, webp);
    QVERIFY(!decodeImage(gif).image.isNull());  // 首帧照常解出

    QVERIFY(decodeImage(testdata::makePng(4, 4)).animation.isEmpty());
    QVERIFY(decodeImage(readSampleHeic()).animation.isEmpty());
}

void TestImageDecoder::heifProbeSelectsByMagicBytes() {
    QVERIFY(looksLikeHeif(readSampleHeic()));
    QVERIFY(!looksLikeHeif(testdata::makePng(4, 4)));  // PNG 不该进 heif 解码器
    QVERIFY(!looksLikeHeif(QByteArrayLiteral("short")));
}

void TestImageDecoder::avifProbeSelectsByMagicBytes() {
    QVERIFY(looksLikeAvif(readSampleAvif()));
    QVERIFY(!looksLikeAvif(readSampleHeic()));  // HEIC 不该进 avif 解码器
    QVERIFY(!looksLikeAvif(testdata::makePng(4, 4)));
}

void TestImageDecoder::rawProbeSelectsBySuffix() {
    const QByteArray dng = readSample(QStringLiteral("sample.dng"));
    QVERIFY(looksLikeRaw(dng, QStringLiteral("dng")));
    QVERIFY(!looksLikeRaw(dng, QString()));  // RAW 靠后缀判定,无后缀不进 LibRaw
    QVERIFY(!looksLikeRaw(testdata::makePng(4, 4), QStringLiteral("png")));
}

void TestImageDecoder::extensionsIncludeRegistryFormats() {
    const QSet<QString>& extensions = decodableExtensions();
    QVERIFY(extensions.contains(QStringLiteral("png")));
    QVERIFY(extensions.contains(QStringLiteral("jpg")));
    QVERIFY(extensions.contains(QStringLiteral("heic")));
    QVERIFY(extensions.contains(QStringLiteral("heif")));
    QVERIFY(extensions.contains(QStringLiteral("avif")));
    QVERIFY(extensions.contains(QStringLiteral("dng")));
    QVERIFY(extensions.contains(QStringLiteral("cr2")));
    QVERIFY(extensions.contains(QStringLiteral("nef")));
    QVERIFY(extensions.contains(QStringLiteral("arw")));
}

void TestImageDecoder::detectsAnimation() {
    QVERIFY(isAnimatedImage(readSample(QStringLiteral("sample-anim.gif"))));
    QVERIFY(isAnimatedImage(readSample(QStringLiteral("sample-anim.webp"))));
    QVERIFY(!isAnimatedImage(testdata::makePng(4, 4)));  // 静态图
    QVERIFY(!isAnimatedImage({}));

    QVERIFY(maybeAnimatedName(QStringLiteral("a.gif")));
    QVERIFY(maybeAnimatedName(QStringLiteral("A.WEBP")));
    QVERIFY(!maybeAnimatedName(QStringLiteral("photo.jpg")));
    QVERIFY(!maybeAnimatedName(QStringLiteral("photo.dng")));  // 大文件不做动画重读
}

QTEST_GUILESS_MAIN(TestImageDecoder)
#include "test_image_decoder.moc"
