#pragma once

#include <QMainWindow>

#include "browse/Browser.h"

class ImageView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Open an image file, a folder, or an archive. The single entry point;
    // also used for command-line argument and drag & drop.
    void openPath(const QString& path);

public slots:
    void openFile();
    void openFolder();
    void openArchive();
    void next();
    void prev();
    void toggleFullScreen();  // 保留进入前的最大化状态

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void connectBrowser();
    void showPage(Page page, const QList<DecodedImage>& images);
    void updateFitTarget();         // Fit 档 = 所在屏幕的物理像素
    void updateFullScreenChrome();  // 按窗口实际状态显隐菜单栏 / 状态栏
    void updateStatus();
    QString pageLabel(Page page) const;  // 「文件名 (序号/总数)」

    ImageView* m_view = nullptr;
    QAction* m_fullScreenAction = nullptr;
    QAction* m_exitFullScreenAction = nullptr;  // Esc,仅全屏时可用
    Browser m_browser;      // 导航 / 缓存 / 解码调度都在 viewer-core 的 Browser 里,全部异步
    QString m_openingPath;  // 最近一次 openPath 的路径,用于报错
    Page m_currentPage;     // 已切到的页(pageChanged)
    Page m_shownPage;       // 画面上显示的页(pageReady);图未到时与 m_currentPage 不同
    QSize m_shownOriginal;  // 显示页的原图尺寸
};
