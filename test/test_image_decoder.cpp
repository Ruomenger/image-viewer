#include <QtTest>

#include <QBuffer>
#include <QImage>
#include <QImageIOHandler>
#include <QImageWriter>

#include "decode/ImageDecoder.h"
#include "support/TestData.h"

class TestImageDecoder : public QObject {
    Q_OBJECT
private slots:
    void decodesPng();
    void appliesExifOrientation();
    void rejectsGarbage();
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

QTEST_GUILESS_MAIN(TestImageDecoder)
#include "test_image_decoder.moc"
