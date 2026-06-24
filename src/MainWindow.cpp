#include "MainWindow.h"

#include "ImageView.h"
#include "source/ArchiveSource.h"
#include "source/FolderSource.h"
#include "source/ImageSource.h"

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
#include <QStatusBar>
#include <QUrl>

#include <algorithm>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_view(new ImageView(this)) {
    setCentralWidget(m_view);
    setAcceptDrops(true);
    buildMenus();
    statusBar()->showMessage(tr("打开图片、文件夹或压缩包以开始"));
}

MainWindow::~MainWindow() = default;

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
    const QFileInfo info(path);
    std::unique_ptr<ImageSource> source;
    int initialIndex = 0;

    if (info.isDir()) {
        source = std::make_unique<FolderSource>(path);
    } else if (ImageSource::isArchive(path)) {
        source = std::make_unique<ArchiveSource>(path);
    } else if (info.isFile()) {
        auto folder = std::make_unique<FolderSource>(info.absolutePath());
        initialIndex = std::max(0, folder->indexOf(info.absoluteFilePath()));
        source = std::move(folder);
    } else {
        statusBar()->showMessage(tr("路径不存在: %1").arg(path), 4000);
        return;
    }

    if (!source || source->count() == 0) {
        statusBar()->showMessage(tr("没有可显示的图片: %1").arg(path), 4000);
        return;
    }

    m_source = std::move(source);
    m_cache.clear();  // 索引含义随来源改变,旧缓存作废
    showIndex(initialIndex);
}

void MainWindow::showIndex(int index) {
    if (!m_source)
        return;
    const int n = m_source->count();
    if (n == 0)
        return;

    index = ((index % n) + n) % n;  // wrap around both ends
    m_index = index;

    QImage image = m_cache.get(index);
    if (image.isNull()) {
        image = QImage::fromData(m_source->readEntry(index));
        if (image.isNull()) {
            statusBar()->showMessage(tr("无法解码: %1").arg(m_source->entryName(index)), 4000);
            return;
        }
        m_cache.insert(index, image);
    }

    m_view->setImage(image);
    const QString name = m_source->entryName(index);
    setWindowTitle(tr("%1  (%2/%3) — ImageViewer").arg(name).arg(index + 1).arg(n));
    statusBar()->showMessage(
        tr("%1 × %2  ·  %3/%4").arg(image.width()).arg(image.height()).arg(index + 1).arg(n));
}

void MainWindow::next() {
    showIndex(m_index + 1);
}

void MainWindow::prev() {
    showIndex(m_index - 1);
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
