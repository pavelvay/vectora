#include "Effects.h"

#include <QPainter>
#include <QTransform>
#include <QtMath>

#include <algorithm>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

QImage transparentImage(const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QImage shifted(const QImage &source, const QPointF &delta)
{
    if (qAbs(delta.x()) < 0.5 && qAbs(delta.y()) < 0.5)
        return source;
    QImage out = transparentImage(source.size());
    QPainter painter(&out);
    painter.drawImage(delta, source);
    painter.end();
    return out;
}

QImage tinted(const QImage &source, const QColor &color, qreal opacity)
{
    const qreal strength = qBound(0.0, opacity, 1.0) * color.alphaF();
    if (strength <= 0.001)
        return transparentImage(source.size());

    QImage out = transparentImage(source.size());
    for (int y = 0; y < source.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const int alpha = int(qAlpha(line[x]) * strength);
            outLine[x] = qRgba(color.red() * alpha / 255,
                               color.green() * alpha / 255,
                               color.blue() * alpha / 255, alpha);
        }
    }
    return out;
}

QImage compositeUnder(const QImage &top, const QImage &under)
{
    QImage out = under.copy();
    QPainter painter(&out);
    painter.drawImage(0, 0, top);
    painter.end();
    return out;
}

QImage compositeOver(const QImage &base, const QImage &over)
{
    QImage out = base.copy();
    QPainter painter(&out);
    painter.drawImage(0, 0, over);
    painter.end();
    return out;
}

QImage clippedByAlpha(QImage image, const QImage &mask)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *maskLine = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int factor = qAlpha(maskLine[x]);
            if (factor >= 255)
                continue;
            const int a = qAlpha(line[x]) * factor / 255;
            line[x] = qRgba(qRed(line[x]) * factor / 255,
                            qGreen(line[x]) * factor / 255,
                            qBlue(line[x]) * factor / 255, a);
        }
    }
    return image;
}

qreal angleRadians(qreal degrees)
{
    return degrees * kPi / 180.0;
}

} // namespace

QStringList effectTypeList()
{
    return {
        QStringLiteral("shadow"),       QStringLiteral("inner-shadow"),
        QStringLiteral("glow"),         QStringLiteral("inner-glow"),
        QStringLiteral("blur"),         QStringLiteral("motion-blur"),
        QStringLiteral("offset-path"),  QStringLiteral("distortion"),
        QStringLiteral("warp"),         QStringLiteral("extrude"),
        QStringLiteral("perspective"),  QStringLiteral("clip-mask"),
        QStringLiteral("opacity-mask"),
    };
}

QString effectDisplayName(const QString &type)
{
    if (type == QLatin1String("shadow"))
        return QStringLiteral("Тень");
    if (type == QLatin1String("inner-shadow"))
        return QStringLiteral("Внутренняя тень");
    if (type == QLatin1String("glow"))
        return QStringLiteral("Свечение");
    if (type == QLatin1String("inner-glow"))
        return QStringLiteral("Внутреннее свечение");
    if (type == QLatin1String("blur"))
        return QStringLiteral("Размытие");
    if (type == QLatin1String("motion-blur"))
        return QStringLiteral("Размытие в движении");
    if (type == QLatin1String("offset-path"))
        return QStringLiteral("Смещённый контур");
    if (type == QLatin1String("distortion"))
        return QStringLiteral("Искажение");
    if (type == QLatin1String("warp"))
        return QStringLiteral("Деформация");
    if (type == QLatin1String("extrude"))
        return QStringLiteral("Экструзия");
    if (type == QLatin1String("perspective"))
        return QStringLiteral("Перспектива");
    if (type == QLatin1String("clip-mask"))
        return QStringLiteral("Обтравочная маска");
    if (type == QLatin1String("opacity-mask"))
        return QStringLiteral("Маска прозрачности");
    return type;
}

