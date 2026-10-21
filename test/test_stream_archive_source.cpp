#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <memory>

#include "source/StreamArchiveSource.h"
#include "support/TestData.h"

// StreamArchiveSource:7z / 压缩 tar 等顺序解压格式的「后台抽取一遍 + ByteStore」(DESIGN §4.5)。
// RAR 无法现场生成,待补真实小样本。
class TestStreamArchiveSource : public QObject {
    Q_OBJECT
private slots:
    void listsAndExtractsInOnePass();
    void reportsProgressAndCanBeAbandoned();
    void spillsBeyondMemoryBudget();
    void duplicateNamesStayDistinct();
    void truncatedStreamKeepsExtractedPrefix();
    void reportsOpenErrorForGarbageFile();
};

namespace {

// 乱序写入 count 张 PNG(第 i 张宽 1+i),包内物理顺序与排序后的顺序相反;夹一个非图片条目。
QList<QPair<QString, QByteArray>> reversedPages(int count) {
    QList<QPair<QString, QByteArray>> entries;
    for (int i = count - 1; i >= 0; --i) {
        entries.append({QStringLiteral("p%1.png").arg(i, 3, 10, QLatin1Char('0')),
                        testdata::makePng(1 + i, 1)});
    }
    entries.insert(1, {QStringLiteral("readme.txt"), QByteArray("not an image")});
    return entries;
}

// count 个各 bytesEach 字节的伪随机条目(不可压缩,也不需要可解码),名字按序。
QList<QPair<QString, QByteArray>> randomPages(int count, int bytesEach, quint32 seed) {
    QList<QPair<QString, QByteArray>> entries;
    quint32 state = seed;
    for (int i = 0; i < count; ++i) {
        QByteArray bytes(bytesEach, Qt::Uninitialized);
        for (char& c : bytes) {
            state = state * 1664525U + 1013904223U;
            c = static_cast<char>(state >> 24);
        }
        entries.append({QStringLiteral("%1.jpg").arg(i, 4, 10, QLatin1Char('0')), bytes});
    }
    return entries;
}

int widthOf(const ImageSource& source, int index) {
    return QImage::fromData(source.readEntry(index)).width();
}

bool waitListed(ImageSource& source) {
    return source.waitForListing([](int) { return true; });
}

}  // namespace

void TestStreamArchiveSource::listsAndExtractsInOnePass() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sevenZip = dir.filePath("pages.7z");
    testdata::makeArchive(sevenZip, reversedPages(6), testdata::ArchiveFormat::SevenZip);
    const QString tgz = dir.filePath("pages.tar.gz");
    testdata::makeArchive(tgz, reversedPages(6), testdata::ArchiveFormat::TarGz);

    for (const QString& path : {sevenZip, tgz}) {
        StreamArchiveSource source(path);
        QVERIFY(waitListed(source));
        QVERIFY(source.isListed());
        QVERIFY2(source.openError().isEmpty(), qPrintable(source.openError()));
        QCOMPARE(source.count(), 6);  // txt 被过滤
        QCOMPARE(source.entryName(0), QStringLiteral("p000.png"));
        QCOMPARE(source.entryName(5), QStringLiteral("p005.png"));
        QCOMPARE(source.spilledCount(), 0);  // 默认预算下全在内存

        // 任意顺序读:回退、重复、跳跃。
        for (const int i : {5, 0, 3, 3, 1, 2, 4}) {
            QCOMPARE(widthOf(source, i), 1 + i);
        }
        // 抽取只走一遍、不重开:删掉包文件后仍能读到每一条。
        QVERIFY(QFile::remove(path));
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(widthOf(source, i), 1 + i);
        }
        QVERIFY(source.readEntry(6).isEmpty());
    }
}

void TestStreamArchiveSource::reportsProgressAndCanBeAbandoned() {
    // 足够大的包,抽取需要一段可观测的时间(千条 × 16KB,gzip 后约 16MB)。
    constexpr int kCount = 1000;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tgz = dir.filePath("big.tar.gz");
    const auto entries = randomPages(kCount, 16 * 1024, 7);
    testdata::makeArchive(tgz, entries, testdata::ArchiveFormat::TarGz);

    {
        StreamArchiveSource source(tgz);
        QList<int> progress;
        QVERIFY(source.waitForListing([&progress](int scanned) {
            progress.append(scanned);
            return true;
        }));
        QVERIFY(!progress.isEmpty());                                 // 至少报一次
        QVERIFY(std::is_sorted(progress.cbegin(), progress.cend()));  // 只增不减
        QCOMPARE(progress.last(), kCount);                            // 最后一次是最终计数
        QCOMPARE(source.count(), kCount);
        QCOMPARE(source.readEntry(kCount - 1), entries.last().second);
    }
    {
        // 调用方放弃等待(如用户又打开了别的):waitForListing 立即返回 false,
        // 销毁来源取消后台抽取、不等它跑完。
        QElapsedTimer timer;
        timer.start();
        auto source = std::make_unique<StreamArchiveSource>(tgz);
        QVERIFY(!source->waitForListing([](int) { return false; }));
        source.reset();
        QVERIFY2(timer.elapsed() < 5000, "放弃后销毁来源不应等整包抽完");
    }
}

