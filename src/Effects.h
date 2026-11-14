#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

// A single non-destructive effect. Parameters are stored with the object and are
// applied only while painting, so the original geometry stays editable.
struct Effect {
    QString type = QStringLiteral("shadow");
    bool enabled = true;
    qreal opacity = 1.0;
    QColor color = QColor(0, 0, 0);
    QPointF offset = QPointF(6.0, 6.0);
    qreal radius = 8.0;
    qreal intensity = 1.0;
    qreal angle = 0.0;
    qreal distance = 0.0;
    qreal amount = 0.0;
    QPointF center = QPointF(0.5, 0.5); // normalised vanishing point / warp centre
    QPolygonF quad;                     // normalised 0..1 perspective corners
    QPainterPath mask;                  // clip / opacity mask in local coordinates
};

// UI helpers.
QStringList effectTypeList();
QString effectDisplayName(const QString &type);
Effect makeEffect(const QString &type);

// A rendered layer in local coordinates. `image` is rasterised at
// image.size() / rect.size() pixels per unit.
struct EffectLayer {
    QImage image;
    QRectF rect;
};

// Bounds in local coordinates including the area occupied by the effects.
QRectF effectBounds(const QVector<Effect> &effects, const QRectF &bounds);

// Apply the effect stack to a rendered layer.
EffectLayer applyEffectStack(const EffectLayer &base,
                             const QVector<Effect> &effects,
                             const QRectF &localBounds,
                             qreal scale,
                             const QPainterPath &geometry);

QImage effectBlurImage(const QImage &source, qreal radius);