Effect makeEffect(const QString &type)
{
    Effect effect;
    effect.type = type;
    if (type == QLatin1String("shadow")) {
        effect.color = QColor(0, 0, 0, 190);
        effect.offset = QPointF(6.0, 6.0);
        effect.radius = 8.0;
        effect.opacity = 0.75;
    } else if (type == QLatin1String("inner-shadow")) {
        effect.color = QColor(0, 0, 0, 200);
        effect.offset = QPointF(4.0, 4.0);
        effect.radius = 6.0;
        effect.opacity = 0.8;
    } else if (type == QLatin1String("glow")) {
        effect.color = QColor(0x00, 0xe0, 0xb0);
        effect.radius = 14.0;
        effect.intensity = 1.2;
        effect.opacity = 0.9;
    } else if (type == QLatin1String("inner-glow")) {
        effect.color = QColor(0x00, 0xe0, 0xb0);
        effect.radius = 10.0;
        effect.intensity = 1.0;
        effect.opacity = 0.8;
    } else if (type == QLatin1String("blur")) {
        effect.radius = 6.0;
    } else if (type == QLatin1String("motion-blur")) {
        effect.radius = 12.0;
        effect.angle = 0.0;
    } else if (type == QLatin1String("offset-path")) {
        effect.color = QColor(0x1d, 0x21, 0x27);
        effect.distance = 6.0;
        effect.opacity = 1.0;
    } else if (type == QLatin1String("distortion")) {
        effect.radius = 28.0;
        effect.amount = 6.0;
        effect.angle = 0.0;
    } else if (type == QLatin1String("warp")) {
        effect.amount = 0.35;
        effect.radius = 0.0;
        effect.center = QPointF(0.5, 0.5);
    } else if (type == QLatin1String("extrude")) {
        effect.color = QColor(0x3d, 0x52, 0x66);
        effect.distance = 20.0;
        effect.angle = 90.0;
        effect.amount = 0.2;
        effect.intensity = 0.65;
        effect.center = QPointF(0.5, 0.15);
    } else if (type == QLatin1String("perspective")) {
        effect.quad = QPolygonF({QPointF(0, 0), QPointF(1, 0),
                                 QPointF(1, 1), QPointF(0, 1)});
        effect.amount = 0.0;
    } else if (type == QLatin1String("clip-mask")
               || type == QLatin1String("opacity-mask")) {
        effect.opacity = 1.0;
    }
    return effect;
}

QRectF effectBounds(const QVector<Effect> &effects, const QRectF &bounds)
{
    qreal margin = 0.0;
    QRectF result = bounds;

    for (const Effect &effect : effects) {
        if (!effect.enabled)
            continue;
        if (effect.type == QLatin1String("shadow")
            || effect.type == QLatin1String("glow")) {
            margin = qMax(margin, std::hypot(effect.offset.x(), effect.offset.y())
                                      + effect.radius * 2.0
                                            * qMax(1.0, effect.intensity));
        } else if (effect.type == QLatin1String("inner-shadow")
                   || effect.type == QLatin1String("inner-glow")) {
            margin = qMax(margin, effect.radius * 2.0);
        } else if (effect.type == QLatin1String("blur")) {
            margin = qMax(margin, effect.radius * 3.0);
        } else if (effect.type == QLatin1String("motion-blur")) {
            margin = qMax(margin, effect.radius * 2.0);
        } else if (effect.type == QLatin1String("offset-path")) {
            margin = qMax(margin, effect.distance * 2.0);
        } else if (effect.type == QLatin1String("distortion")) {
            margin = qMax(margin, effect.amount * 3.0);
        } else if (effect.type == QLatin1String("warp")) {
            margin = qMax(margin, effect.amount * qMax(bounds.width(), bounds.height()) * 0.5);
        } else if (effect.type == QLatin1String("extrude")) {
            margin = qMax(margin, effect.distance * 2.0);
        }
    }

    result = bounds.adjusted(-margin - 2.0, -margin - 2.0,
                             margin + 2.0, margin + 2.0);

    for (const Effect &effect : effects) {
        if (!effect.enabled || effect.type != QLatin1String("perspective"))
            continue;
        if (effect.quad.size() == 4) {
            QPolygonF source({bounds.topLeft(), bounds.topRight(),
                              bounds.bottomRight(), bounds.bottomLeft()});
            QPolygonF target;
            for (const QPointF &point : effect.quad) {
                target.append(QPointF(bounds.left() + point.x() * bounds.width(),
                                      bounds.top() + point.y() * bounds.height()));
            }
            QTransform transform;
            if (QTransform::quadToQuad(source, target, transform)) {
                QPolygonF corners({result.topLeft(), result.topRight(),
                                   result.bottomRight(), result.bottomLeft()});
                const QRectF mapped = transform.map(corners).boundingRect();
                result = result.united(mapped);
            }
        }
    }
    return result;
}

