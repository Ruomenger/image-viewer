#pragma once

#include <QMainWindow>

#include <memory>

#include "cache/ImageCache.h"
#include "cache/Prefetcher.h"

class ImageView;
class ImageSource;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;  // out-of-line: ImageSource is incomplete here.

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
    void showIndex(int index);

    ImageView* m_view = nullptr;
    std::shared_ptr<ImageSource> m_source;  // shared:预读任务也会持有
    ImageCache m_cache;                     // 解码结果缓存,切换来源时清空
    Prefetcher m_prefetcher;                // 须声明在 m_cache 之后(析构先于 m_cache)
    int m_index = 0;
};
