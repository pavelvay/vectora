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
    void setGradientFill(int style, const QColor &second, qreal angle);
    void setGradientDefinition(int style, const QVector<GradientStop> &stops,
                               qreal angle, const QPointF &center, qreal radius);
    void setMeshColors(const QColor &topRight, const QColor &bottomLeft,
                       const QColor &bottomRight);
    void setStrokeColor(const QColor &color);
    void setStrokeWidth(qreal width);
    void setStrokeStyle(int capStyle, int joinStyle, qreal miterLimit,
                        const QVector<qreal> &dashPattern);
    void setObjectOpacity(qreal opacity);
    void setFillOpacity(qreal opacity);
    void setStrokeOpacity(qreal opacity);
    void setBlendMode(int mode);
    void setShadowEnabled(bool on);
    void setShadowOffset(const QPointF &offset);
    void setShadowOpacity(qreal opacity);
    void setShadowColor(const QColor &color);
    void setFrameSize(const QSizeF &size);
    void setTextFont(const QFont &font);
    void setTextAlignment(int align);
    void setLetterSpacing(qreal spacing);
    void setLineSpacing(qreal spacing);
    void setRotationAngle(qreal degrees);
    void setCornerRadius(qreal radius);
    void setSides(int sides);
    void setStarPoints(int points);
    void setStarRatio(qreal ratio);
    void setSpiralTurns(qreal turns);
    bool flip(bool horizontal);
    void setLocked(bool locked);
    bool applySkew(qreal angleX, qreal angleY);
    void setPivot(const QPointF &normalised);
    QPointF pivotPoint() const;
    void setSnapSettings(bool enabled, qreal step, const QVector<qreal> &guidesX,
                         const QVector<qreal> &guidesY, qreal threshold);
    bool isLocked() const { return m_object.locked; }
    int shapeTypeValue() const { return int(m_object.type); }
    void setPath(const QPainterPath &path);

    QVector<QPainterPath::Element> pathElements() const;
    void setPathElements(const QVector<QPainterPath::Element> &elements);
    void setLayerIndex(int layer) { m_object.layer = layer; }
    void setGroupIndex(int group) { m_object.group = group; }
    int groupIndex() const { return m_object.group; }
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
    enum class Interaction { None, Move, Resize, Rotate, Pivot };

    int handleAt(const QPointF &pos) const;
    QVector<QPointF> handlePoints() const;
    void applyResize(const QPointF &pos, bool keepAspect);
    void updateOrigin();
    void drawSelection(QPainter *painter) const;
    void applySnapping();
    void updateTextBoxHeight();

    VectorObject m_object;
    bool m_snapEnabled = false;
    qreal m_snapStep = 20.0;
    qreal m_snapThreshold = 6.0;
    QVector<qreal> m_snapGuidesX;
    QVector<qreal> m_snapGuidesY;
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
