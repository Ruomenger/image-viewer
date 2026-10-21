#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMenuBar>
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
    void fullScreenHidesChromeAndKeepsShortcuts();
    void doubleClickTogglesFullScreenKeepingMaximized();
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

void TestMainWindow::fullScreenHidesChromeAndKeepsShortcuts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 3; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 64, 48);

    MainWindow window;
    window.menuBar()->setNativeMenuBar(false);  // 按 Windows / Linux 的窗口内菜单栏测
    window.resize(800, 600);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));  // 窗口级快捷键只在激活的窗口里生效
    window.openPath(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(window.windowTitle().contains("(1/3)"), 10000);
    QVERIFY(window.menuBar()->isVisible());

    QTest::keyClick(&window, Qt::Key_Return);
    QVERIFY(window.isFullScreen());
    QVERIFY(!window.menuBar()->isVisible());
    QVERIFY(!window.statusBar()->isVisible());

    QTest::keyClick(&window, Qt::Key_Right);  // 菜单栏藏起来后,菜单里动作的快捷键仍有效
    QTRY_VERIFY(window.windowTitle().contains("(2/3)"));

    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.isFullScreen());
    QVERIFY(window.menuBar()->isVisible());
    QVERIFY(window.statusBar()->isVisible());
    QTest::keyClick(&window, Qt::Key_Escape);  // 非全屏时 Esc 不做任何事
    QVERIFY(!window.isFullScreen());
}

void TestMainWindow::doubleClickTogglesFullScreenKeepingMaximized() {
    MainWindow window;
    window.showMaximized();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* view = window.findChild<ImageView*>();
    QVERIFY(view != nullptr);
    // 走 QWindow 的平台事件路径:按下-松开-按下-松开,双击由 Qt 合成。
    const auto doubleClick = [&] {
        const QWidget* viewport = view->viewport();
        QTest::mouseDClick(window.windowHandle(), Qt::LeftButton, {},
                           viewport->mapTo(&window, viewport->rect().center()));
    };

    doubleClick();
    QVERIFY(window.isFullScreen());
    doubleClick();
    QVERIFY(!window.isFullScreen());
    QVERIFY(window.isMaximized());  // 回到进入前的最大化,而不是普通窗口
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