void TestStreamArchiveSource::spillsBeyondMemoryBudget() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sevenZip = dir.filePath("spill.7z");
    const auto entries = randomPages(5, 4 * 1024, 3);
    testdata::makeArchive(sevenZip, entries, testdata::ArchiveFormat::SevenZip);

    QString spillDir;
    {
        // 预算只够放前两条,其余三条落盘到会话临时目录。
        StreamArchiveSource source(sevenZip, qint64{2} * 4 * 1024);
        QVERIFY(waitListed(source));
        QCOMPARE(source.count(), 5);
        QCOMPARE(source.spilledCount(), 3);
        spillDir = source.spillDir();
        QVERIFY(!spillDir.isEmpty());
        QVERIFY(QDir(spillDir).exists());
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(source.readEntry(i), entries.at(i).second);  // 内存 / 落盘两条路都对
        }
    }
    QVERIFY(!QDir(spillDir).exists());  // 关闭来源即删临时目录

    // 预算为 0:全部落盘。
    StreamArchiveSource all(sevenZip, 0);
    QVERIFY(waitListed(all));
    QCOMPARE(all.spilledCount(), 5);
    QCOMPARE(all.readEntry(4), entries.at(4).second);
}

void TestStreamArchiveSource::duplicateNamesStayDistinct() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sevenZip = dir.filePath("dup.7z");
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("dup.png"), testdata::makePng(3, 3)});
    entries.append({QStringLiteral("dup.png"), testdata::makePng(6, 6)});
    testdata::makeArchive(sevenZip, entries, testdata::ArchiveFormat::SevenZip);

    StreamArchiveSource source(sevenZip);
    QVERIFY(waitListed(source));
    QCOMPARE(source.count(), 2);
    const int a = widthOf(source, 0);
    const int b = widthOf(source, 1);
    QVERIFY(a == 3 || a == 6);
    QVERIFY(b == 3 || b == 6);
    QVERIFY(a != b);  // 按序号区分,不是都命中第一个
}

void TestStreamArchiveSource::truncatedStreamKeepsExtractedPrefix() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tgz = dir.filePath("cut.tar.gz");
    // gzip 过滤器按 64KB 输出块解压,输入在块中间截断时整块报错:条目要比块大,
    // 第一条才能完整落在截断点之前的块里。
    const auto entries = randomPages(3, 200 * 1024, 11);
    testdata::makeArchive(tgz, entries, testdata::ArchiveFormat::TarGz);

    // 截到第二条中间:第一条完整,其后读不到;包仍可浏览、不算打开失败。
    QFile file(tgz);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.size() > 300 * 1024);
    QVERIFY(file.resize(300 * 1024));
    file.close();

    StreamArchiveSource source(tgz);
    QVERIFY(waitListed(source));
    QVERIFY(source.openError().isEmpty());
    QVERIFY(source.count() >= 1 && source.count() <= 2);
    QCOMPARE(source.readEntry(0), entries.at(0).second);
}

void TestStreamArchiveSource::reportsOpenErrorForGarbageFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = dir.filePath("fake.7z");
    QFile file(fake);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not an archive");
    file.close();

    StreamArchiveSource source(fake);
    QVERIFY(source.isListed());  // 没东西可抽:列表即刻完成
    QVERIFY(waitListed(source));
    QVERIFY(!source.openError().isEmpty());
    QCOMPARE(source.count(), 0);
    QVERIFY(source.readEntry(0).isEmpty());

    StreamArchiveSource missing(dir.filePath("missing.7z"));
    QVERIFY(waitListed(missing));
    QVERIFY(!missing.openError().isEmpty());
}

QTEST_GUILESS_MAIN(TestStreamArchiveSource)
#include "test_stream_archive_source.moc"
