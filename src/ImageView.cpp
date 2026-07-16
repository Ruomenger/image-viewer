#include "ImageView.h"

#include <QColor>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QMovie>
#include <QPainter>
#include <QPixmap>
#include <QWheelEvent>

#include <algorithm>

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kMinScale = 0.02;
constexpr double kMaxScale = 50.0;
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

void ImageView::setImage(const QImage& image) {
    stopMovie();
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(m_item->boundingRect());
    m_fitMode = true;
    fitToWindow();
}

void ImageView::setAnimation(const QByteArray& bytes, const QImage& firstFrame) {
    setImage(firstFrame);  // 铺场景 + 适配,并停掉上一个动画

    m_movieBuffer.setData(bytes);
    if (!m_movieBuffer.open(QIODevice::ReadOnly))
        return;  // 保底显示静态首帧

    m_movie = new QMovie(&m_movieBuffer, QByteArray(), this);
    m_movie->setCacheMode(QMovie::CacheAll);  // 循环播放需回绕到首帧
    connect(m_movie, &QMovie::frameChanged, this, [this] {
        if (m_item != nullptr && m_movie != nullptr)
            m_item->setPixmap(m_movie->currentPixmap());
    });
    m_movie->start();
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
    fitInView(m_item, Qt::KeepAspectRatio);
    m_scale = transform().m11();
    m_fitMode = true;
}

void ImageView::actualSize() {
    if (!m_item)
        return;
    resetTransform();
    m_scale = 1.0;
    m_fitMode = false;
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
