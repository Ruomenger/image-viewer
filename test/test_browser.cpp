#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "browse/Browser.h"
#include "support/TestData.h"

class TestBrowser : public QObject {
    Q_OBJECT
private slots:
    void opensFolderAndNavigatesWithWrap();
    void openingFileStartsAtItsIndex();
    void opensArchive();
    void reportsOpenFailure();
    void failedOpenKeepsCurrentSource();
    void decodeFailureReturnsNullImage();
};

void TestBrowser::opensFolderAndNavigatesWithWrap() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 3; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);

    Browser browser;
    QVERIFY(browser.open(dir.path()));
    QCOMPARE(browser.count(), 3);
    QCOMPARE(browser.currentIndex(), 0);
    QVERIFY(!browser.currentImage().isNull());

    browser.next();
    QCOMPARE(browser.currentIndex(), 1);
    browser.goTo(5);  // 环绕: 5 % 3 == 2
    QCOMPARE(browser.currentIndex(), 2);
    browser.next();  // 尾部环绕回 0
    QCOMPARE(browser.currentIndex(), 0);
    browser.prev();  // 头部环绕回尾
    QCOMPARE(browser.currentIndex(), 2);
    QCOMPARE(browser.currentName(), QStringLiteral("2.png"));
}

void TestBrowser::openingFileStartsAtItsIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 3; ++i)
        testdata::writePng(dir.filePath(QStringLiteral("%1.png").arg(i)), 8, 8);

    Browser browser;
    QVERIFY(browser.open(dir.filePath(QStringLiteral("1.png"))));
    QCOMPARE(browser.count(), 3);  // 打开单个文件 → 浏览整个文件夹
    QCOMPARE(browser.currentIndex(), 1);
    QCOMPARE(browser.currentName(), QStringLiteral("1.png"));
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
    QVERIFY(browser.open(zip));
    QCOMPARE(browser.count(), 2);
    QCOMPARE(browser.currentImage().size(), QSize(6, 4));
}

void TestBrowser::reportsOpenFailure() {
    Browser browser;
    QString error;
    QVERIFY(!browser.open(QStringLiteral("/no/such/path"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!browser.hasSource());

    // 空文件夹:打开成功但没有图片 → 同样报失败并给出原因。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    error.clear();
    QVERIFY(!browser.open(dir.path(), &error));
    QVERIFY(!error.isEmpty());
}

void TestBrowser::failedOpenKeepsCurrentSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("a.png")), 8, 8);

    Browser browser;
    QVERIFY(browser.open(dir.path()));
    QVERIFY(!browser.open(QStringLiteral("/no/such/path")));
    QCOMPARE(browser.count(), 1);  // 旧来源保持可用
    QVERIFY(!browser.currentImage().isNull());
}

void TestBrowser::decodeFailureReturnsNullImage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    testdata::writePng(dir.filePath(QStringLiteral("good.png")), 8, 8);
    QFile bad(dir.filePath(QStringLiteral("bad.png")));  // 后缀合法但内容损坏
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("corrupt");
    bad.close();

    Browser browser;
    QVERIFY(browser.open(dir.path()));
    QCOMPARE(browser.currentName(), QStringLiteral("bad.png"));
    QVERIFY(browser.currentImage().isNull());  // 坏图:返回空,导航不受影响
    browser.next();
    QVERIFY(!browser.currentImage().isNull());
}

QTEST_GUILESS_MAIN(TestBrowser)
#include "test_browser.moc"
