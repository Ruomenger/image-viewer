#include "ImageView.h"

#include <QColor>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QMovie>
#include <QPainter>
#include <QPixmap>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kMinScale = 0.02;
constexpr double kMaxScale = 50.0;
// 浮点误差容限:适应窗口时 Fit 档(屏幕物理像素)恰好 1:1,不应误判为被拉伸。
constexpr double kStretchTolerance = 1.01;
}  // namespace

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent), m_scene(new QGraphicsScene(this)) {
    setScene(m_scene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QColor(30, 30, 30));
    setFrameShape(QFrame::NoFrame);
}

void ImageView::setImage(const QImage& image, QSize originalSize) {
    resetScene(image, originalSize);
    fitToWindow();
}

void ImageView::resetScene(const QImage& image, QSize originalSize) {
    stopMovie();
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap());
    // 图元默认 FastTransformation,会盖过视图的 SmoothPixmapTransform 提示。
    m_item->setTransformationMode(Qt::SmoothTransformation);
    const QSize original = originalSize.isEmpty() ? image.size() : originalSize;
    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(original)));
    m_sharperRequested = false;
    showPixmap(QPixmap::fromImage(image));
}

void ImageView::replaceImage(const QImage& image) {
    if (m_item == nullptr || m_movie != nullptr)
        return;
    showPixmap(QPixmap::fromImage(image));
    m_sharperRequested = false;
    checkSharpness();  // 换上的若仍不是原图(如 Fit 目标调大),放大时还能再要
}

void ImageView::setAnimation(const QByteArray& bytes, const QImage& firstFrame,
                             QSize originalSize) {
    resetScene(firstFrame, originalSize);  // 先铺静态首帧(并停掉上一个动画),QMovie 失败也有内容

    m_movieBuffer.setData(bytes);
    if (m_movieBuffer.open(QIODevice::ReadOnly)) {
        m_movie = new QMovie(&m_movieBuffer, QByteArray(), this);
        m_movie->setCacheMode(QMovie::CacheAll);  // 循环播放需回绕到首帧
        connect(m_movie, &QMovie::frameChanged, this, [this] {
            if (m_item != nullptr && m_movie != nullptr)
                showPixmap(m_movie->currentPixmap());
        });
    }
    fitToWindow();  // movie 建好之后再适配:动画帧是原图尺寸,不必要更清晰的图
    if (m_movie != nullptr)
        m_movie->start();
}

void ImageView::clear() {
    stopMovie();
    m_scene->clear();
    m_item = nullptr;
    m_scene->setSceneRect(QRectF());
}

void ImageView::showPixmap(const QPixmap& pixmap) {
    m_item->setPixmap(pixmap);
    if (pixmap.isNull())
        return;
    const QRectF rect = m_scene->sceneRect();
    m_item->setTransform(
        QTransform::fromScale(rect.width() / pixmap.width(), rect.height() / pixmap.height()));
}

void ImageView::stopMovie() {
    if (m_movie != nullptr) {
        m_movie->stop();
        delete m_movie;
        m_movie = nullptr;
    }
    if (m_movieBuffer.isOpen())
        m_movieBuffer.close();
}

void ImageView::fitToWindow() {
    if (!m_item)
        return;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_scale = transform().m11();
    m_fitMode = true;
    zoomUpdated();
}

void ImageView::actualSize() {
    if (!m_item)
        return;
    resetTransform();
    m_scale = 1.0;
    m_fitMode = false;
    zoomUpdated();
}

void ImageView::zoomIn() {
    zoomBy(kZoomStep);
}

void ImageView::zoomOut() {
    zoomBy(1.0 / kZoomStep);
}

void ImageView::zoomBy(double factor) {
    if (!m_item)
        return;
    // 越界时贴到边界而不是直接拒绝,连续缩放才能真正到达极限值。
    const double target = std::clamp(m_scale * factor, kMinScale, kMaxScale);
    if (qFuzzyCompare(target, m_scale))
        return;
    const double applied = target / m_scale;
    m_scale = target;
    m_fitMode = false;
    scale(applied, applied);
    zoomUpdated();
}

void ImageView::zoomUpdated() {
    emit zoomChanged(m_scale);
    checkSharpness();
}

void ImageView::checkSharpness() {
    if (m_sharperRequested || m_item == nullptr || m_movie != nullptr)
        return;
    const QPixmap pixmap = m_item->pixmap();
    const double sceneWidth = m_scene->sceneRect().width();
    if (pixmap.isNull() || pixmap.width() >= sceneWidth)
        return;  // 已是原图
    // 每个显示图像素占多少设备像素:视图缩放 × 图元放大 × 设备像素比。
    const double stretch = m_scale * (sceneWidth / pixmap.width()) * devicePixelRatioF();
    if (stretch > kStretchTolerance) {
        m_sharperRequested = true;
        emit needsSharperImage();
    }
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    zoomBy(event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep);
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (m_fitMode)
        fitToWindow();
}
