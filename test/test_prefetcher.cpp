#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "cache/ImageCache.h"
#include "cache/Prefetcher.h"
#include "source/FolderSource.h"
#include "support/TestData.h"

class TestPrefetcher : public QObject {
    Q_OBJECT
private slots:
    void prefetchesNeighborsIntoCache();
    void forwardBiasedByDefault();
    void decodesRawNeighborOnPoolThread();
};

void TestPrefetcher::prefetchesNeighborsIntoCache() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 5; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);

    auto source = std::make_shared<FolderSource>(dir.path());
    QCOMPARE(source->count(), 5);

    ImageCache cache(64LL * 1024 * 1024);
    Prefetcher prefetcher(cache, 2, 2);
    prefetcher.setSource(source);
    prefetcher.prefetchAround(0);  // 前后各 2,环绕邻域: 1、2 与 4、3

    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(1), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(2), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(3), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(4), 5000);
    QVERIFY(!cache.contains(0));  // 当前页不预读
}

void TestPrefetcher::forwardBiasedByDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 8; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);

    auto source = std::make_shared<FolderSource>(dir.path());
    ImageCache cache(64LL * 1024 * 1024);
    Prefetcher prefetcher(cache);  // 默认前 3 后 1
    prefetcher.setSource(source);
    prefetcher.prefetchAround(0);

    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(1), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(2), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(3), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(7), 5000);  // 向后 1 张(环绕到尾)
    QVERIFY(!cache.contains(4));                        // 前偏:不预读更远的后方
    QVERIFY(!cache.contains(5));
    QVERIFY(!cache.contains(6));
}

void TestPrefetcher::decodesRawNeighborOnPoolThread() {
    // 回归:LibRaw 栈深超过次线程默认 512KB,未加大池线程栈时此测试 SIGBUS。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("0.png")), 8, 8);
    QVERIFY(QFile::copy(QFINDTESTDATA("data/sample.dng"), dir.filePath(QStringLiteral("1.dng"))));

    auto source = std::make_shared<FolderSource>(dir.path());
    QCOMPARE(source->count(), 2);

    ImageCache cache(256LL * 1024 * 1024);
    Prefetcher prefetcher(cache);
    prefetcher.setSource(source);
    prefetcher.prefetchAround(0);  // 邻页 1.dng 在池线程上走 LibRaw 解码

    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(1), 15000);
    QCOMPARE(cache.get(1).size(), QSize(1920, 818));
}

QTEST_GUILESS_MAIN(TestPrefetcher)
#include "test_prefetcher.moc"
