#include <QtTest>

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

QTEST_GUILESS_MAIN(TestPrefetcher)
#include "test_prefetcher.moc"
