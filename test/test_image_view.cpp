#include <QtTest>

#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QSignalSpy>

#include "ImageView.h"

namespace {

QImage solid(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    return image;
}

QGraphicsPixmapItem* pixmapItem(const ImageView& view) {
    const QList<QGraphicsItem*> items = view.scene()->items();
    return items.isEmpty() ? nullptr : qgraphicsitem_cast<QGraphicsPixmapItem*>(items.first());
}

QPointF viewCenterInScene(const ImageView& view) {
    return view.mapToScene(view.viewport()->rect().center());
}

}  // namespace

class TestImageView : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void sceneUsesOriginalPixels();
    void replaceImageKeepsZoomAndPan();
    void stretchedImageRequestsSharperOnce();
    void originalNeverRequestsSharper();
    void animationFramesFillOriginalRect();
    void clearRemovesPicture();

private:
    ImageView* m_view = nullptr;
};

void TestImageView::init() {
    m_view = new ImageView;
    m_view->resize(400, 300);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
    QCOMPARE(m_view->devicePixelRatioF(), 1.0);  // 离屏平台;下面的拉伸判定按 1 计算
}

void TestImageView::cleanup() {
    delete m_view;
    m_view = nullptr;
}

void TestImageView::sceneUsesOriginalPixels() {
    m_view->setImage(solid(400, 200), QSize(1000, 500));  // Fit 档,原图 1000×500

    QCOMPARE(m_view->sceneRect(), QRectF(0, 0, 1000, 500));
    QGraphicsPixmapItem* item = pixmapItem(*m_view);
    QVERIFY(item != nullptr);
    QCOMPARE(item->sceneBoundingRect(), QRectF(0, 0, 1000, 500));  // 放大铺满原图矩形
    QVERIFY(m_view->zoom() < 0.4);  // 适应 400×300 的窗口:相对原图约 0.4

    m_view->actualSize();
    QCOMPARE(m_view->zoom(), 1.0);  // 1.0 = 原图 1:1
}

void TestImageView::replaceImageKeepsZoomAndPan() {
    m_view->setImage(solid(400, 200), QSize(1000, 500));
    m_view->zoomIn();
    m_view->zoomIn();
    m_view->centerOn(QPointF(700, 300));
    const QTransform transform = m_view->transform();
    const QPointF center = viewCenterInScene(*m_view);

    m_view->replaceImage(solid(1000, 500));  // Fit → Full

    QCOMPARE(m_view->transform(), transform);
    QCOMPARE(viewCenterInScene(*m_view), center);
    QGraphicsPixmapItem* item = pixmapItem(*m_view);
    QCOMPARE(item->pixmap().size(), QSize(1000, 500));
    QCOMPARE(item->sceneBoundingRect(), QRectF(0, 0, 1000, 500));
}

void TestImageView::stretchedImageRequestsSharperOnce() {
    QSignalSpy sharper(m_view, &ImageView::needsSharperImage);
    m_view->setImage(solid(400, 200), QSize(1000, 500));
    QCOMPARE(sharper.count(), 0);  // 适应窗口:Fit 像素没被拉伸

    m_view->actualSize();  // 1 原图像素 = 1 设备像素:Fit 像素被拉伸 2.5 倍
    QCOMPARE(sharper.count(), 1);
    m_view->zoomIn();
    QCOMPARE(sharper.count(), 1);  // 每张图只要一次

    m_view->replaceImage(solid(1000, 500));
    m_view->zoomIn();
    QCOMPARE(sharper.count(), 1);  // 已是原图
}

void TestImageView::originalNeverRequestsSharper() {
    QSignalSpy sharper(m_view, &ImageView::needsSharperImage);
    m_view->setImage(solid(300, 200), QSize(300, 200));
    m_view->actualSize();
    m_view->zoomIn();
    m_view->zoomIn();
    QCOMPARE(sharper.count(), 0);
}

void TestImageView::animationFramesFillOriginalRect() {
    QFile file(QFINDTESTDATA("data/sample-anim.gif"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const QImage first = QImage::fromData(bytes);
    QVERIFY(!first.isNull());
    const QSize original = first.size();
    const QImage fitFrame = first.scaled(original / 2);  // 首帧是 Fit 档,帧来自 QMovie(原图尺寸)

    QSignalSpy sharper(m_view, &ImageView::needsSharperImage);
    m_view->setAnimation(bytes, fitFrame, original);
    QVERIFY(m_view->isAnimating());
    QTRY_COMPARE(pixmapItem(*m_view)->pixmap().size(), original);
    QCOMPARE(pixmapItem(*m_view)->sceneBoundingRect(), QRectF(QPointF(0, 0), QSizeF(original)));
    m_view->actualSize();
    QCOMPARE(sharper.count(), 0);  // 动画由 QMovie 按原尺寸播,不要原图
}

void TestImageView::clearRemovesPicture() {
    m_view->setImage(solid(400, 200), QSize(1000, 500));
    m_view->clear();
    QVERIFY(m_view->scene()->items().isEmpty());
    const double zoom = m_view->zoom();
    m_view->zoomIn();  // 没有图时缩放不生效、不崩溃
    QCOMPARE(m_view->zoom(), zoom);
}

QTEST_MAIN(TestImageView)
#include "test_image_view.moc"
