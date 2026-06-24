#pragma once

#include <QGraphicsView>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QImage;

// Displays one image with smooth zoom (wheel, around the cursor) and
// drag-to-pan. Re-fits to the window while in "fit" mode.
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);

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

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_scale = 1.0;
    bool m_fitMode = true;
};
