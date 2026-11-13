#pragma once

#include <QColor>
#include <QGraphicsView>
#include <QVector>

#include "Document.h"
#include "Tools.h"

class VectorItem;

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);

    qreal zoom() const { return m_zoom; }
    QRectF pageRect() const;
    QSizeF pageSize() const { return m_pageSize; }
    bool showGrid() const { return m_grid; }

    QColor fillColor() const { return m_fill; }
    QColor strokeColor() const { return m_stroke; }
    qreal strokeWidth() const { return m_strokeWidth; }

    void setZoom(qreal zoom);
    void zoomAt(qreal zoom, const QPoint &viewportPos);
    void zoomStep(qreal factor);
    void fitPage();
    void actualSize();
    void zoomToSelection();
    void setShowGrid(bool on);
    void setActiveTool(Tool tool);

    void setPageSize(const QSizeF &size);
    void setFillColor(const QColor &color);
    void setStrokeColor(const QColor &color);
    void setStrokeWidth(qreal width);

    QList<VectorItem *> vectorItems() const;
    QList<VectorItem *> selectedVectorItems() const;
    VectorItem *addVectorObject(const VectorObject &object, bool record = true,
                                bool select = true);
    void setDocument(const Document &document);
    QVector<VectorObject> objects() const;

    void deleteSelectedObjects();
    void selectAllObjects();
    void editTextItem(VectorItem *item);

signals:
    void scenePosChanged(const QPointF &pos);
    void zoomChanged(qreal zoom);
    void statusHint(const QString &text);
    void beginEdit();
    void endEdit();
    void selectionChanged(int count);
    void colorPicked(const QColor &color);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    enum class DrawMode {
        None,
        Rectangle,
        Ellipse,
        Polygon,
        Star,
        Spiral,
        Line,
        Freehand,
        Artistic,
        Crop,
        Transparency
    };

    struct BezierNode {
        QPointF anchor;
        QPointF in;
        QPointF out;
    };

    void updateCursor();
    VectorItem *itemAtScene(const QPointF &scenePos) const;
    void finishDrawing(const QPointF &scenePos);
    void startDrawing(DrawMode mode, const QPointF &scenePos);

    void createTextAt(const QPointF &scenePos);
    void applyFillAt(const QPointF &scenePos);
    void applyOutlineAt(const QPointF &scenePos);
    void toggleShadowAt(const QPointF &scenePos);
    void blendSelected();
    void cropTo(const QRectF &rect);

    void addBezierNode(const QPointF &scenePos);
    void finishBezier();
    void cancelBezier();
    QPainterPath buildBezierPath() const;
    QPainterPath buildFreehandPath(const QVector<QPointF> &points) const;

    bool nodePress(const QPointF &scenePos);
    void drawNodeOverlay(QPainter *painter);
    void drawPreview(QPainter *painter);

    qreal m_zoom = 1.0;
    QSizeF m_pageSize = QSizeF(794.0, 1123.0);
    bool m_grid = true;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_panAnchor;
    Tool m_tool = Tool::Select;

    QColor m_fill = QColor("#4f7cc0");
    QColor m_stroke = QColor("#1d2127");
    qreal m_strokeWidth = 2.0;

    DrawMode m_drawMode = DrawMode::None;
    bool m_drawing = false;
    QPointF m_drawStart;
    QPointF m_drawCurrent;
    QVector<QPointF> m_freehandPoints;

    bool m_bezierActive = false;
    bool m_bezierDragging = false;
    QVector<BezierNode> m_bezierNodes;

    bool m_nodeDragging = false;
    VectorItem *m_nodeItem = nullptr;
    int m_nodeElement = -1;

    bool m_transparencyDragging = false;
    VectorItem *m_transparencyItem = nullptr;
    qreal m_transparencyStartValue = 1.0;
    qreal m_transparencyStartX = 0.0;

    qreal m_nextZ = 1.0;
};
