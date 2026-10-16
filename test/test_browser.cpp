#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "browse/Browser.h"
#include "support/FakeSource.h"
#include "support/TestData.h"

namespace {

// 按发出顺序记录 Browser 的信号,断言顺序与内容。须声明在 Browser 之前(活得更久)。
struct Recorder {
    QStringList events;

    void attach(Browser& browser) {
        QObject::connect(&browser, &Browser::opened,
                         [this](int n) { events << QStringLiteral("opened %1").arg(n); });
        QObject::connect(&browser, &Browser::openFailed,
                         [this](const QString&) { events << QStringLiteral("openFailed"); });
        QObject::connect(&browser, &Browser::pageChanged, [this](Page page) {
            events << QStringLiteral("pageChanged %1").arg(page.first);
        });
        QObject::connect(&browser, &Browser::pageReady,
                         [this](Page page, const QList<DecodedImage>&) {
                             events << QStringLiteral("pageReady %1").arg(page.first);
                         });
        QObject::connect(&browser, &Browser::pageFailed, [this](Page page, const QString&) {
            events << QStringLiteral("pageFailed %1").arg(page.first);
        });
        QObject::connect(&browser, &Browser::reachedEnd, [this](bool forward) {
            events << QStringLiteral("reachedEnd %1").arg(forward ? "forward" : "backward");
        });
    }
};

DecodedImage readyImage(const QSignalSpy& spy, int i) {
    return spy.at(i).at(1).value<QList<DecodedImage>>().first();
}

void writePngs(const QTemporaryDir& dir, int count) {
    for (int i = 0; i < count; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);
}

}  // namespace

class TestBrowser : public QObject {
    Q_OBJECT
private slots:
    void opensFolderAndStopsAtEnds();
    void openingFileStartsAtItsIndex();
    void opensArchive();
    void reportsOpenFailure();
    void failedOpenKeepsCurrentSource();
    void laterOpenWins();
    void decodeFailureReportsPageFailed();
    void animationBytesComeWithPage();
    void cacheHitIsSynchronous();
    void staleResultsAreDropped();
    void neverReadsOnCallingThread();
    void requestFullUpgradesCurrentPage();
    void fitTargetChangeRedecodesCurrentPage();
};

void TestBrowser::opensFolderAndStopsAtEnds() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writePngs(dir, 3);

    Recorder rec;
    Browser browser;
    rec.attach(browser);
    browser.open(dir.path());
    QVERIFY(rec.events.isEmpty());  // 列表在后台建:open 返回时还没有任何结果
    QTRY_COMPARE(rec.events, QStringList({"opened 3", "pageChanged 0", "pageReady 0"}));
    QCOMPARE(browser.count(), 3);

    rec.events.clear();
    browser.next();
    QCOMPARE(rec.events.first(), QStringLiteral("pageChanged 1"));  // 序号立即更新
    QTRY_VERIFY(rec.events.contains("pageReady 1"));

    browser.goTo(5);  // 越界贴到末页
    QCOMPARE(browser.currentPage(), Page({.first = 2, .count = 1}));
    rec.events.clear();
    browser.next();  // 末页继续翻:停住,不环绕
    QCOMPARE(rec.events, QStringList({"reachedEnd forward"}));
    QCOMPARE(browser.currentPage().first, 2);

    browser.goTo(-3);
    QCOMPARE(browser.currentPage().first, 0);
    rec.events.clear();
    browser.prev();
    QCOMPARE(rec.events, QStringList({"reachedEnd backward"}));
    QCOMPARE(browser.entryName(0), QStringLiteral("0.png"));
}

void TestBrowser::openingFileStartsAtItsIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writePngs(dir, 3);

    Browser browser;
    QSignalSpy opened(&browser, &Browser::opened);
    browser.open(dir.filePath(QStringLiteral("1.png")));
    QTRY_COMPARE(opened.count(), 1);
    QCOMPARE(browser.count(), 3);  // 打开单个文件 → 浏览整个文件夹
    QCOMPARE(browser.currentPage().first, 1);
    QCOMPARE(browser.entryName(1), QStringLiteral("1.png"));
}

