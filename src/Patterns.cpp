#include "Patterns.h"

#include <QJsonArray>
#include <QPainterPath>
#include <QtMath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

QRectF objectVisualBounds(const VectorObject &object)
{
    QRectF bounds = object.localBounds();
    bounds.translate(object.pos);
    return bounds;
}

QSizeF effectiveTile(const PatternSettings &settings)
{
    const qreal scale = qBound<qreal>(0.05, settings.scale, 20.0);
    return QSizeF(qMax<qreal>(4.0, settings.tile.width() * scale),
                  qMax<qreal>(4.0, settings.tile.height() * scale));
}

// Maps tile-local shape coordinates into the target area: uniform scale,
// rotation, optional mirroring and the tile's final size.
QTransform tileTransform(const PatternSettings &settings, const QSizeF &effective)
{
    const qreal scale = qBound<qreal>(0.05, settings.scale, 20.0);
    QTransform transform;
    transform.translate(effective.width() / 2.0, effective.height() / 2.0);
    transform.rotate(settings.angle);
    transform.scale(settings.mirrorX ? -1.0 : 1.0,
                    settings.mirrorY ? -1.0 : 1.0);
    transform.translate(-effective.width() / 2.0, -effective.height() / 2.0);
    transform.scale(scale, scale);
    return transform;
}

QVector<QTransform> gridTransforms(const PatternSettings &settings,
                                   const QRectF &area)
{
    QVector<QTransform> transforms;
    const QSizeF tile = effectiveTile(settings);
    const QTransform tileMatrix = tileTransform(settings, tile);

    if (settings.type == 3) {
        const qreal cellWidth = tile.width() * 2.0
                                + qMax<qreal>(0.0, settings.spacingX);
        const qreal cellHeight = tile.height() * 2.0
                                 + qMax<qreal>(0.0, settings.spacingY);
        const QRectF expanded = area.adjusted(-cellWidth, -cellHeight,
                                              cellWidth, cellHeight);
        const int firstCol = int(std::floor((expanded.left() - settings.offsetX)
                                            / cellWidth)) - 1;
        const int lastCol = int(std::ceil((expanded.right() - settings.offsetX)
                                          / cellWidth)) + 1;
        const int firstRow = int(std::floor((expanded.top() - settings.offsetY)
                                            / cellHeight)) - 1;
        const int lastRow = int(std::ceil((expanded.bottom() - settings.offsetY)
                                          / cellHeight)) + 1;
        const int maxTiles = 1600;
        for (int row = firstRow; row <= lastRow; ++row) {
            for (int col = firstCol; col <= lastCol; ++col) {
                if (transforms.size() >= maxTiles)
                    return transforms;
                const qreal x = col * cellWidth + settings.offsetX;
                const qreal y = row * cellHeight + settings.offsetY;
                for (int variant = 0; variant < 4; ++variant) {
                    const bool mirrorX = (variant & 1) != 0;
                    const bool mirrorY = (variant & 2) != 0;
                    QTransform placement;
                    placement.translate(x + (mirrorX ? tile.width() : 0.0),
                                        y + (mirrorY ? tile.height() : 0.0));
                    placement.translate(tile.width() / 2.0, tile.height() / 2.0);
                    placement.scale(mirrorX ? -1.0 : 1.0,
                                    mirrorY ? -1.0 : 1.0);
                    placement.translate(-tile.width() / 2.0, -tile.height() / 2.0);
                    transforms.append(placement * tileMatrix);
                }
            }
        }
        return transforms;
    }

    const bool hex = settings.type == 2;
    const qreal cellWidth = tile.width() + qMax<qreal>(0.0, settings.spacingX);
    const qreal cellHeight = tile.height() + qMax<qreal>(0.0, settings.spacingY);
    const qreal rowStep = hex ? cellHeight * 0.75 : cellHeight;

    const QRectF expanded = area.adjusted(-cellWidth * 2.0, -cellHeight * 2.0,
                                          cellWidth * 2.0, cellHeight * 2.0);
    const int firstCol = int(std::floor((expanded.left() - settings.offsetX)
                                        / qMax<qreal>(1.0, cellWidth))) - 1;
    const int lastCol = int(std::ceil((expanded.right() - settings.offsetX)
                                      / qMax<qreal>(1.0, cellWidth))) + 1;
    const int firstRow = int(std::floor((expanded.top() - settings.offsetY)
                                        / qMax<qreal>(1.0, rowStep))) - 1;
    const int lastRow = int(std::ceil((expanded.bottom() - settings.offsetY)
                                      / qMax<qreal>(1.0, rowStep))) + 1;
    const int maxTiles = 1600;
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = firstCol; col <= lastCol; ++col) {
            if (transforms.size() >= maxTiles)
                return transforms;
            const bool shiftedRow = (settings.type == 1 || hex) && (row % 2 != 0);
            const qreal x = col * cellWidth + settings.offsetX
                            + (shiftedRow ? cellWidth / 2.0 : 0.0);
            const qreal y = row * rowStep + settings.offsetY;
            QTransform placement;
            placement.translate(x, y);
            transforms.append(placement * tileMatrix);
        }
    }
    return transforms;
}

