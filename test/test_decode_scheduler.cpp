#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "cache/ImageCache.h"
#include "schedule/DecodeScheduler.h"
#include "source/FolderSource.h"
#include "support/FakeSource.h"
#include "support/TestData.h"

namespace {

ImageKey keyOf(const ImageSource& source, int entry, Tier tier = Tier::Fit) {
    return {.entry = {.source = source.id(), .entry = entry}, .tier = tier};
}

QList<ImageKey> spiedKeys(const QSignalSpy& spy) {
    QList<ImageKey> keys;
    for (const QList<QVariant>& args : spy)
        keys.append(args.at(0).value<ImageKey>());
    return keys;
}

QList<int> spiedEntries(const QSignalSpy& spy) {
    QList<int> entries;
    for (const ImageKey& key : spiedKeys(spy))
        entries.append(key.entry.entry);
    return entries;
}

DecodedImage spiedImage(const QSignalSpy& spy, int i) {
    return spy.at(i).at(1).value<DecodedImage>();
}

}  // namespace

class TestDecodeScheduler : public QObject {
    Q_OBJECT
private slots:
    void prefetchesForwardWindowWithoutWrap();
    void backwardDirectionFlipsWindow();
    void currentJumpsAheadOfQueuedWork();
    void sameKeyIsReadOnce();
    void focusDropsQueuedTasksOutsideWindow();
    void sourceChangeDropsOldResults();
    void fitTargetChangeRestartsInFlightFit();
    void tiersDecodeToTheirTargets();
    void fitBudgetCoversWindowOn5K();
    void reportsDecodeFailure();
    void decodesRawOnWorkerThread();
    void destroysWithQueuedWork();
};

void TestDecodeScheduler::prefetchesForwardWindowWithoutWrap() {
    auto source = std::make_shared<FakeSource>(8);
    ImageCache cache;
    DecodeScheduler scheduler(cache);  // 默认:2 线程,前 3 后 1
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    QVERIFY(!scheduler.focus(0, Direction::Forward));  // 首次:当前页尚未缓存
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 4, 5000);
    QTest::qWait(50);  // 不应再有别的结果
    QCOMPARE(ready.count(), 4);

    for (int i = 0; i <= 3; ++i)
        QVERIFY(cache.contains(keyOf(*source, i)));
    QVERIFY(!cache.contains(keyOf(*source, 7)));  // 不环绕:首页向后没有邻页
    QVERIFY(!cache.contains(keyOf(*source, 4)));
    QCOMPARE(source->readCount(7), 0);

    QVERIFY(scheduler.focus(1, Direction::Forward));  // 当前页已缓存:直接返回
}

void TestDecodeScheduler::backwardDirectionFlipsWindow() {
    auto source = std::make_shared<FakeSource>(8);
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    scheduler.focus(5, Direction::Backward);  // 向后翻:P1 是 4、3、2,P2 是 6
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, 5000);
    QCOMPARE(source->readOrder(), QList<int>({5, 4, 3, 2, 6}));
}

void TestDecodeScheduler::currentJumpsAheadOfQueuedWork() {
    auto source = std::make_shared<FakeSource>(8);
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    source->block();
    scheduler.request(0, Tier::Fit, Priority::Ahead);
    QVERIFY(source->waitForReads(1));  // 唯一的工作线程卡在 0 上,其余都在排队
    scheduler.request(1, Tier::Thumb, Priority::Thumbnail);
    scheduler.request(2, Tier::Fit, Priority::Behind);
    scheduler.request(3, Tier::Fit, Priority::Ahead);
    scheduler.request(4, Tier::Fit, Priority::Current);
    source->release();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, 5000);
    QCOMPARE(source->readOrder(), QList<int>({0, 4, 3, 2, 1}));  // P0 > P1 > P2 > P3
}