void TestBrowser::opensArchive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zip = dir.filePath(QStringLiteral("a.cbz"));
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("p1.png"), testdata::makePng(6, 4)});
    entries.append({QStringLiteral("p2.png"), testdata::makePng(6, 4)});
    testdata::makeZip(zip, entries);

    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.open(zip);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(browser.count(), 2);
    QCOMPARE(readyImage(ready, 0).image.size(), QSize(6, 4));
}

void TestBrowser::reportsOpenFailure() {
    Browser browser;
    QSignalSpy failed(&browser, &Browser::openFailed);
    browser.open(QStringLiteral("/no/such/path"));
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!failed.at(0).at(0).toString().isEmpty());
    QVERIFY(!browser.hasSource());

    // 空文件夹:打开成功但没有图片 → 同样报失败并给出原因。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    browser.open(dir.path());
    QTRY_COMPARE(failed.count(), 2);
    QVERIFY(!failed.at(1).at(0).toString().isEmpty());
}

void TestBrowser::failedOpenKeepsCurrentSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("a.png")), 8, 8);

    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    QSignalSpy failed(&browser, &Browser::openFailed);
    browser.open(dir.path());
    QTRY_COMPARE(ready.count(), 1);
    browser.open(QStringLiteral("/no/such/path"));
    QTRY_COMPARE(failed.count(), 1);

    QCOMPARE(browser.count(), 1);  // 旧来源保持可用
    browser.goTo(0);
    QCOMPARE(ready.count(), 2);  // 仍命中旧来源的缓存
}

void TestBrowser::laterOpenWins() {
    QTemporaryDir a;
    QTemporaryDir b;
    QVERIFY(a.isValid() && b.isValid());
    writePngs(a, 2);
    writePngs(b, 3);

    Recorder rec;
    Browser browser;
    rec.attach(browser);
    browser.open(a.path());
    browser.open(b.path());
    QTRY_VERIFY(rec.events.contains("pageReady 0"));
    QTest::qWait(50);
    QCOMPARE(rec.events.filter(QStringLiteral("opened")), QStringList({"opened 3"}));
    QCOMPARE(browser.count(), 3);
}

void TestBrowser::decodeFailureReportsPageFailed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("good.png")), 8, 8);
    QFile bad(dir.filePath(QStringLiteral("bad.png")));  // 后缀合法但内容损坏
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("corrupt");
    bad.close();

    Recorder rec;
    Browser browser;
    rec.attach(browser);
    browser.open(dir.path());
    QTRY_VERIFY(rec.events.contains("pageFailed 0"));
    QCOMPARE(browser.entryName(0), QStringLiteral("bad.png"));
    QVERIFY(!rec.events.contains("pageReady 0"));

    browser.next();  // 坏图不影响导航
    QTRY_VERIFY(rec.events.contains("pageReady 1"));
}

void TestBrowser::animationBytesComeWithPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 自然排序:anim.gif 在 still.png 之前。
    QVERIFY(QFile::copy(QFINDTESTDATA("data/sample-anim.gif"), dir.filePath("anim.gif")));
    testdata::writePng(dir.filePath("still.png"), 8, 8);

    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.open(dir.path());
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(!readyImage(ready, 0).image.isNull());       // 首帧照常可显示
    QVERIFY(!readyImage(ready, 0).animation.isEmpty());  // 动画字节随结果返回,不必重读

    browser.next();
    QTRY_COMPARE(ready.count(), 2);
    QVERIFY(readyImage(ready, 1).animation.isEmpty());  // 静态图为空
}

