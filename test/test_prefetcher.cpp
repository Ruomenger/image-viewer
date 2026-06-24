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
};

void TestPrefetcher::prefetchesNeighborsIntoCache() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 5; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);

    auto source = std::make_shared<FolderSource>(dir.path());
    QCOMPARE(source->count(), 5);

    ImageCache cache(64LL * 1024 * 1024);
    Prefetcher prefetcher(cache, 2);
    prefetcher.setSource(source);
    prefetcher.prefetchAround(0);  // 半径 2,环绕邻域: 1、2 与 4、3

    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(1), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(2), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(3), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(cache.contains(4), 5000);
    QVERIFY(!cache.contains(0));  // 当前页不预读
}

QTEST_GUILESS_MAIN(TestPrefetcher)
#include "test_prefetcher.moc"