QImage effectBlurImage(const QImage &source, qreal radius)
{
    if (radius <= 0.3 || source.isNull())
        return source;
    const qreal factor = qMax<qreal>(1.0, radius / 1.5);
    const QSize small(qMax(1, int(source.width() / factor)),
                      qMax(1, int(source.height() / factor)));
    if (small == source.size())
        return source;
    const QImage down = source.scaled(small, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
    return down.scaled(source.size(), Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
}

namespace {

QImage directionalBlur(const QImage &source, qreal radius, qreal angle)
{
    const int steps = qBound(2, int(qMax<qreal>(2.0, radius)), 32);
    QImage out = transparentImage(source.size());
    QPainter painter(&out);
    painter.setOpacity(1.0 / steps);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    const qreal radians = angleRadians(angle);
    const QPointF direction(std::cos(radians), std::sin(radians));
    for (int i = 0; i < steps; ++i) {
        const qreal t = steps == 1 ? 0.0 : (qreal(i) / (steps - 1) - 0.5);
        painter.drawImage(direction * (radius * t), source);
    }
    painter.end();
    return out;
}

QImage distortImage(const QImage &source, qreal amount, qreal period, qreal angle)
{
    if (source.isNull() || qAbs(amount) < 0.1)
        return source;
    const qreal radians = angleRadians(angle);
    const QPointF normal(-std::sin(radians), std::cos(radians));
    const qreal safePeriod = qMax<qreal>(8.0, period);
    QImage out = transparentImage(source.size());
    for (int y = 0; y < source.height(); ++y) {
        QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const qreal wave = std::sin((x * std::cos(radians)
                                         + y * std::sin(radians))
                                        * 2.0 * kPi / safePeriod);
            const int sx = qBound(0, x + int(normal.x() * amount * wave),
                                  source.width() - 1);
            const int sy = qBound(0, y + int(normal.y() * amount * wave),
                                  source.height() - 1);
            const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(sy));
            outLine[x] = line[sx];
        }
    }
    return out;
}

QImage warpImage(const QImage &source, qreal amount, const QPointF &center)
{
    if (source.isNull() || qAbs(amount) < 0.01)
        return source;
    const QPointF c(center.x() * source.width(), center.y() * source.height());
    const qreal maxDistance = std::hypot(qMax(c.x(), source.width() - c.x()),
                                         qMax(c.y(), source.height() - c.y()));
    QImage out = transparentImage(source.size());
    for (int y = 0; y < source.height(); ++y) {
        QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const QPointF delta(x - c.x(), y - c.y());
            const qreal distance = std::hypot(delta.x(), delta.y());
            const qreal factor = 1.0 + amount * (1.0 - distance / qMax<qreal>(1.0, maxDistance));
            const int sx = qBound(0, int(c.x() + delta.x() / qMax<qreal>(0.05, factor)),
                                  source.width() - 1);
            const int sy = qBound(0, int(c.y() + delta.y() / qMax<qreal>(0.05, factor)),
                                  source.height() - 1);
            const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(sy));
            outLine[x] = line[sx];
        }
    }
    return out;
}

QImage maskLayer(const QRectF &rect, qreal scale, const QPainterPath &mask)
{
    QImage out(transparentImage(QSize(qMax(1, int(rect.width() * scale)),
                                      qMax(1, int(rect.height() * scale)))));
    if (mask.isEmpty())
        return out;
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    painter.translate(-rect.left(), -rect.top());
    painter.fillPath(mask, Qt::white);
    painter.end();
    return out;
}

