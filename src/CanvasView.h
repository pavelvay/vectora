#pragma once

#include <QGraphicsView>

#include "Tools.h"

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);

    qreal zoom() const { return m_zoom; }
    QRectF pageRect() const;
    bool showGrid() const { return m_grid; }

    void setZoom(qreal zoom);
    void zoomAt(qreal zoom, const QPoint &viewportPos);
    void zoomStep(qreal factor);
    void fitPage();
    void actualSize();
    void setShowGrid(bool on);
    void setActiveTool(Tool tool);

signals:
    void scenePosChanged(const QPointF &pos);
    void zoomChanged(qreal zoom);
    void statusHint(const QString &text);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    void updateCursor();

    qreal m_zoom = 1.0;
    bool m_grid = true;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_panAnchor;
    Tool m_tool = Tool::Select;
};
