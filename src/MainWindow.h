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

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void connectBrowser();
    void showPage(Page page, const QList<DecodedImage>& images);
    QString pageLabel(Page page) const;  // 「文件名 (序号/总数)」

    ImageView* m_view = nullptr;
    Browser m_browser;      // 导航 / 缓存 / 解码调度都在 viewer-core 的 Browser 里,全部异步
    QString m_openingPath;  // 最近一次 openPath 的路径,用于报错
};