QVector<QTransform> radialTransforms(const PatternSettings &settings,
                                     const QRectF &area, int type)
{
    QVector<QTransform> transforms;
    const int count = qBound(1, settings.count, 64);
    const QPointF center = area.center();
    const QSizeF tile = effectiveTile(settings);
    const QTransform tileMatrix = tileTransform(settings, tile);
    const qreal radius = qMax(tile.width(), tile.height()) / 2.0
                         + qMax<qreal>(0.0, settings.spacingX);

    for (int i = 0; i < count; ++i) {
        const qreal angle = settings.angle + i * 360.0 / count;
        QTransform placement;
        placement.translate(center.x(), center.y());
        placement.rotate(angle);
        if (type == 4)
            placement.translate(radius - tile.width() / 2.0, -tile.height() / 2.0);
        else
            placement.translate(-tile.width() / 2.0, -tile.height() / 2.0);
        transforms.append(placement * tileMatrix);
    }
    return transforms;
}

QPainterPath guidePath(const PatternSettings &settings, const QRectF &area)
{
    if (!settings.path.isEmpty())
        return settings.path;
    QPainterPath path;
    path.moveTo(area.left(), area.center().y());
    path.cubicTo(area.left() + area.width() * 0.34, area.top(),
                 area.left() + area.width() * 0.66, area.bottom(),
                 area.right(), area.center().y());
    return path;
}

QVector<QTransform> pathTransforms(const PatternSettings &settings,
                                   const QRectF &area)
{
    QVector<QTransform> transforms;
    const QPainterPath path = guidePath(settings, area);
    const qreal length = path.length();
    if (length < 0.5)
        return transforms;
    const qreal step = qMax<qreal>(4.0, settings.pathStep);
    const int maximum = qBound(1, settings.count, 512);
    const int steps = qMin(maximum, int(length / step) + 1);
    const QSizeF tile = effectiveTile(settings);
    const QTransform tileMatrix = tileTransform(settings, tile);

    for (int i = 0; i < steps; ++i) {
        const qreal t = steps > 1 ? qreal(i) / qreal(steps - 1) : 0.0;
        const QPointF point = path.pointAtPercent(qMin<qreal>(1.0, t));
        const qreal tangent = path.angleAtPercent(qMin<qreal>(1.0, t));
        QTransform placement;
        placement.translate(point.x(), point.y());
        placement.rotate(settings.angle - tangent);
        placement.translate(-tile.width() / 2.0, -tile.height() / 2.0);
        transforms.append(placement * tileMatrix);
    }
    return transforms;
}

void applyTileTransform(QPainter *painter, const PatternSettings &settings,
                        const QSizeF &tile)
{
    painter->translate(tile.width() / 2.0, tile.height() / 2.0);
    painter->rotate(settings.angle);
    painter->scale(settings.mirrorX ? -1.0 : 1.0,
                   settings.mirrorY ? -1.0 : 1.0);
    painter->translate(-tile.width() / 2.0, -tile.height() / 2.0);
    painter->scale(qBound<qreal>(0.05, settings.scale, 20.0),
                   qBound<qreal>(0.05, settings.scale, 20.0));
}

