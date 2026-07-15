#include <QtTest>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "source/ArchiveSource.h"
#include "support/TestData.h"

class TestArchiveSource : public QObject {
    Q_OBJECT
private slots:
    void listsImagesInNaturalOrder();
    void readEntryDecodes();
    void sequentialAndRandomReads();
    void duplicateNamesStayDistinct();
    void reportsOpenErrorForGarbageFile();
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

void TestArchiveSource::sequentialAndRandomReads() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("pages.cbz");

    // 乱序写入:排序后的索引顺序 ≠ 包内物理顺序,迫使顺序读快路径
    // 同时经历「继续向前扫」与「回退重开」两条路径。pageN 宽为 2+N。
    QList<QPair<QString, QByteArray>> entries;
    for (const int n : {2, 5, 1, 4, 3})
        entries.append({QStringLiteral("page%1.png").arg(n), testdata::makePng(2 + n, 2)});
    testdata::makeZip(zip, entries);

    ArchiveSource source(zip);
    QCOMPARE(source.count(), 5);

    // 顺序前进读全部。
    for (int i = 0; i < 5; ++i) {
        const QImage image = QImage::fromData(source.readEntry(i));
        QCOMPARE(source.entryName(i), QStringLiteral("page%1.png").arg(i + 1));
        QCOMPARE(image.width(), 2 + (i + 1));
    }
    // 回退与重复读(触发 reader 重开)。
    QCOMPARE(QImage::fromData(source.readEntry(1)).width(), 4);
    QCOMPARE(QImage::fromData(source.readEntry(1)).width(), 4);
    // 跳跃前进。
    QCOMPARE(QImage::fromData(source.readEntry(4)).width(), 7);
}

void TestArchiveSource::duplicateNamesStayDistinct() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("dup.zip");

    // zip 允许同名条目;按序号定位应各读各的数据而不是都命中第一个。
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("dup.png"), testdata::makePng(3, 3)});
    entries.append({QStringLiteral("dup.png"), testdata::makePng(6, 6)});
    testdata::makeZip(zip, entries);

    ArchiveSource source(zip);
    QCOMPARE(source.count(), 2);
    const QImage a = QImage::fromData(source.readEntry(0));
    const QImage b = QImage::fromData(source.readEntry(1));
    QVERIFY(!a.isNull());
    QVERIFY(!b.isNull());
    QVERIFY(a.width() != b.width());  // 同名排序顺序不保证,但必须是两份不同数据
}

void TestArchiveSource::reportsOpenErrorForGarbageFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = dir.filePath("fake.zip");
    QFile file(fake);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not an archive");
    file.close();

    ArchiveSource source(fake);
    QCOMPARE(source.count(), 0);
    QVERIFY(!source.openError().isEmpty());

    // 正常包不应报错。
    const QString ok = dir.filePath("ok.zip");
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("a.png"), testdata::makePng(2, 2)});
    testdata::makeZip(ok, entries);
    QVERIFY(ArchiveSource(ok).openError().isEmpty());
}

QTEST_GUILESS_MAIN(TestArchiveSource)
#include "test_archive_source.moc"
