#include <QtTest>

#include <QImage>
#include <QTemporaryDir>

#include "source/ArchiveSource.h"
#include "support/TestData.h"

class TestArchiveSource : public QObject {
    Q_OBJECT
private slots:
    void listsImagesInNaturalOrder();
    void readEntryDecodes();
};

void TestArchiveSource::listsImagesInNaturalOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("comic.cbz");

    // 乱序写入,并夹一个非图片条目。
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("page10.png"), testdata::makePng(4, 4)});
    entries.append({QStringLiteral("page2.png"), testdata::makePng(4, 4)});
    entries.append({QStringLiteral("page1.png"), testdata::makePng(4, 4)});
    entries.append({QStringLiteral("readme.txt"), QByteArray("not an image")});
    testdata::makeZip(zip, entries);

    ArchiveSource source(zip);
    QCOMPARE(source.count(), 3);  // txt 被过滤
    QCOMPARE(source.entryName(0), QStringLiteral("page1.png"));
    QCOMPARE(source.entryName(1), QStringLiteral("page2.png"));
    QCOMPARE(source.entryName(2), QStringLiteral("page10.png"));  // 自然排序
}

void TestArchiveSource::readEntryDecodes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("a.zip");

    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("img.png"), testdata::makePng(12, 9)});
    testdata::makeZip(zip, entries);

    ArchiveSource source(zip);
    QCOMPARE(source.count(), 1);

    const QImage image = QImage::fromData(source.readEntry(0));
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 12);
    QCOMPARE(image.height(), 9);
}

QTEST_GUILESS_MAIN(TestArchiveSource)
#include "test_archive_source.moc"
