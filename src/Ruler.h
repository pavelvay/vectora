#pragma once

#include <QWidget>

class CanvasView;

class Ruler : public QWidget
{
    Q_OBJECT

public:
    explicit Ruler(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setView(CanvasView *view);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    qreal sceneCoordinate() const;
    Qt::Orientation m_orientation;
    CanvasView *m_view = nullptr;
    bool m_dragging = false;
    bool m_moved = false;
    QPoint m_pressGlobal;
};
