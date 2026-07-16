#include <QtTest>

#include "source/ImageSource.h"

class TestImageSource : public QObject {
    Q_OBJECT
private slots:
    void supportedImageByExtension();
    void archiveByExtension();
};

void TestImageSource::supportedImageByExtension() {
    QVERIFY(ImageSource::isSupportedImage("photo.png"));
    QVERIFY(ImageSource::isSupportedImage("PHOTO.PNG"));  // 大小写不敏感
    QVERIFY(ImageSource::isSupportedImage("a.jpg"));
    QVERIFY(ImageSource::isSupportedImage("a.jpeg"));
    QVERIFY(ImageSource::isSupportedImage("photo.heic"));  // 注册表格式(libheif)
    QVERIFY(ImageSource::isSupportedImage("photo.HEIF"));
    QVERIFY(ImageSource::isSupportedImage("photo.avif"));  // 注册表格式(libavif)
    QVERIFY(ImageSource::isSupportedImage("photo.dng"));   // 注册表格式(LibRaw)
    QVERIFY(ImageSource::isSupportedImage("photo.CR2"));
    QVERIFY(!ImageSource::isSupportedImage("notes.txt"));
    QVERIFY(!ImageSource::isSupportedImage("archive.zip"));
    QVERIFY(!ImageSource::isSupportedImage("noextension"));
}

void TestImageSource::archiveByExtension() {
    QVERIFY(ImageSource::isArchive("a.zip"));
    QVERIFY(ImageSource::isArchive("a.cbz"));
    QVERIFY(ImageSource::isArchive("a.7z"));
    QVERIFY(ImageSource::isArchive("a.rar"));
    QVERIFY(ImageSource::isArchive("comic.CBR"));  // 大小写不敏感
    QVERIFY(!ImageSource::isArchive("a.png"));
    QVERIFY(!ImageSource::isArchive("a.txt"));
}

QTEST_GUILESS_MAIN(TestImageSource)
#include "test_image_source.moc"
