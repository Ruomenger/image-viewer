#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageIOHandler>
#include <QImageWriter>

#include "decode/AvifDecoder.h"
#include "decode/HeifDecoder.h"
#include "decode/ImageDecoder.h"
#include "decode/RawDecoder.h"
#include "support/TestData.h"

namespace {
// test/data/ 下的样本均为 32x24,由 macOS `sips -s format heic|avif`
// 从生成的 PNG 转出(真实编码器产物,非手工构造字节)。
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
    void heifProbeSelectsByMagicBytes();
    void avifProbeSelectsByMagicBytes();
    void rawProbeSelectsBySuffix();
    void extensionsIncludeRegistryFormats();
    void detectsAnimation();
};

void TestImageDecoder::decodesPng() {
    const QImage image = decodeImage(testdata::makePng(12, 9));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(12, 9));
}

void TestImageDecoder::appliesExifOrientation() {
    if (!QImageWriter::supportedImageFormats().contains("jpeg"))
        QSKIP("当前 Qt 没有 jpeg 插件");

    // 4x2 横图,写 JPEG 时打上「旋转 90°」的 EXIF 方向标记。
    QImage source(4, 2, QImage::Format_RGB32);
    source.fill(Qt::white);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QImageWriter writer(&buffer, "jpeg");
    writer.setTransformation(QImageIOHandler::TransformationRotate90);
    QVERIFY(writer.write(source));
    buffer.close();

    // autoTransform 应用 EXIF 方向 → 宽高互换。
    const QImage decoded = decodeImage(bytes);
    QVERIFY(!decoded.isNull());
    QCOMPARE(decoded.size(), QSize(2, 4));
}

void TestImageDecoder::rejectsGarbage() {
    QVERIFY(decodeImage({}).isNull());
    QVERIFY(decodeImage(QByteArrayLiteral("definitely not an image")).isNull());
}

void TestImageDecoder::decodesHeic() {
    const QByteArray bytes = readSampleHeic();
    QVERIFY(!bytes.isEmpty());

    // 直测 libheif 路径:decodeImage 的 QImageReader 兜底在装有 qmacheif
    // 插件的环境(如本地 Homebrew Qt)也能解 HEIC,会掩盖专用解码器故障。
    // 样本用常规尺寸:libheif 1.23 把解码上限收紧到「ispe 尺寸+64」,而
    // Apple 编码器会把过小的图(如 32x24)填充到远超该余量的编码尺寸。
    const QImage viaLibheif = decodeHeif(bytes);
    QVERIFY(!viaLibheif.isNull());
    QCOMPARE(viaLibheif.size(), QSize(320, 240));

    const QImage image = decodeImage(bytes);  // 经注册表命中 libheif
    QCOMPARE(image.size(), QSize(320, 240));
}

void TestImageDecoder::decodesAvif() {
    const QByteArray bytes = readSampleAvif();
    QVERIFY(!bytes.isEmpty());

    const QImage viaLibavif = decodeAvif(bytes);  // 直测,不受兜底掩盖
    QVERIFY(!viaLibavif.isNull());
    QCOMPARE(viaLibavif.size(), QSize(32, 24));

    const QImage image = decodeImage(bytes);  // 经注册表命中 libavif/dav1d
    QCOMPARE(image.size(), QSize(32, 24));
}

void TestImageDecoder::decodesRawDng() {
    // test/data/sample.dng:Canon EOS 5D Mark III,raw.pixls.us 的 CC0 样本
    // (sha256 1d77dcc6…)。
    const QByteArray bytes = readSample(QStringLiteral("sample.dng"));
    QVERIFY(!bytes.isEmpty());

    const QImage image = decodeImage(bytes, QStringLiteral("sample.dng"));  // 经注册表命中 LibRaw
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(1920, 818));  // 该样本为 5D3 视频裁切模式(2.35:1)
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
    // test/data/sample-anim.{gif,webp}:ImageMagick 生成的 2 帧动画。
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
