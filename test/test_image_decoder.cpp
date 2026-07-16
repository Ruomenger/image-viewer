#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageIOHandler>
#include <QImageWriter>

#include "decode/AvifDecoder.h"
#include "decode/HeifDecoder.h"
#include "decode/ImageDecoder.h"
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
    void heifProbeSelectsByMagicBytes();
    void avifProbeSelectsByMagicBytes();
    void extensionsIncludeRegistryFormats();
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

    const QImage image = decodeImage(bytes);  // 经注册表命中 libheif
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(32, 24));
}

void TestImageDecoder::decodesAvif() {
    const QByteArray bytes = readSampleAvif();
    QVERIFY(!bytes.isEmpty());

    const QImage image = decodeImage(bytes);  // 经注册表命中 libavif/dav1d
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(32, 24));
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

void TestImageDecoder::extensionsIncludeRegistryFormats() {
    const QSet<QString>& extensions = decodableExtensions();
    QVERIFY(extensions.contains(QStringLiteral("png")));
    QVERIFY(extensions.contains(QStringLiteral("jpg")));
    QVERIFY(extensions.contains(QStringLiteral("heic")));
    QVERIFY(extensions.contains(QStringLiteral("heif")));
    QVERIFY(extensions.contains(QStringLiteral("avif")));
}

QTEST_GUILESS_MAIN(TestImageDecoder)
#include "test_image_decoder.moc"