QImage extrudeLayer(const QImage &source, const QRectF &rect, qreal scale,
                    const QPainterPath &geometry, const Effect &effect,
                    const QRectF &bounds)
{
    QImage out = transparentImage(source.size());
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    painter.translate(-rect.left(), -rect.top());

    QPainterPath path = geometry;
    if (path.isEmpty())
        path.addRect(bounds);

    const QPointF vanishing = bounds.topLeft()
                              + QPointF(bounds.width() * effect.center.x(),
                                        bounds.height() * effect.center.y());
    const qreal radians = angleRadians(effect.angle);
    const QPointF direction(std::cos(radians), std::sin(radians));
    const qreal perspective = qBound(0.0, effect.amount, 0.95);

    const auto farPoint = [&](const QPointF &point) {
        QPointF result = point + direction * effect.distance;
        if (perspective > 0.001)
            result = vanishing + (result - vanishing) * (1.0 - perspective * 0.6);
        return result;
    };

    const QPointF light = QPointF(-0.45, -0.89);
    const QList<QPolygonF> polygons = path.toSubpathPolygons();
    for (const QPolygonF &polygon : polygons) {
        if (polygon.size() < 2)
            continue;
        // The far face closes the volume.
        QPolygonF farFace;
        for (const QPointF &point : polygon)
            farFace.append(farPoint(point));
        painter.setPen(Qt::NoPen);
        QColor farColor = effect.color.darker(135);
        farColor.setAlphaF(qBound(0.0, effect.opacity, 1.0));
        painter.setBrush(farColor);
        painter.drawPolygon(farFace);

        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF p1 = polygon.at(i);
            const QPointF p2 = polygon.at((i + 1) % polygon.size());
            const QPointF f1 = farPoint(p1);
            const QPointF f2 = farPoint(p2);
            const QPointF edge = p2 - p1;
            const qreal length = std::hypot(edge.x(), edge.y());
            if (length < 0.001)
                continue;
            const QPointF normal(-edge.y() / length, edge.x() / length);
            const qreal diffuse = qBound(-1.0, normal.x() * light.x()
                                                  + normal.y() * light.y(), 1.0);
            const qreal shade = 0.55 + 0.45 * diffuse;
            const qreal lighting = 0.35 + 0.65 * qBound(0.0, effect.intensity, 1.0);
            QColor side = QColor::fromRgbF(
                qBound(0.0, effect.color.redF() * shade * lighting, 1.0),
                qBound(0.0, effect.color.greenF() * shade * lighting, 1.0),
                qBound(0.0, effect.color.blueF() * shade * lighting, 1.0));
            side.setAlphaF(qBound(0.0, effect.opacity, 1.0));
            QPolygonF quad;
            quad << p1 << p2 << f2 << f1;
            painter.setBrush(side);
            painter.drawPolygon(quad);
        }
    }
    painter.end();

    return compositeUnder(source, out);
}

EffectLayer applyPerspective(const EffectLayer &layer, const Effect &effect,
                             const QRectF &bounds, qreal scale)
{
    if (effect.quad.size() != 4)
        return layer;

    QPolygonF source({bounds.topLeft(), bounds.topRight(),
                      bounds.bottomRight(), bounds.bottomLeft()});
    QPolygonF target;
    for (const QPointF &point : effect.quad) {
        target.append(QPointF(bounds.left() + point.x() * bounds.width(),
                              bounds.top() + point.y() * bounds.height()));
    }

    QTransform transform;
    if (!QTransform::quadToQuad(source, target, transform))
        return layer;

    const QPolygonF corners({layer.rect.topLeft(), layer.rect.topRight(),
                             layer.rect.bottomRight(), layer.rect.bottomLeft()});
    const QRectF targetRect = transform.map(corners).boundingRect();
    if (targetRect.width() < 1.0 || targetRect.height() < 1.0)
        return layer;

    QImage out(transparentImage(QSize(qMax(1, int(targetRect.width() * scale)),
                                      qMax(1, int(targetRect.height() * scale)))));
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setTransform(transform);
    painter.translate(-targetRect.left(), -targetRect.top());
    painter.scale(scale, scale);
    painter.drawImage(layer.rect, layer.image);
    painter.end();

    EffectLayer result;
    result.image = out;
    result.rect = targetRect;
    return result;
}

} // namespace