void drawShape(QPainter *painter, const PatternShape &shape)
{
    painter->save();
    painter->setOpacity(qBound(0.0, shape.opacity, 1.0));
    if (shape.hasFill) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(shape.fill);
        painter->drawPath(shape.path);
    }
    if (shape.hasStroke && shape.strokeWidth > 0.0) {
        QPen pen(shape.stroke, shape.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(shape.path);
    }
    painter->restore();
}

} // namespace

QStringList patternTypeNames()
{
    return {QStringLiteral("Сетка"), QStringLiteral("Кирпич"),
            QStringLiteral("Шестиугольник"), QStringLiteral("Зеркало"),
            QStringLiteral("Радиальный"), QStringLiteral("Поворот"),
            QStringLiteral("По кривой")};
}

QString patternTypeName(int type)
{
    const QStringList names = patternTypeNames();
    if (type < 0 || type >= names.size())
        return names.first();
    return names.at(type);
}

PatternDefinition patternFromObjects(const QVector<VectorObject> &objects,
                                     const QString &name)
{
    PatternDefinition definition;
    definition.name = name.trimmed().isEmpty() ? QStringLiteral("Узор")
                                               : name.trimmed();
    QRectF bounds;
    for (const VectorObject &object : objects) {
        const QRectF visual = objectVisualBounds(object);
        if (visual.width() < 0.2 || visual.height() < 0.2)
            continue;
        bounds = bounds.isNull() ? visual : bounds.united(visual);
    }
    if (bounds.width() < 1.0 || bounds.height() < 1.0)
        bounds = QRectF(0.0, 0.0, 64.0, 64.0);
    definition.bounds = bounds;
    definition.objects = objects;
    definition.settings.type = 0;
    definition.settings.tile = bounds.size();
    definition.settings.count = 8;
    return definition;
}

QVector<PatternShape> bakePatternShapes(const PatternDefinition &definition)
{
    QVector<PatternShape> shapes;
    const QSizeF tile(qMax<qreal>(4.0, definition.settings.tile.width()),
                      qMax<qreal>(4.0, definition.settings.tile.height()));
    QRectF bounds = definition.bounds;
    if (bounds.width() < 1.0 || bounds.height() < 1.0)
        bounds = QRectF(QPointF(0, 0), tile);
    const qreal scaleX = tile.width() / bounds.width();
    const qreal scaleY = tile.height() / bounds.height();

    for (const VectorObject &source : definition.objects) {
        QPainterPath path = source.path;
        if (path.isEmpty() && source.type == ShapeType::Text)
            path = buildTextPath(source);
        if (path.isEmpty() || source.type == ShapeType::Image)
            continue;

        const QRectF local = source.localBounds();
        // QTransform composes in the reverse order of the calls below, so the
        // scaling into the tile is requested first and applied last.
        QTransform transform;
        transform.scale(scaleX, scaleY);
        transform.translate(-bounds.left(), -bounds.top());
        transform.translate(source.pos.x() + local.center().x(),
                            source.pos.y() + local.center().y());
        transform.rotate(source.rotation);
        transform.translate(-local.center().x(), -local.center().y());

        PatternShape shape;
        shape.path = transform.map(path);
        shape.fill = source.fill;
        shape.stroke = source.stroke;
        shape.strokeWidth = qMax<qreal>(0.0, source.strokeWidth
                                                 * (scaleX + scaleY) / 2.0);
        shape.hasFill = source.hasFill;
        shape.hasStroke = source.hasStroke;
        shape.opacity = source.opacity;
        if (shape.hasFill || shape.hasStroke)
            shapes.append(shape);
    }
    return shapes;
}

QVector<QTransform> patternTileTransforms(const PatternSettings &settings,
                                          const QRectF &area)
{
    if (settings.tile.width() < 0.5 || settings.tile.height() < 0.5)
        return {};
    if (settings.type == 4 || settings.type == 5)
        return radialTransforms(settings, area, settings.type);
    if (settings.type == 6)
        return pathTransforms(settings, area);
    return gridTransforms(settings, area);
}

