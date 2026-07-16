#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QGraphicsView>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QImage;
class QMovie;

// Displays one image with smooth zoom (wheel, around the cursor) and
// drag-to-pan. Re-fits to the window while in "fit" mode.
// 动画(GIF/WebP):setAnimation 用 QMovie 驱动帧更新,换页时自动停止。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);

    // 播放多帧动画:firstFrame 先按静态图铺场景与适配(QMovie 失败也有
    // 可见内容),随后帧更新只替换 pixmap,缩放/平移状态不受影响。
    void setAnimation(const QByteArray& bytes, const QImage& firstFrame);

public slots:
    void fitToWindow();
    void actualSize();
    void zoomIn();
    void zoomOut();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void zoomBy(double factor);
    void stopMovie();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_scale = 1.0;
    bool m_fitMode = true;

    QMovie* m_movie = nullptr;  // 仅动画条目存在;换页时销毁
    QBuffer m_movieBuffer;      // QMovie 的数据源,持有字节副本
};
