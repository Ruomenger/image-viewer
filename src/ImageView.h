#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QGraphicsView>
#include <QSize>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QImage;
class QMovie;
class QPixmap;

// 显示一页图片:滚轮缩放(跟随光标)、拖拽平移、适应窗口(DESIGN §4.3)。
// - 场景坐标 = 原图像素:显示图(可能是缩小过的 Fit 档)按「原图尺寸 / 图尺寸」放大,
//   铺满整个原图矩形,视图的缩放系数就是相对原图的缩放比例(1.0 = 原图 1:1)。
// - 同一页换成更清晰的图(Fit → Full)只换 pixmap,缩放与平移位置不动。
// - 放大到显示图的像素被拉伸显示(每个像素占超过 1 个设备像素)时发 needsSharperImage,
//   每张图最多一次;已是原图时不发。
// - 动画(GIF/WebP)由 QMovie 驱动帧更新,帧本身就是原图尺寸;换页时自动停止。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    // 新的一页:铺场景并适应窗口。originalSize 为空时按图本身尺寸。
    void setImage(const QImage& image, QSize originalSize);
    // 同一页换成更清晰的图:只换 pixmap。动画播放中忽略。
    void replaceImage(const QImage& image);
    // 新的一页且为多帧动画:firstFrame 先铺场景与适配(QMovie 失败也有可见内容),随后
    // 帧更新只替换 pixmap,缩放 / 平移状态不受影响。
    void setAnimation(const QByteArray& bytes, const QImage& firstFrame, QSize originalSize);
    void clear();  // 清空画面(如当前页解码失败)

    bool isAnimating() const { return m_movie != nullptr; }
    double zoom() const { return m_scale; }  // 相对原图的缩放比例

public slots:
    void fitToWindow();
    void actualSize();
    void zoomIn();
    void zoomOut();

signals:
    void zoomChanged(double zoom);
    void needsSharperImage();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void resetScene(const QImage& image, QSize originalSize);  // 换页:停动画、按原图铺场景
    void showPixmap(const QPixmap& pixmap);                    // 按场景(原图)矩形缩放放置
    void zoomBy(double factor);
    void zoomUpdated();     // 发 zoomChanged 并检查是否需要更清晰的图
    void checkSharpness();  // 显示图像素被拉伸时发 needsSharperImage
    void stopMovie();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_scale = 1.0;
    bool m_fitMode = true;
    bool m_sharperRequested = false;  // 当前图已发过 needsSharperImage

    QMovie* m_movie = nullptr;  // 仅动画条目存在;换页时销毁
    QBuffer m_movieBuffer;      // QMovie 的数据源,持有字节副本
};
