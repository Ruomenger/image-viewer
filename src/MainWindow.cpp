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
    prevAction->setShortcuts({QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp),
                              QKeySequence(Qt::Key_Backspace)});
    QAction* nextAction = navMenu->addAction(tr("下一张"), this, &MainWindow::next);
    nextAction->setShortcuts(
        {QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown), QKeySequence(Qt::Key_Space)});
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

void MainWindow::openPath(const QString& path) {
    QString error;
    if (!m_browser.open(path, &error)) {
        statusBar()->showMessage(tr("无法打开 %1:%2").arg(path, error), 4000);
        return;
    }
    showCurrent();
}

void MainWindow::showCurrent() {
    const QImage image = m_browser.currentImage();
    const QString name = m_browser.currentName();

    if (image.isNull()) {
        statusBar()->showMessage(tr("无法解码: %1").arg(name), 4000);
        return;
    }

    const int position = m_browser.currentIndex() + 1;
    const int total = m_browser.count();
    m_view->setImage(image);
    setWindowTitle(tr("%1  (%2/%3) — ImageViewer").arg(name).arg(position).arg(total));
    statusBar()->showMessage(
        tr("%1 × %2  ·  %3/%4").arg(image.width()).arg(image.height()).arg(position).arg(total));
}

void MainWindow::next() {
    if (!m_browser.hasSource())
        return;
    m_browser.next();
    showCurrent();
}

void MainWindow::prev() {
    if (!m_browser.hasSource())
        return;
    m_browser.prev();
    showCurrent();
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
