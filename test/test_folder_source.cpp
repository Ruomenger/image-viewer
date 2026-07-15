#include <QtTest>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "source/FolderSource.h"
#include "support/TestData.h"

class TestFolderSource : public QObject {
    Q_OBJECT
private slots:
    void naturalSortAndFilter();
    void indexOfFindsFile();
    void readEntryDecodes();
    void reportsOpenErrorForMissingDir();
};

void TestFolderSource::naturalSortAndFilter() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 故意乱序写入,并夹一个非图片文件。
    testdata::writePng(dir.filePath("10.png"), 4, 4);
    testdata::writePng(dir.filePath("2.png"), 4, 4);
    testdata::writePng(dir.filePath("1.png"), 4, 4);
    QFile note(dir.filePath("note.txt"));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("not an image");
    note.close();

    FolderSource source(dir.path());
    QCOMPARE(source.count(), 3);  // txt 被过滤
    QCOMPARE(source.entryName(0), QStringLiteral("1.png"));
    QCOMPARE(source.entryName(1), QStringLiteral("2.png"));
    QCOMPARE(source.entryName(2), QStringLiteral("10.png"));  // 自然排序: 10 在 2 之后
}

void TestFolderSource::indexOfFindsFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath("a.png"), 2, 2);
    testdata::writePng(dir.filePath("b.png"), 2, 2);

    FolderSource source(dir.path());
    QCOMPARE(source.indexOf(dir.filePath("b.png")), 1);
    QCOMPARE(source.indexOf(dir.filePath("missing.png")), -1);
}

void TestFolderSource::readEntryDecodes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath("only.png"), 8, 6);

    FolderSource source(dir.path());
    QCOMPARE(source.count(), 1);

    const QByteArray bytes = source.readEntry(0);
    QVERIFY(!bytes.isEmpty());
    const QImage image = QImage::fromData(bytes);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 8);
    QCOMPARE(image.height(), 6);
}

void TestFolderSource::reportsOpenErrorForMissingDir() {
    const FolderSource missing(QStringLiteral("/no/such/dir"));
    QCOMPARE(missing.count(), 0);
    QVERIFY(!missing.openError().isEmpty());

    // 存在但没有图片的文件夹:不算打开失败,openError 为空。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const FolderSource empty(dir.path());
    QCOMPARE(empty.count(), 0);
    QVERIFY(empty.openError().isEmpty());
}

QTEST_GUILESS_MAIN(TestFolderSource)
#include "test_folder_source.moc"