void TestDecodeScheduler::sameKeyIsReadOnce() {
    auto source = std::make_shared<FakeSource>(8);
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    source->block();
    scheduler.request(0, Tier::Fit, Priority::Ahead);
    QVERIFY(source->waitForReads(1));
    QVERIFY(!scheduler.request(0, Tier::Fit, Priority::Current));  // 执行中:等它的结果
    scheduler.request(1, Tier::Fit, Priority::Behind);
    scheduler.request(2, Tier::Fit, Priority::Ahead);
    scheduler.request(1, Tier::Fit, Priority::Current);  // 排队中:只提升优先级
    source->release();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 5000);
    QTest::qWait(50);
    QCOMPARE(ready.count(), 3);                            // 每个键只送达一次
    QCOMPARE(source->readOrder(), QList<int>({0, 1, 2}));  // 各读一次,1 提升后排在 2 前

    QVERIFY(scheduler.request(0, Tier::Fit, Priority::Current));  // 之后命中缓存
    QCOMPARE(source->readCount(0), 1);
}

void TestDecodeScheduler::focusDropsQueuedTasksOutsideWindow() {
    auto source = std::make_shared<FakeSource>(20);
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    source->block();
    scheduler.focus(0, Direction::Forward);  // 0 执行中,1–3 排队
    QVERIFY(source->waitForReads(1));
    scheduler.request(1, Tier::Full, Priority::Current);     // 跳走后不再需要
    scheduler.request(5, Tier::Thumb, Priority::Thumbnail);  // 缩略图不受导航影响
    scheduler.focus(10, Direction::Forward);
    source->release();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 7, 5000);
    QTest::qWait(50);
    QCOMPARE(ready.count(), 7);
    QCOMPARE(source->readOrder(), QList<int>({0, 10, 11, 12, 13, 9, 5}));
    QVERIFY(cache.contains(keyOf(*source, 0)));  // 执行中的跑完照常入缓存
    QVERIFY(!cache.contains(keyOf(*source, 1)));
    QVERIFY(cache.contains(keyOf(*source, 5, Tier::Thumb)));
}

void TestDecodeScheduler::sourceChangeDropsOldResults() {
    auto oldSource = std::make_shared<FakeSource>(4);
    auto newSource = std::make_shared<FakeSource>(4);
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(oldSource);

    oldSource->block();
    scheduler.request(0, Tier::Fit, Priority::Current);
    QVERIFY(oldSource->waitForReads(1));
    scheduler.request(1, Tier::Fit, Priority::Ahead);
    scheduler.setSource(newSource);
    scheduler.request(0, Tier::Fit, Priority::Current);
    oldSource->release();

    // 单工作线程先跑完旧来源的 0、再跑新来源的 0:后者送达时前者若会送达也早已送达。
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QTest::qWait(50);
    QCOMPARE(spiedKeys(ready), QList<ImageKey>({keyOf(*newSource, 0)}));
    QCOMPARE(oldSource->readCount(1), 0);            // 旧来源的排队任务已丢弃
    QVERIFY(!cache.contains(keyOf(*oldSource, 0)));  // 迟到结果不入缓存
    QVERIFY(cache.contains(keyOf(*newSource, 0)));
}

void TestDecodeScheduler::fitTargetChangeRestartsInFlightFit() {
    auto source = std::make_shared<FakeSource>(2, QSize(100, 50));
    ImageCache cache;
    DecodeScheduler scheduler(cache, {.threads = 1});
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);

    source->block();
    scheduler.request(0, Tier::Fit, Priority::Current);  // 按原图在解
    QVERIFY(source->waitForReads(1));
    scheduler.setFitTarget(QSize(40, 40));
    QVERIFY(!scheduler.request(0, Tier::Fit, Priority::Current));  // 不并到旧任务上,另起
    source->release();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QTest::qWait(50);
    QCOMPARE(ready.count(), 1);  // 旧目标的结果丢弃
    QCOMPARE(source->readCount(0), 2);
    QCOMPARE(spiedImage(ready, 0).image.size(), QSize(40, 20));
    QCOMPARE(cache.get(keyOf(*source, 0))->image.size(), QSize(40, 20));
}