EffectLayer applyEffectStack(const EffectLayer &base,
                             const QVector<Effect> &effects,
                             const QRectF &localBounds,
                             qreal scale,
                             const QPainterPath &geometry)
{
    EffectLayer layer = base;
    const qreal safeScale = qMax<qreal>(0.25, scale);

    for (const Effect &effect : effects) {
        if (!effect.enabled)
            continue;

        const QString type = effect.type;
        if (type == QLatin1String("shadow")) {
            const QPointF offset = effect.offset * safeScale;
            QImage shadow = tinted(layer.image, effect.color, effect.opacity);
            shadow = shifted(shadow, offset);
            shadow = effectBlurImage(shadow, effect.radius * safeScale);
            layer.image = compositeUnder(layer.image, shadow);
        } else if (type == QLatin1String("glow")) {
            const qreal strength = qBound(0.0, effect.opacity, 1.0)
                                   * qMax<qreal>(0.1, effect.intensity);
            QImage glow = tinted(layer.image, effect.color, strength);
            glow = effectBlurImage(glow, effect.radius * safeScale);
            if (effect.intensity > 1.0) {
                glow = compositeOver(glow, tinted(glow, effect.color,
                                                  (effect.intensity - 1.0) * 0.5));
            }
            layer.image = compositeUnder(layer.image, glow);
        } else if (type == QLatin1String("inner-shadow")
                   || type == QLatin1String("inner-glow")) {
            QImage inverse = transparentImage(layer.image.size());
            for (int y = 0; y < layer.image.height(); ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(layer.image.constScanLine(y));
                QRgb *outLine = reinterpret_cast<QRgb *>(inverse.scanLine(y));
                for (int x = 0; x < layer.image.width(); ++x) {
                    const int alpha = 255 - qAlpha(line[x]);
                    outLine[x] = qRgba(0, 0, 0, alpha);
                }
            }
            QImage shadow = tinted(inverse, effect.color,
                                   qBound(0.0, effect.opacity, 1.0)
                                       * qMax<qreal>(0.1, effect.intensity));
            const QPointF offset = type == QLatin1String("inner-shadow")
                                       ? effect.offset * safeScale : QPointF();
            shadow = shifted(shadow, offset);
            shadow = effectBlurImage(shadow, effect.radius * safeScale);
            shadow = clippedByAlpha(shadow, layer.image);
            layer.image = compositeOver(layer.image, shadow);
        } else if (type == QLatin1String("blur")) {
            layer.image = effectBlurImage(layer.image, effect.radius * safeScale);
        } else if (type == QLatin1String("motion-blur")) {
            layer.image = directionalBlur(layer.image, effect.radius * safeScale,
                                          effect.angle);
        } else if (type == QLatin1String("offset-path")) {
            const QImage silhouette = tinted(layer.image, effect.color,
                                             qBound(0.0, effect.opacity, 1.0));
            QImage outline = transparentImage(layer.image.size());
            QPainter painter(&outline);
            const int steps = qBound(8, int(qMax<qreal>(8.0, effect.distance * safeScale)), 32);
            for (int i = 0; i < steps; ++i) {
                const qreal radians = 2.0 * kPi * i / steps;
                const QPointF delta(std::cos(radians) * effect.distance * safeScale,
                                    std::sin(radians) * effect.distance * safeScale);
                painter.drawImage(delta, silhouette);
            }
            painter.end();
            layer.image = compositeUnder(layer.image, outline);
        } else if (type == QLatin1String("distortion")) {
            layer.image = distortImage(layer.image, effect.amount * safeScale,
                                       effect.radius * safeScale, effect.angle);
        } else if (type == QLatin1String("warp")) {
            layer.image = warpImage(layer.image, effect.amount, effect.center);
        } else if (type == QLatin1String("extrude")) {
            layer.image = extrudeLayer(layer.image, layer.rect, safeScale,
                                       geometry, effect, localBounds);
        } else if (type == QLatin1String("perspective")) {
            layer = applyPerspective(layer, effect, localBounds, safeScale);
        } else if (type == QLatin1String("clip-mask")
                   || type == QLatin1String("opacity-mask")) {
            if (effect.mask.isEmpty())
                continue;
            QImage mask = maskLayer(layer.rect, safeScale, effect.mask);
            if (type == QLatin1String("clip-mask")) {
                layer.image = clippedByAlpha(layer.image, mask);
            } else {
                const qreal strength = qBound(0.0, effect.opacity, 1.0);
                for (int y = 0; y < layer.image.height(); ++y) {
                    const QRgb *maskLine = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
                    QRgb *line = reinterpret_cast<QRgb *>(layer.image.scanLine(y));
                    for (int x = 0; x < layer.image.width(); ++x) {
                        const int factor = int(qAlpha(maskLine[x]) * strength);
                        const int a = qAlpha(line[x]) * factor / 255;
                        line[x] = qRgba(qRed(line[x]) * factor / 255,
                                        qGreen(line[x]) * factor / 255,
                                        qBlue(line[x]) * factor / 255, a);
                    }
                }
            }
        }
    }
    return layer;
}
