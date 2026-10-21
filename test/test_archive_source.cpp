#include <QtTest>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <memory>

#include "source/ArchiveReader.h"
#include "source/ArchiveSource.h"
#include "source/StreamArchiveSource.h"
#include "source/ZipArchiveSource.h"
#include "support/TestData.h"

// openArchive() 入口:按 libarchive 探测到的实际格式分派到 Zip / Stream 两种来源,不看后缀。
class TestArchiveSource : public QObject {
    Q_OBJECT
private slots:
    void probesAccessByContentNotSuffix();
    void openArchiveDispatchesByFormat();
};

namespace {

void writeGarbage(const QString& path) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not an archive");
}

}  // namespace

void TestArchiveSource::probesAccessByContentNotSuffix() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("a.png"), testdata::makePng(2, 2)});

    const QString zipAsCbr = dir.filePath("comic.cbr");  // 后缀撒谎:cbr 里装的是 zip
    testdata::makeZip(zipAsCbr, entries);
    QCOMPARE(probeArchiveAccess(zipAsCbr), ArchiveAccess::Random);

    const QString tar = dir.filePath("a.tar");
    testdata::makeArchive(tar, entries, testdata::ArchiveFormat::Tar);
    QCOMPARE(probeArchiveAccess(tar), ArchiveAccess::Random);

    const QString tgz = dir.filePath("a.tgz");
    testdata::makeArchive(tgz, entries, testdata::ArchiveFormat::TarGz);
    QCOMPARE(probeArchiveAccess(tgz), ArchiveAccess::Streaming);

    const QString sevenZipAsZip = dir.filePath("a.zip");  // 后缀撒谎:zip 里装的是 7z
    testdata::makeArchive(sevenZipAsZip, entries, testdata::ArchiveFormat::SevenZip);
    QCOMPARE(probeArchiveAccess(sevenZipAsZip), ArchiveAccess::Streaming);

    const QString emptyZip = dir.filePath("empty.zip");  // 空包也能判定格式
    testdata::makeZip(emptyZip, {});
    QCOMPARE(probeArchiveAccess(emptyZip), ArchiveAccess::Random);

    const QString garbage = dir.filePath("garbage.zip");
    writeGarbage(garbage);
    QCOMPARE(probeArchiveAccess(garbage), ArchiveAccess::Unreadable);
    QCOMPARE(probeArchiveAccess(dir.filePath("missing.zip")), ArchiveAccess::Unreadable);
}

void TestArchiveSource::openArchiveDispatchesByFormat() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("a.png"), testdata::makePng(5, 2)});

    const QString zipAsCbr = dir.filePath("comic.cbr");
    testdata::makeZip(zipAsCbr, entries);
    const std::unique_ptr<ImageSource> zip = openArchive(zipAsCbr);
    QVERIFY(dynamic_cast<ZipArchiveSource*>(zip.get()) != nullptr);  // 随机读路径
    QVERIFY(zip->waitForListing([](int) { return false; }));         // 列表现成:不等、不报进度
    QCOMPARE(zip->count(), 1);
    QCOMPARE(QImage::fromData(zip->readEntry(0)).width(), 5);

    const QString sevenZip = dir.filePath("a.7z");
    testdata::makeArchive(sevenZip, entries, testdata::ArchiveFormat::SevenZip);
    const std::unique_ptr<ImageSource> stream = openArchive(sevenZip);
    QVERIFY(dynamic_cast<StreamArchiveSource*>(stream.get()) != nullptr);  // 流式路径
    QVERIFY(stream->waitForListing([](int) { return true; }));
    QCOMPARE(stream->count(), 1);
    QCOMPARE(QImage::fromData(stream->readEntry(0)).width(), 5);

    const QString garbage = dir.filePath("garbage.cbz");
    writeGarbage(garbage);
    const std::unique_ptr<ImageSource> bad = openArchive(garbage);
    QVERIFY(bad != nullptr);
    QVERIFY(bad->waitForListing([](int) { return true; }));
    QVERIFY(!bad->openError().isEmpty());
    QCOMPARE(bad->count(), 0);
}

QTEST_GUILESS_MAIN(TestArchiveSource)
#include "test_archive_source.moc"
