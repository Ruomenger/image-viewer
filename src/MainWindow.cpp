#include "MainWindow.h"

#include "ImageView.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QScreen>
#include <QShowEvent>
#include <QStatusBar>
#include <QUrl>
#include <QWindow>

#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_view(new ImageView(this)) {
    setCentralWidget(m_view);
    setAcceptDrops(true);
    buildMenus();
    connectBrowser();
    // 视图放大到 Fit 档像素被拉伸时,当前页换成原图。
    connect(m_view, &ImageView::needsSharperImage, &m_browser, &Browser::requestFull);
    connect(m_view, &ImageView::zoomChanged, this, &MainWindow::updateStatus);
    updateFitTarget();  // 显示前先按初始屏幕定好,命令行打开的首图就按 Fit 档解
    statusBar()->showMessage(tr("打开图片、文件夹或压缩包以开始"));
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // 原生窗口在首次显示时才建好;换屏(含拖到另一块屏幕)时更新 Fit 目标。
    if (QWindow* window = windowHandle())
        connect(window, &QWindow::screenChanged, this, &MainWindow::updateFitTarget,
                Qt::UniqueConnection);
    updateFitTarget();
}

void MainWindow::updateFitTarget() {
    if (const QScreen* s = screen())
        m_browser.setFitTarget(s->size() * s->devicePixelRatio());
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
    connect(&m_browser, &Browser::opened, this, [this] { m_shownPage = {}; });  // 页号换了来源
    connect(&m_browser, &Browser::openFailed, this, [this](const QString& reason) {
        statusBar()->showMessage(tr("无法打开 %1:%2").arg(m_openingPath, reason), 4000);
    });
    // 流式压缩包(7z / rar / 压缩 tar)要先在后台解压一遍才有列表:期间报进度,不冻结。
    connect(&m_browser, &Browser::listingProgress, this, [this](int scanned) {
        statusBar()->showMessage(tr("正在读取 %1 … 已扫描 %2 个条目")
                                     .arg(QFileInfo(m_openingPath).fileName())
                                     .arg(scanned));
    });
    // 换页先更新标题与序号;图到之前保留上一页画面,不清屏、不阻塞。
    connect(&m_browser, &Browser::pageChanged, this, [this](Page page) {
        m_currentPage = page;
        setWindowTitle(tr("%1 — ImageViewer").arg(pageLabel(page)));
        updateStatus();
    });
    connect(&m_browser, &Browser::pageReady, this, &MainWindow::showPage);
    connect(&m_browser, &Browser::pageFailed, this, [this](Page page, const QString& reason) {
        m_view->clear();  // 不留上一页的画面,免得与标题对不上
        m_shownPage = {};
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
    if (page == m_shownPage) {
        // 同一页更清晰的图(Fit → Full、Fit 目标调大):只换 pixmap,缩放与平移不动。
        m_view->replaceImage(decoded.image);
    } else if (!decoded.animation.isEmpty()) {
        m_view->setAnimation(decoded.animation, decoded.image, decoded.originalSize);
    } else {
        m_view->setImage(decoded.image, decoded.originalSize);
    }
    m_shownPage = page;
    m_shownOriginal = decoded.originalSize;
    updateStatus();
}

void MainWindow::updateStatus() {
    if (!m_currentPage.isValid())
        return;
    if (m_currentPage != m_shownPage) {
        statusBar()->showMessage(tr("%1  ·  加载中…").arg(pageLabel(m_currentPage)));
        return;
    }
    // 尺寸与缩放都相对原图:屏上显示的可能是 Fit 档。
    statusBar()->showMessage(tr("%1 × %2  ·  %3  ·  %4%")
                                 .arg(m_shownOriginal.width())
                                 .arg(m_shownOriginal.height())
                                 .arg(pageLabel(m_shownPage))
                                 .arg(std::lround(m_view->zoom() * 100)));
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
