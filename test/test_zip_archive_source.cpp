#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <thread>

#include "source/ZipArchiveSource.h"
#include "support/TestData.h"

// ZipArchiveSource:zip / cbz / 未压缩 tar 的无状态随机读(DESIGN §4.5)。
class TestZipArchiveSource : public QObject {
    Q_OBJECT
private slots:
    void listsImagesInNaturalOrder();
    void readsInAnyOrder();
    void duplicateNamesStayDistinct();
    void concurrentOutOfOrderReads();
    void readsUncompressedTar();
    void reportsOpenErrorForGarbageFile();
    void randomReadLatencyOnThousandPages();
};

namespace {

// 乱序写入 count 张 PNG(第 i 张宽 1+i),包内物理顺序与排序后的顺序相反。
QList<QPair<QString, QByteArray>> reversedPages(int count) {
    QList<QPair<QString, QByteArray>> entries;
    for (int i = count - 1; i >= 0; --i) {
        entries.append({QStringLiteral("p%1.png").arg(i, 3, 10, QLatin1Char('0')),
                        testdata::makePng(1 + i, 1)});
    }
    return entries;
}

int widthOf(const ImageSource& source, int index) {
    return QImage::fromData(source.readEntry(index)).width();
}

}  // namespace

void TestZipArchiveSource::listsImagesInNaturalOrder() {
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

    const ZipArchiveSource source(zip);
    QVERIFY(source.openError().isEmpty());
    QCOMPARE(source.count(), 3);  // txt 被过滤
    QCOMPARE(source.entryName(0), QStringLiteral("page1.png"));
    QCOMPARE(source.entryName(1), QStringLiteral("page2.png"));
    QCOMPARE(source.entryName(2), QStringLiteral("page10.png"));  // 自然排序
    QCOMPARE(source.entryName(3), QString());
    QVERIFY(source.readEntry(3).isEmpty());
}

void TestZipArchiveSource::readsInAnyOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("pages.cbz");
    testdata::makeZip(zip, reversedPages(5));

    const ZipArchiveSource source(zip);
    QCOMPARE(source.count(), 5);
    // 回退、重复、跳跃:每次读取彼此独立,结果只取决于下标。
    for (const int i : {4, 0, 2, 2, 1, 3, 4, 0}) {
        QCOMPARE(widthOf(source, i), 1 + i);
    }
}

void TestZipArchiveSource::duplicateNamesStayDistinct() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("dup.zip");

    // zip 允许同名条目;按序号定位应各读各的数据而不是都命中第一个。
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("dup.png"), testdata::makePng(3, 3)});
    entries.append({QStringLiteral("dup.png"), testdata::makePng(6, 6)});
    testdata::makeZip(zip, entries);

    const ZipArchiveSource source(zip);
    QCOMPARE(source.count(), 2);
    const int a = widthOf(source, 0);
    const int b = widthOf(source, 1);
    QVERIFY(a == 3 || a == 6);
    QVERIFY(b == 3 || b == 6);
    QVERIFY(a != b);  // 同名排序顺序不保证,但必须是两份不同数据
}

void TestZipArchiveSource::concurrentOutOfOrderReads() {
    constexpr int kCount = 24;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("concurrent.cbz");
    testdata::makeZip(zip, reversedPages(kCount));

    const ZipArchiveSource source(zip);
    QCOMPARE(source.count(), kCount);

    // 两个线程按不同顺序各读全部条目,主线程同时也读:没有共享游标,每个结果都只取决于下标。
    QList<int> forward;
    QList<int> backward;
    QList<int> strided;
    for (int i = 0; i < kCount; ++i) {
        forward.append(i);
        backward.append(kCount - 1 - i);
        strided.append((i * 7) % kCount);  // 7 与 24 互质:是一个排列
    }
    const auto readAll = [&source](const QList<int>& order, QList<int>& widths) {
        for (const int i : order)
            widths.append(widthOf(source, i));
    };
    QList<int> w1;
    QList<int> w2;
    QList<int> w3;
    std::thread t1(readAll, std::cref(forward), std::ref(w1));
    std::thread t2(readAll, std::cref(backward), std::ref(w2));
    readAll(strided, w3);
    t1.join();
    t2.join();

    for (int k = 0; k < kCount; ++k) {
        QCOMPARE(w1.at(k), 1 + forward.at(k));
        QCOMPARE(w2.at(k), 1 + backward.at(k));
        QCOMPARE(w3.at(k), 1 + strided.at(k));
    }
}

void TestZipArchiveSource::readsUncompressedTar() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tar = dir.filePath("pages.tar");
    testdata::makeArchive(tar, reversedPages(6), testdata::ArchiveFormat::Tar);

    const ZipArchiveSource source(tar);
    QVERIFY(source.openError().isEmpty());
    QCOMPARE(source.count(), 6);
    for (const int i : {5, 0, 3, 3, 1}) {
        QCOMPARE(widthOf(source, i), 1 + i);
    }
}

void TestZipArchiveSource::reportsOpenErrorForGarbageFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = dir.filePath("fake.zip");
    QFile file(fake);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not an archive");
    file.close();

    const ZipArchiveSource source(fake);
    QCOMPARE(source.count(), 0);
    QVERIFY(!source.openError().isEmpty());
    QVERIFY(source.readEntry(0).isEmpty());

    QVERIFY(!ZipArchiveSource(dir.filePath("missing.zip")).openError().isEmpty());
}

void TestZipArchiveSource::randomReadLatencyOnThousandPages() {
    // 千页 cbz 的单次随机读:读最后一页要前进过全部 999 个条目头(每个一次 seek + 读头)。
    // DESIGN §4.5:若超过约 10ms 就该换 libzip / minizip-ng。页的内容不解码,用伪随机字节即可;
    // 不压缩存放,让条目头在文件里真的隔开。
    constexpr int kPages = 1000;
    constexpr int kPageBytes = 20 * 1024;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath("thousand.cbz");

    QList<QPair<QString, QByteArray>> entries;
    quint32 state = 12345;
    for (int i = 0; i < kPages; ++i) {
        QByteArray bytes(kPageBytes, Qt::Uninitialized);
        for (char& c : bytes) {
            state = state * 1664525U + 1013904223U;
            c = static_cast<char>(state >> 24);
        }
        entries.append({QStringLiteral("%1.jpg").arg(i, 4, 10, QLatin1Char('0')), bytes});
    }
    testdata::makeArchive(zip, entries, testdata::ArchiveFormat::ZipStored);

    QElapsedTimer timer;
    timer.start();
    const ZipArchiveSource source(zip);
    const double listMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    QCOMPARE(source.count(), kPages);

    double maxMs = 0;
    double totalMs = 0;
    const QList<int> probes = {kPages - 1, 0, kPages / 2, kPages / 4, kPages * 3 / 4, kPages - 1};
    for (const int i : probes) {
        timer.restart();
        const QByteArray data = source.readEntry(i);
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        QCOMPARE(data, entries.at(i).second);
        maxMs = std::max(maxMs, ms);
        totalMs += ms;
    }
    qInfo("千页 zip(每页 20KB,stored):列表 %.1f ms;随机读 %d 次,最大 %.2f ms,平均 %.2f ms", listMs,
          static_cast<int>(probes.size()), maxMs, totalMs / probes.size());
    // 宽松上限只为拦住数量级的退化(如退回到逐条目解压);真实数字看上面的日志。
    QVERIFY2(maxMs < 200.0, "千页 zip 单次随机读超过 200ms");
}

QTEST_GUILESS_MAIN(TestZipArchiveSource)
#include "test_zip_archive_source.moc"
