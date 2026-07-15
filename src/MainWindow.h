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
    void showCurrent();

    ImageView* m_view = nullptr;
    Browser m_browser;  // 导航 / 缓存 / 预读的编排都在 viewer-core 的 Browser 里
};
