#include <QtTest>

#include <QImage>

#include "cache/ImageCache.h"

namespace {
QImage makeImage(int side) {
    QImage image(side, side, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    return image;
}
}  // namespace

class TestImageCache : public QObject {
    Q_OBJECT
private slots:
    void hitAndMiss();
    void evictsLeastRecentlyUsed();
    void getRefreshesRecency();
    void oversizedNotCached();
    void clearResets();
};

void TestImageCache::hitAndMiss() {
    ImageCache cache(64LL * 1024 * 1024);
    QVERIFY(cache.get(0).isNull());  // 未命中
    QVERIFY(!cache.contains(0));

    const QImage image = makeImage(16);
    cache.insert(0, image);
    QVERIFY(cache.contains(0));
    QCOMPARE(cache.get(0).size(), image.size());
    QCOMPARE(cache.count(), 1);
}

void TestImageCache::evictsLeastRecentlyUsed() {
    const qint64 s = makeImage(64).sizeInBytes();
    ImageCache cache(2 * s);  // 恰好容 2 张

    cache.insert(0, makeImage(64));
    cache.insert(1, makeImage(64));
    cache.insert(2, makeImage(64));  // 触发淘汰最久未用(0)

    QVERIFY(!cache.contains(0));
    QVERIFY(cache.contains(1));
    QVERIFY(cache.contains(2));
    QVERIFY(cache.totalBytes() <= cache.maxBytes());
}

void TestImageCache::getRefreshesRecency() {
    const qint64 s = makeImage(64).sizeInBytes();
    ImageCache cache(2 * s);

    cache.insert(0, makeImage(64));
    cache.insert(1, makeImage(64));
    (void)cache.get(0);              // 0 变为最近使用,此时最久未用是 1
    cache.insert(2, makeImage(64));  // 应淘汰 1

    QVERIFY(cache.contains(0));
    QVERIFY(!cache.contains(1));
    QVERIFY(cache.contains(2));
}

void TestImageCache::oversizedNotCached() {
    const qint64 s = makeImage(64).sizeInBytes();
    ImageCache cache(s / 2);  // 比单张还小

    cache.insert(0, makeImage(64));
    QVERIFY(!cache.contains(0));
    QCOMPARE(cache.totalBytes(), qint64(0));
}

void TestImageCache::clearResets() {
    ImageCache cache(64LL * 1024 * 1024);
    cache.insert(0, makeImage(16));
    cache.insert(1, makeImage(16));

    cache.clear();
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.totalBytes(), qint64(0));
    QVERIFY(cache.get(0).isNull());
}

QTEST_GUILESS_MAIN(TestImageCache)
#include "test_image_cache.moc"