void paintPatternFill(QPainter *painter, const PatternSettings &settings,
                      const QVector<PatternShape> &shapes, const QRectF &area)
{
    if (shapes.isEmpty() || area.width() < 0.1 || area.height() < 0.1)
        return;
    const QVector<QTransform> transforms = patternTileTransforms(settings, area);
    for (const QTransform &transform : transforms) {
        painter->save();
        painter->setTransform(transform, true);
        for (const PatternShape &shape : shapes)
            drawShape(painter, shape);
        painter->restore();
    }
}

QVector<VectorObject> expandPatternObject(const VectorObject &source)
{
    QVector<VectorObject> result;
    if (source.patternShapes.isEmpty())
        return result;

    const QRectF area = source.localBounds();
    QPainterPath clip = source.path;
    if (clip.isEmpty() && source.type == ShapeType::Text)
        clip = buildTextPath(source);
    if (clip.isEmpty())
        clip.addRect(area);

    const QVector<QTransform> transforms =
        patternTileTransforms(source.pattern, area);
    constexpr int kMaxExpandedObjects = 4000;
    for (const QTransform &transform : transforms) {
        if (result.size() >= kMaxExpandedObjects)
            break;
        for (const PatternShape &shape : source.patternShapes) {
            if (result.size() >= kMaxExpandedObjects)
                break;
            const QPainterPath visible = transform.map(shape.path).intersected(clip);
            if (visible.isEmpty())
                continue;
            VectorObject object;
            object.type = ShapeType::Bezier;
            object.path = visible;
            object.pos = source.pos;
            object.fill = shape.fill;
            object.stroke = shape.stroke;
            object.strokeWidth = shape.strokeWidth;
            object.hasFill = shape.hasFill;
            object.hasStroke = shape.hasStroke;
            object.opacity = qBound(0.0, source.opacity * shape.opacity, 1.0);
            object.page = source.page;
            object.layer = source.layer;
            result.append(object);
        }
    }
    return result;
}

QImage renderPatternPreview(const PatternSettings &settings,
                            const QVector<PatternShape> &shapes,
                            const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (shapes.isEmpty() || size.isEmpty())
        return image;

    const QSizeF tile = effectiveTile(settings);
    const qreal scale = qMin(size.width() / (tile.width() * 2.0),
                             size.height() / (tile.height() * 2.0));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            painter.save();
            painter.translate(col * tile.width(), row * tile.height());
            applyTileTransform(&painter, settings, tile);
            for (const PatternShape &shape : shapes)
                drawShape(&painter, shape);
            painter.restore();
        }
    }
    painter.end();
    return image;
}

bool patternIsSeamless(const PatternSettings &settings,
                       const QVector<PatternShape> &shapes, qreal *difference)
{
    if (difference)
        *difference = 0.0;
    if (shapes.isEmpty())
        return true;
    if (settings.spacingX > 0.5 || settings.spacingY > 0.5)
        return true;

    const QSizeF tile = effectiveTile(settings);
    const int width = qBound(24, int(tile.width()), 320);
    const int height = qBound(24, int(tile.height()), 320);

    QImage cell(width, height, QImage::Format_ARGB32_Premultiplied);
    cell.fill(Qt::transparent);
    {
        QPainter painter(&cell);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(qreal(width) / tile.width(), qreal(height) / tile.height());
        applyTileTransform(&painter, settings, tile);
        for (const PatternShape &shape : shapes)
            drawShape(&painter, shape);
    }

    qreal total = 0.0;
    int count = 0;
    for (int y = 0; y < height; ++y) {
        const QColor left = cell.pixelColor(0, y);
        const QColor right = cell.pixelColor(width - 1, y);
        total += qAbs(left.redF() - right.redF())
                 + qAbs(left.greenF() - right.greenF())
                 + qAbs(left.blueF() - right.blueF())
                 + qAbs(left.alphaF() - right.alphaF());
        count += 4;
    }
    for (int x = 0; x < width; ++x) {
        const QColor top = cell.pixelColor(x, 0);
        const QColor bottom = cell.pixelColor(x, height - 1);
        total += qAbs(top.redF() - bottom.redF())
                 + qAbs(top.greenF() - bottom.greenF())
                 + qAbs(top.blueF() - bottom.blueF())
                 + qAbs(top.alphaF() - bottom.alphaF());
        count += 4;
    }
    const qreal mean = count > 0 ? total / count : 0.0;
    if (difference)
        *difference = mean;
    return mean < 0.06;
}