void TestDecodeScheduler::tiersDecodeToTheirTargets() {
    auto source = std::make_shared<FakeSource>(1, QSize(1000, 500));
    ImageCache cache;
    DecodeScheduler scheduler(cache);
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);
    scheduler.setFitTarget(QSize(400, 400));

    scheduler.request(0, Tier::Thumb, Priority::Thumbnail);
    scheduler.request(0, Tier::Fit, Priority::Current);
    scheduler.request(0, Tier::Full, Priority::Current);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 5000);

    QCOMPARE(cache.get(keyOf(*source, 0, Tier::Thumb))->image.size(), QSize(256, 128));
    QCOMPARE(cache.get(keyOf(*source, 0, Tier::Fit))->image.size(), QSize(400, 200));
    QCOMPARE(cache.get(keyOf(*source, 0, Tier::Full))->image.size(), QSize(1000, 500));
    for (int i = 0; i < ready.count(); ++i)
        QCOMPARE(spiedImage(ready, i).originalSize, QSize(1000, 500));
}

void TestDecodeScheduler::fitBudgetCoversWindowOn5K() {
    ImageCache cache;
    DecodeScheduler scheduler(cache);  // 窗口 = 当前 + 前 3 + 后 1
    constexpr QSize k5K(5120, 2880);

    scheduler.setFitTarget(QSize(1920, 1080));  // 小屏:窗口所需不到 256MB,维持默认
    QCOMPARE(cache.maxBytes(Tier::Fit), ImageCache::kDefaultFitBytes);

    scheduler.setFitTarget(k5K);
    QVERIFY(cache.maxBytes(Tier::Fit) > ImageCache::kDefaultFitBytes);
    for (int i = 0; i < 5; ++i)  // 未填充像素的 QImage 不占实际内存,只计账
        cache.insert({.entry = {.source = 1, .entry = i}, .tier = Tier::Fit},
                     {.image = QImage(k5K, QImage::Format_RGB32)});
    QCOMPARE(cache.count(Tier::Fit), 5);
}

void TestDecodeScheduler::reportsDecodeFailure() {
    auto source = std::make_shared<FakeSource>(2);
    source->setCorrupt(1);
    ImageCache cache;
    DecodeScheduler scheduler(cache);
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    QSignalSpy failed(&scheduler, &DecodeScheduler::failed);
    scheduler.setSource(source);

    scheduler.request(1, Tier::Fit, Priority::Current);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.at(0).at(0).value<ImageKey>(), keyOf(*source, 1));
    QCOMPARE(ready.count(), 0);
    QVERIFY(!cache.contains(keyOf(*source, 1)));
}

void TestDecodeScheduler::decodesRawOnWorkerThread() {
    // 回归:LibRaw 栈深超过次线程默认 512KB,工作线程栈不加大时此测试 SIGBUS。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("0.png")), 8, 8);
    QVERIFY(QFile::copy(QFINDTESTDATA("data/sample.dng"), dir.filePath(QStringLiteral("1.dng"))));

    auto source = std::make_shared<FolderSource>(dir.path());
    QCOMPARE(source->count(), 2);

    ImageCache cache;
    DecodeScheduler scheduler(cache);
    QSignalSpy ready(&scheduler, &DecodeScheduler::ready);
    scheduler.setSource(source);
    scheduler.focus(0, Direction::Forward);  // 邻页 1.dng 在工作线程上走 LibRaw 解码

    QTRY_VERIFY_WITH_TIMEOUT(spiedEntries(ready).contains(1), 15000);
    QCOMPARE(cache.get(keyOf(*source, 1))->image.size(), QSize(1920, 818));
}

void TestDecodeScheduler::destroysWithQueuedWork() {
    auto source = std::make_shared<FakeSource>(50);
    ImageCache cache;
    {
        DecodeScheduler scheduler(cache);
        scheduler.setSource(source);
        for (int i = 0; i < 50; ++i)
            scheduler.request(i, Tier::Thumb, Priority::Thumbnail);
    }  // 析构:丢弃排队任务、等执行中的跑完,不挂起不崩溃
    QVERIFY(source->readOrder().size() < 50);
}

QTEST_GUILESS_MAIN(TestDecodeScheduler)
#include "test_decode_scheduler.moc"