void TestBrowser::cacheHitIsSynchronous() {
    auto source = std::make_shared<FakeSource>(3);
    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.openSource(source);
    QTRY_COMPARE(ready.count(), 1);
    browser.next();
    QTRY_COMPARE(ready.count(), 2);

    browser.prev();  // 第 0 页已在缓存:pageReady 在 prev() 返回前发出
    QCOMPARE(ready.count(), 3);
    QCOMPARE(ready.at(2).at(0).value<Page>().first, 0);
}

void TestBrowser::staleResultsAreDropped() {
    auto source = std::make_shared<FakeSource>(10);
    Recorder rec;
    Browser browser;
    rec.attach(browser);

    source->block();
    browser.openSource(source);  // 第 0 页及预读都卡在读取上
    QVERIFY(source->waitForReads(1));
    browser.goTo(5);
    source->release();

    QTRY_VERIFY(rec.events.contains("pageReady 5"));
    QTest::qWait(50);
    QVERIFY(!rec.events.contains("pageReady 0"));  // 已翻走的页不再送达
    QCOMPARE(rec.events.filter(QStringLiteral("pageChanged")),
             QStringList({"pageChanged 0", "pageChanged 5"}));
}

void TestBrowser::neverReadsOnCallingThread() {
    // UI 线程守卫:viewer-core 在调用线程上不读字节、不解码。
    auto source = std::make_shared<FakeSource>(6);
    source->forbidReadsOn(QThread::currentThread());
    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.setFitTarget(QSize(4, 4));  // 8×8 的图按 4×4 解,requestFull 才会去解原图

    browser.openSource(source);
    QTRY_COMPARE(ready.count(), 1);
    browser.next();
    browser.next();
    browser.prev();
    browser.requestFull();
    browser.goTo(5);
    QTRY_COMPARE(ready.at(ready.count() - 1).at(0).value<Page>().first, 5);

    QVERIFY(!source->readOrder().isEmpty());
    QCOMPARE(source->forbiddenReads(), 0);
}

void TestBrowser::requestFullUpgradesCurrentPage() {
    auto large = std::make_shared<FakeSource>(2, QSize(1000, 500));
    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.setFitTarget(QSize(400, 400));
    browser.openSource(large);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(readyImage(ready, 0).image.size(), QSize(400, 200));
    QCOMPARE(readyImage(ready, 0).originalSize, QSize(1000, 500));

    browser.requestFull();
    QTRY_COMPARE(ready.count(), 2);  // 同一页再发一次,换成原图
    QCOMPARE(ready.at(1).at(0).value<Page>().first, 0);
    QCOMPARE(readyImage(ready, 1).image.size(), QSize(1000, 500));
    QCOMPARE(large->readCount(0), 2);  // Fit + Full

    // 原图不大于 Fit 目标:Fit 即原图,requestFull 不再解。
    auto small = std::make_shared<FakeSource>(2, QSize(100, 50));
    browser.openSource(small);
    QTRY_COMPARE(ready.count(), 3);
    browser.requestFull();
    QTest::qWait(50);
    QCOMPARE(ready.count(), 3);
    QCOMPARE(small->readCount(0), 1);
}

void TestBrowser::fitTargetChangeRedecodesCurrentPage() {
    auto source = std::make_shared<FakeSource>(1, QSize(1000, 500));
    Browser browser;
    QSignalSpy ready(&browser, &Browser::pageReady);
    browser.setFitTarget(QSize(200, 200));
    browser.openSource(source);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(readyImage(ready, 0).image.size(), QSize(200, 100));

    browser.setFitTarget(QSize(400, 400));  // 换到更大的屏:按新尺寸重解
    QTRY_COMPARE(ready.count(), 2);
    QCOMPARE(readyImage(ready, 1).image.size(), QSize(400, 200));

    browser.setFitTarget(QSize(100, 100));  // 更小的屏:已有的更清晰,不再发
    QTest::qWait(50);
    QCOMPARE(ready.count(), 2);
}

QTEST_GUILESS_MAIN(TestBrowser)
#include "test_browser.moc"