QJsonObject patternSettingsToJson(const PatternSettings &settings)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("type"), settings.type);
    entry.insert(QStringLiteral("tile"),
                 QJsonArray{settings.tile.width(), settings.tile.height()});
    entry.insert(QStringLiteral("spacingX"), settings.spacingX);
    entry.insert(QStringLiteral("spacingY"), settings.spacingY);
    entry.insert(QStringLiteral("offsetX"), settings.offsetX);
    entry.insert(QStringLiteral("offsetY"), settings.offsetY);
    entry.insert(QStringLiteral("angle"), settings.angle);
    entry.insert(QStringLiteral("scale"), settings.scale);
    entry.insert(QStringLiteral("count"), settings.count);
    entry.insert(QStringLiteral("mirrorX"), settings.mirrorX);
    entry.insert(QStringLiteral("mirrorY"), settings.mirrorY);
    entry.insert(QStringLiteral("pathStep"), settings.pathStep);
    if (!settings.path.isEmpty())
        entry.insert(QStringLiteral("path"), encodePathData(settings.path));
    return entry;
}

PatternSettings patternSettingsFromJson(const QJsonObject &entry)
{
    PatternSettings settings;
    settings.type = entry.value(QStringLiteral("type")).toInt(0);
    const QJsonArray tile = entry.value(QStringLiteral("tile")).toArray();
    if (tile.size() == 2) {
        settings.tile = QSizeF(tile.at(0).toDouble(64.0),
                               tile.at(1).toDouble(64.0));
    }
    settings.spacingX = entry.value(QStringLiteral("spacingX")).toDouble(0.0);
    settings.spacingY = entry.value(QStringLiteral("spacingY")).toDouble(0.0);
    settings.offsetX = entry.value(QStringLiteral("offsetX")).toDouble(0.0);
    settings.offsetY = entry.value(QStringLiteral("offsetY")).toDouble(0.0);
    settings.angle = entry.value(QStringLiteral("angle")).toDouble(0.0);
    settings.scale = entry.value(QStringLiteral("scale")).toDouble(1.0);
    settings.count = entry.value(QStringLiteral("count")).toInt(8);
    settings.mirrorX = entry.value(QStringLiteral("mirrorX")).toBool(false);
    settings.mirrorY = entry.value(QStringLiteral("mirrorY")).toBool(false);
    settings.pathStep = entry.value(QStringLiteral("pathStep")).toDouble(80.0);
    settings.path = decodePathData(entry.value(QStringLiteral("path")).toArray());
    return settings;
}

QJsonObject patternDefinitionToJson(const PatternDefinition &definition)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), definition.id);
    entry.insert(QStringLiteral("name"), definition.name);
    entry.insert(QStringLiteral("bounds"),
                 QJsonArray{definition.bounds.x(), definition.bounds.y(),
                            definition.bounds.width(), definition.bounds.height()});
    entry.insert(QStringLiteral("settings"),
                 patternSettingsToJson(definition.settings));
    QJsonArray objects;
    for (const VectorObject &object : definition.objects)
        objects.append(vectorObjectToJson(object));
    entry.insert(QStringLiteral("objects"), objects);
    return entry;
}

PatternDefinition patternDefinitionFromJson(const QJsonObject &entry)
{
    PatternDefinition definition;
    definition.id = entry.value(QStringLiteral("id")).toInt();
    definition.name = entry.value(QStringLiteral("name"))
                          .toString(QStringLiteral("Узор"));
    const QJsonArray bounds = entry.value(QStringLiteral("bounds")).toArray();
    if (bounds.size() == 4) {
        definition.bounds = QRectF(bounds.at(0).toDouble(), bounds.at(1).toDouble(),
                                   bounds.at(2).toDouble(), bounds.at(3).toDouble());
    }
    definition.settings = patternSettingsFromJson(
        entry.value(QStringLiteral("settings")).toObject());
    for (const QJsonValue &value : entry.value(QStringLiteral("objects")).toArray())
        definition.objects.append(vectorObjectFromJson(value.toObject()));
    return definition;
}
