#pragma once

#include <QGraphicsObject>
#include <QPainterPath>

#include "Document.h"

class VectorItem : public QGraphicsObject
{
    Q_OBJECT

public:
    enum { ItemType = UserType + 1 };

    explicit VectorItem(const VectorObject &object, QGraphicsItem *parent = nullptr);

    int type() const override { return ItemType; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    VectorObject object() const;
    void setObject(const VectorObject &object);
    void setText(const QString &text);
    void setFillColor(const QColor &color);
    void setStrokeColor(const QColor &color);
    void setStrokeWidth(qreal width);
    void setObjectOpacity(qreal opacity);
    void setShadowEnabled(bool on);
    void setRotationAngle(qreal degrees);
    void setPath(const QPainterPath &path);

    QVector<QPainterPath::Element> pathElements() const;
    void setPathElements(const QVector<QPainterPath::Element> &elements);
    QRectF localBounds() const { return m_object.localBounds(); }
    bool hasPath() const { return !m_object.path.isEmpty(); }

signals:
    void editStarted();
    void editFinished();
    void textEditRequested(VectorItem *item);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    enum class Interaction { None, Move, Resize, Rotate };

    int handleAt(const QPointF &pos) const;
    QVector<QPointF> handlePoints() const;
    void applyResize(const QPointF &pos);
    void updateOrigin();
    void drawSelection(QPainter *painter) const;

    VectorObject m_object;
    Interaction m_interaction = Interaction::None;
    int m_handle = -1;
    QPointF m_pressScenePos;
    QPointF m_rotateCenter;
    qreal m_startRotation = 0.0;
    qreal m_startAngle = 0.0;
    QPainterPath m_startPath;
    QRectF m_startBounds;
    qreal m_startFontSize = 24.0;
};
