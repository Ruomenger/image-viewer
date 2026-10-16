#include <QtTest>

#include <QImage>

#include "cache/ImageCache.h"

namespace {
DecodedImage makeImage(int side) {
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::red);
    return {.image = image, .originalSize = image.size()};
}

ImageKey key(int entry, Tier tier = Tier::Fit, quint64 source = 1) {
    return {.entry = {.source = source, .entry = entry}, .tier = tier};
}
}  // namespace

class TestImageCache : public QObject {
    Q_OBJECT
private slots:
    void hitAndMiss();
    void keyedBySourceAndTier();
    void evictsLeastRecentlyUsed();
    void getRefreshesRecency();
    void oversizedNotCached();
    void countsAnimationBytes();
    void fullKeepsOnlyOneAndSparesFit();
    void thumbHasItsOwnBudget();
    void shrinkingBudgetEvicts();
    void clearResets();
};

void TestImageCache::hitAndMiss() {
    ImageCache cache;
    QVERIFY(!cache.get(key(0)));  // 未命中
    QVERIFY(!cache.contains(key(0)));

    cache.insert(key(0), makeImage(16));
    QVERIFY(cache.contains(key(0)));
    QCOMPARE(cache.get(key(0))->image.size(), QSize(16, 16));
    QCOMPARE(cache.get(key(0))->originalSize, QSize(16, 16));
    QCOMPARE(cache.count(Tier::Fit), 1);

    cache.insert(key(1), {});  // 空图忽略
    QVERIFY(!cache.contains(key(1)));
}

void TestImageCache::keyedBySourceAndTier() {
    ImageCache cache;
    cache.insert(key(0, Tier::Fit, 1), makeImage(16));
    QVERIFY(!cache.contains(key(0, Tier::Fit, 2)));  // 另一个来源的同下标
    QVERIFY(!cache.contains(key(0, Tier::Thumb, 1)));
    QVERIFY(!cache.contains(key(0, Tier::Full, 1)));
}

void TestImageCache::evictsLeastRecentlyUsed() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(2 * s);  // Fit 恰好容 2 张

    cache.insert(key(0), makeImage(64));
    cache.insert(key(1), makeImage(64));
    cache.insert(key(2), makeImage(64));  // 触发淘汰最久未用(0)

    QVERIFY(!cache.contains(key(0)));
    QVERIFY(cache.contains(key(1)));
    QVERIFY(cache.contains(key(2)));
    QVERIFY(cache.totalBytes(Tier::Fit) <= cache.maxBytes(Tier::Fit));
}

void TestImageCache::getRefreshesRecency() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(2 * s);

    cache.insert(key(0), makeImage(64));
    cache.insert(key(1), makeImage(64));
    (void)cache.get(key(0));              // 0 变为最近使用,此时最久未用是 1
    cache.insert(key(2), makeImage(64));  // 应淘汰 1

    QVERIFY(cache.contains(key(0)));
    QVERIFY(!cache.contains(key(1)));
    QVERIFY(cache.contains(key(2)));
}

void TestImageCache::oversizedNotCached() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(s / 2);  // 比单张还小

    cache.insert(key(0), makeImage(64));
    QVERIFY(!cache.contains(key(0)));
    QCOMPARE(cache.totalBytes(Tier::Fit), qint64(0));
}

void TestImageCache::countsAnimationBytes() {
    ImageCache cache;
    DecodedImage animated = makeImage(16);
    animated.animation = QByteArray(1000, 'x');
    cache.insert(key(0), animated);
    QCOMPARE(cache.totalBytes(Tier::Fit), animated.image.sizeInBytes() + 1000);
}

void TestImageCache::fullKeepsOnlyOneAndSparesFit() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(2 * s);
    cache.insert(key(0), makeImage(64));
    cache.insert(key(1), makeImage(64));  // Fit 已满

    cache.insert(key(0, Tier::Full), makeImage(512));  // 远超 Fit 预算,Full 不设字节上限
    QVERIFY(cache.contains(key(0, Tier::Full)));
    QVERIFY(cache.contains(key(0)));  // 插入 Full 不挤掉 Fit
    QVERIFY(cache.contains(key(1)));

    cache.insert(key(1, Tier::Full), makeImage(512));  // Full 只留当前页
    QVERIFY(!cache.contains(key(0, Tier::Full)));
    QVERIFY(cache.contains(key(1, Tier::Full)));
    QCOMPARE(cache.count(Tier::Full), 1);
    QCOMPARE(cache.count(Tier::Fit), 2);
}

void TestImageCache::thumbHasItsOwnBudget() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(2 * s, s);  // Thumb 只容 1 张
    cache.insert(key(0), makeImage(64));
    cache.insert(key(0, Tier::Thumb), makeImage(64));
    cache.insert(key(1, Tier::Thumb), makeImage(64));

    QVERIFY(!cache.contains(key(0, Tier::Thumb)));
    QVERIFY(cache.contains(key(1, Tier::Thumb)));
    QVERIFY(cache.contains(key(0)));  // 刷缩略图不挤掉 Fit
}

void TestImageCache::shrinkingBudgetEvicts() {
    const qint64 s = makeImage(64).image.sizeInBytes();
    ImageCache cache(3 * s);
    for (int i = 0; i < 3; ++i)
        cache.insert(key(i), makeImage(64));

    cache.setMaxBytes(Tier::Fit, s);
    QCOMPARE(cache.count(Tier::Fit), 1);
    QVERIFY(cache.contains(key(2)));  // 留下最近使用的
}

void TestImageCache::clearResets() {
    ImageCache cache;
    cache.insert(key(0), makeImage(16));
    cache.insert(key(1, Tier::Thumb), makeImage(16));
    cache.insert(key(2, Tier::Full), makeImage(16));

    cache.clear(Tier::Thumb);
    QVERIFY(!cache.contains(key(1, Tier::Thumb)));
    QVERIFY(cache.contains(key(0)));

    cache.clear();
    QCOMPARE(cache.count(Tier::Fit), 0);
    QCOMPARE(cache.count(Tier::Full), 0);
    QCOMPARE(cache.totalBytes(Tier::Fit), qint64(0));
    QVERIFY(!cache.get(key(0)));
}

QTEST_GUILESS_MAIN(TestImageCache)
#include "test_image_cache.moc"
