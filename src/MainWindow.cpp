#include "MainWindow.h"

#include "ImageView.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QUrl>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_view(new ImageView(this)) {
    setCentralWidget(m_view);
    setAcceptDrops(true);
    buildMenus();
    connectBrowser();
    statusBar()->showMessage(tr("打开图片、文件夹或压缩包以开始"));
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    QAction* openFileAction = fileMenu->addAction(tr("打开图片(&O)…"), this, &MainWindow::openFile);
    openFileAction->setShortcut(QKeySequence::Open);
    fileMenu->addAction(tr("打开文件夹(&D)…"), this, &MainWindow::openFolder);
    fileMenu->addAction(tr("打开压缩包(&A)…"), this, &MainWindow::openArchive);
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(tr("退出(&Q)"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    QAction* fit = viewMenu->addAction(tr("适应窗口"), this, [this] { m_view->fitToWindow(); });
    fit->setShortcut(QKeySequence(Qt::Key_F));
    QAction* actual = viewMenu->addAction(tr("实际大小"), this, [this] { m_view->actualSize(); });
    actual->setShortcuts({QKeySequence(Qt::ControlModifier | Qt::Key_0), QKeySequence(Qt::Key_1)});
    QAction* zoomIn = viewMenu->addAction(tr("放大"), this, [this] { m_view->zoomIn(); });
    zoomIn->setShortcuts(
        {QKeySequence::ZoomIn, QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    QAction* zoomOut = viewMenu->addAction(tr("缩小"), this, [this] { m_view->zoomOut(); });
    zoomOut->setShortcuts({QKeySequence::ZoomOut, QKeySequence(Qt::Key_Minus)});

    QMenu* navMenu = menuBar()->addMenu(tr("浏览(&N)"));
    QAction* prevAction = navMenu->addAction(tr("上一张"), this, &MainWindow::prev);
    prevAction->setShortcuts({
        QKeySequence(Qt::Key_Left),
        QKeySequence(Qt::Key_PageUp),
        QKeySequence(Qt::Key_Backspace),
    });
    QAction* nextAction = navMenu->addAction(tr("下一张"), this, &MainWindow::next);
    nextAction->setShortcuts({
        QKeySequence(Qt::Key_Right),
        QKeySequence(Qt::Key_PageDown),
        QKeySequence(Qt::Key_Space),
    });
}

void MainWindow::openFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("打开文件夹"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openArchive() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("打开压缩包"), QString(),
                                     tr("压缩包 (*.zip *.cbz *.7z *.cb7 *.rar *.cbr *.tar *.cbt)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::connectBrowser() {
    connect(&m_browser, &Browser::openFailed, this, [this](const QString& reason) {
        statusBar()->showMessage(tr("无法打开 %1:%2").arg(m_openingPath, reason), 4000);
    });
    // 换页先更新标题与序号;图到之前保留上一页画面,不清屏、不阻塞。
    connect(&m_browser, &Browser::pageChanged, this, [this](Page page) {
        setWindowTitle(tr("%1 — ImageViewer").arg(pageLabel(page)));
        statusBar()->showMessage(tr("%1  ·  加载中…").arg(pageLabel(page)));
    });
    connect(&m_browser, &Browser::pageReady, this, &MainWindow::showPage);
    connect(&m_browser, &Browser::pageFailed, this, [this](Page page, const QString& reason) {
        statusBar()->showMessage(tr("%1  ·  %2").arg(pageLabel(page), reason));
    });
    connect(&m_browser, &Browser::reachedEnd, this, [this](bool forward) {
        statusBar()->showMessage(forward ? tr("已是最后一张") : tr("已是第一张"), 2000);
    });
}

void MainWindow::openPath(const QString& path) {
    m_openingPath = path;
    m_browser.open(path);  // 后台建列表;结果经 opened / openFailed 信号回来
}

void MainWindow::showPage(Page page, const QList<DecodedImage>& images) {
    const DecodedImage& decoded = images.first();  // 单页布局
    if (!decoded.animation.isEmpty())
        m_view->setAnimation(decoded.animation, decoded.image);
    else
        m_view->setImage(decoded.image);
    statusBar()->showMessage(tr("%1 × %2  ·  %3")
                                 .arg(decoded.image.width())
                                 .arg(decoded.image.height())
                                 .arg(pageLabel(page)));
}

QString MainWindow::pageLabel(Page page) const {
    return tr("%1  (%2/%3)")
        .arg(m_browser.entryName(page.first))
        .arg(page.first + 1)
        .arg(m_browser.count());
}

void MainWindow::next() {
    m_browser.next();
}

void MainWindow::prev() {
    m_browser.prev();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QString local = urls.first().toLocalFile();
    if (!local.isEmpty())
        openPath(local);
}
