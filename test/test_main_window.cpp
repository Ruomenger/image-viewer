#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QStatusBar>
#include <QTemporaryDir>

#include <algorithm>

#include "ImageView.h"
#include "MainWindow.h"
#include "support/TestData.h"

// 离屏端到端冒烟:真实 MainWindow + Browser + 调度器,打开 → 翻页 → 放大。
class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void navigatesWithoutBlocking();
    void zoomingInSwapsToFullResolution();
};

void TestMainWindow::navigatesWithoutBlocking() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int kCount = 6;
    // 无内嵌预览的 DNG:若在 UI 线程解码,单次翻页要几百毫秒(纯色 PNG 解码太快,测不出来)。
    for (int i = 0; i < kCount; ++i)
        QVERIFY(QFile::copy(QFINDTESTDATA("data/sample.dng"),
                            dir.filePath(QStringLiteral("%1.dng").arg(i))));

    MainWindow window;
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains("(1/6)  ·  "), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains("1920 × 818"), 15000);

    QElapsedTimer timer;
    qint64 slowest = 0;
    for (int i = 1; i < kCount; ++i) {  // 连续翻页,不等图
        timer.start();
        window.next();
        slowest = std::max(slowest, timer.elapsed());
    }
    qInfo("next() 最慢 %lldms", slowest);
    QVERIFY2(slowest < 50, qPrintable(QStringLiteral("next() 最慢 %1ms").arg(slowest)));
    QVERIFY(window.windowTitle().contains("(6/6)"));  // 序号立即更新

    QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().startsWith("1920 × 818"), 15000);
    QVERIFY(window.statusBar()->currentMessage().contains("(6/6)"));
    window.next();
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("已是最后一张"));
}

void TestMainWindow::zoomingInSwapsToFullResolution() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("photo.png"));
    testdata::writePng(path, 3000, 2000);

    MainWindow window;
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);
    auto* view = window.findChild<ImageView*>();
    QVERIFY(view != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!view->scene()->items().isEmpty(), 10000);
    auto* item = qgraphicsitem_cast<QGraphicsPixmapItem*>(view->scene()->items().first());
    QVERIFY(item != nullptr);
    QVERIFY(item->pixmap().width() < 3000);  // 先出 Fit 档(屏幕物理像素)

    view->actualSize();  // 放大到 100%:Fit 像素被拉伸 → 请求原图
    QTRY_COMPARE_WITH_TIMEOUT(item->pixmap().size(), QSize(3000, 2000), 10000);
    QCOMPARE(view->zoom(), 1.0);  // 换图不动缩放
    QVERIFY(window.statusBar()->currentMessage().endsWith("100%"));
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
