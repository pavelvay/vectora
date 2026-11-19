#include "VectorCraftIO.h"

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTransform>
#include <QtMath>

#include <cmath>

#include "Document.h"

namespace VectorCraftIO {
namespace {

constexpr qreal kPointsToPixels = 96.0 / 72.0;
constexpr qreal kPixelsToPoints = 72.0 / 96.0;

const QByteArray &blobMarker()
{
    static const QByteArray marker("\0VCBLOBS\0", 9);
    return marker;
}

QColor parseColor(const QJsonObject &value)
{
    const QString model = value.value(QStringLiteral("model")).toString();
    if (model == QLatin1String("rgb")) {
        return QColor::fromRgbF(qBound(0.0, value.value(QStringLiteral("r")).toDouble(0.0), 1.0),
                                qBound(0.0, value.value(QStringLiteral("g")).toDouble(0.0), 1.0),
                                qBound(0.0, value.value(QStringLiteral("b")).toDouble(0.0), 1.0));
    }
    if (model == QLatin1String("cmyk")) {
        return QColor::fromCmykF(
            qBound(0.0, value.value(QStringLiteral("c")).toDouble(0.0), 1.0),
            qBound(0.0, value.value(QStringLiteral("m")).toDouble(0.0), 1.0),
            qBound(0.0, value.value(QStringLiteral("y")).toDouble(0.0), 1.0),
            qBound(0.0, value.value(QStringLiteral("k")).toDouble(0.0), 1.0));
    }
    if (model == QLatin1String("gray")) {
        const qreal gray = qBound(0.0, value.value(QStringLiteral("k")).toDouble(0.0), 1.0);
        return QColor::fromRgbF(gray, gray, gray);
    }
    if (model == QLatin1String("lab")) {
        return colorFromLab(value.value(QStringLiteral("l")).toDouble(0.0),
                            value.value(QStringLiteral("a")).toDouble(0.0),
                            value.value(QStringLiteral("b")).toDouble(0.0));
    }
    return QColor(Qt::black);
}

QColor solidFromPattern(const QJsonArray &patterns, const QString &name,
                        QColor fallback)
{
    for (const QJsonValue &patternValue : patterns) {
        const QJsonObject pattern = patternValue.toObject();
        if (pattern.value(QStringLiteral("name")).toString() != name)
            continue;
        const QJsonArray art = pattern.value(QStringLiteral("art")).toArray();
        for (const QJsonValue &nodeValue : art) {
            const QJsonObject node = nodeValue.toObject();
            const QJsonObject appearance = node.value(QStringLiteral("appearance")).toObject();
            for (const QJsonValue &itemValue : appearance.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = itemValue.toObject();
                if (item.value(QStringLiteral("kind")).toString() != QLatin1String("fill"))
                    continue;
                const QJsonObject paint = item.value(QStringLiteral("paint")).toObject();
                if (paint.value(QStringLiteral("type")).toString() == QLatin1String("solid"))
                    return parseColor(paint.value(QStringLiteral("color")).toObject());
            }
            if (node.value(QStringLiteral("kind")).toObject()
                    .value(QStringLiteral("type")).toString() == QLatin1String("group")) {
                const QJsonObject kind = node.value(QStringLiteral("kind")).toObject();
                for (const QJsonValue &childValue : kind.value(QStringLiteral("children")).toArray()) {
                    const QJsonObject child = childValue.toObject();
                    const QJsonObject childAppearance =
                        child.value(QStringLiteral("appearance")).toObject();
                    for (const QJsonValue &itemValue :
                         childAppearance.value(QStringLiteral("items")).toArray()) {
                        const QJsonObject item = itemValue.toObject();
                        if (item.value(QStringLiteral("kind")).toString()
                            != QLatin1String("fill"))
                            continue;
                        const QJsonObject paint =
                            item.value(QStringLiteral("paint")).toObject();
                        if (paint.value(QStringLiteral("type")).toString()
                            == QLatin1String("solid"))
                            return parseColor(
                                paint.value(QStringLiteral("color")).toObject());
                    }
                }
            }
        }
    }
    return fallback;
}

struct Paint {
    bool visible = false;
    QColor color = QColor(Qt::black);
};

Paint parsePaint(const QJsonObject &paint, const QJsonArray &patterns)
{
    Paint result;
    const QString type = paint.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("solid")) {
        result.visible = true;
        result.color = parseColor(paint.value(QStringLiteral("color")).toObject());
    } else if (type == QLatin1String("pattern")) {
        result.visible = true;
        result.color = solidFromPattern(
            patterns, paint.value(QStringLiteral("pattern")).toString(),
            QColor(QStringLiteral("#9aa4b2")));
    }
    return result;
}

struct Appearance {
    bool hasFill = true;
    QColor fill = QColor(Qt::black);
    bool hasStroke = false;
    QColor stroke = QColor(Qt::black);
    qreal strokeWidth = 1.0;
    int cap = 0;
    int join = 0;
};

int capFromName(const QString &name)
{
    if (name == QLatin1String("Round"))
        return 2;
    if (name == QLatin1String("Square"))
        return 1;
    return 0;
}

int joinFromName(const QString &name)
{
    if (name == QLatin1String("Round"))
        return 2;
    if (name == QLatin1String("Bevel"))
        return 1;
    return 0;
}

Appearance parseAppearance(const QJsonObject &node, const QJsonArray &patterns)
{
    Appearance appearance;
    const QJsonObject object = node.value(QStringLiteral("appearance")).toObject();
    const QJsonArray items = object.value(QStringLiteral("items")).toArray();
    if (items.isEmpty())
        return appearance;
    appearance.hasFill = false;
    appearance.hasStroke = false;
    for (const QJsonValue &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        const Paint paint = parsePaint(item.value(QStringLiteral("paint")).toObject(),
                                       patterns);
        if (kind == QLatin1String("fill")) {
            appearance.hasFill = paint.visible;
            if (paint.visible)
                appearance.fill = paint.color;
        } else if (kind == QLatin1String("stroke")) {
            appearance.hasStroke = paint.visible;
            if (paint.visible)
                appearance.stroke = paint.color;
            appearance.strokeWidth = item.value(QStringLiteral("width")).toDouble(1.0);
            appearance.cap = capFromName(item.value(QStringLiteral("cap")).toString());
            appearance.join = joinFromName(item.value(QStringLiteral("join")).toString());
        }
    }
    return appearance;
}

QPointF pointFromJson(const QJsonValue &value, const QPointF &fallback = QPointF())
{
    const QJsonArray array = value.toArray();
    if (array.size() < 2)
        return fallback;
    return QPointF(array.at(0).toDouble(), array.at(1).toDouble());
}

QPainterPath pathFromJson(const QJsonObject &pathObject)
{
    QPainterPath result;
    const QJsonArray subpaths = pathObject.value(QStringLiteral("subpaths")).toArray();
    for (const QJsonValue &subpathValue : subpaths) {
        const QJsonObject subpath = subpathValue.toObject();
        const QJsonArray anchorValues = subpath.value(QStringLiteral("anchors")).toArray();
        if (anchorValues.isEmpty())
            continue;

        struct Anchor {
            QPointF point;
            bool hasIn = false;
            bool hasOut = false;
            QPointF in;
            QPointF out;
        };
        QVector<Anchor> anchors;
        anchors.reserve(anchorValues.size());
        for (const QJsonValue &anchorValue : anchorValues) {
            const QJsonObject entry = anchorValue.toObject();
            Anchor anchor;
            anchor.point = pointFromJson(entry.value(QStringLiteral("p")));
            if (entry.contains(QStringLiteral("in"))) {
                anchor.hasIn = true;
                anchor.in = pointFromJson(entry.value(QStringLiteral("in")), anchor.point);
            }
            if (entry.contains(QStringLiteral("out"))) {
                anchor.hasOut = true;
                anchor.out = pointFromJson(entry.value(QStringLiteral("out")), anchor.point);
            }
            anchors.append(anchor);
        }
        if (anchors.size() < 2)
            continue;

        const bool closed = subpath.value(QStringLiteral("closed")).toBool();
        if (closed
            && QLineF(anchors.first().point, anchors.last().point).length() < 0.0001)
            anchors.removeLast();
        if (anchors.size() < 2)
            continue;

        result.moveTo(anchors.first().point);
        for (int i = 1; i < anchors.size(); ++i) {
            const Anchor &previous = anchors.at(i - 1);
            const Anchor &current = anchors.at(i);
            if (previous.hasOut || current.hasIn) {
                result.cubicTo(previous.hasOut ? previous.out : previous.point,
                               current.hasIn ? current.in : current.point,
                               current.point);
            } else {
                result.lineTo(current.point);
            }
        }
        if (closed) {
            const Anchor &last = anchors.last();
            const Anchor &first = anchors.first();
            if (last.hasOut || first.hasIn) {
                result.cubicTo(last.hasOut ? last.out : last.point,
                               first.hasIn ? first.in : first.point,
                               first.point);
            }
            result.closeSubpath();
        }
    }
    result.setFillRule(Qt::WindingFill);
    return result;
}

QTransform transformFromJson(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() < 6)
        return QTransform();
    return QTransform(array.at(0).toDouble(), array.at(1).toDouble(),
                      array.at(2).toDouble(), array.at(3).toDouble(),
                      array.at(4).toDouble(), array.at(5).toDouble());
}

int textAlignFromName(const QString &name)
{
    if (name == QLatin1String("Center"))
        return 1;
    if (name == QLatin1String("Right"))
        return 2;
    return 0;
}

struct ImportState {
    Document *document = nullptr;
    QJsonArray patterns;
    QHash<QString, QImage> images;
    QVector<QRectF> artboards;
    QVector<QPointF> origins;
    QVector<VectorObject> objects;
};

QString textOfRuns(const QJsonArray &runs, QJsonObject *firstStyle)
{
    QString text;
    for (const QJsonValue &runValue : runs) {
        const QJsonObject run = runValue.toObject();
        text += run.value(QStringLiteral("text")).toString();
        if (firstStyle && firstStyle->isEmpty())
            *firstStyle = run.value(QStringLiteral("style")).toObject();
    }
    return text;
}

void appendPathObject(ImportState &state, const QJsonObject &node,
                      const QPainterPath &path, int layer, qreal inheritedOpacity,
                      const QString &name)
{
    if (path.isEmpty())
        return;
    const Appearance appearance = parseAppearance(node, state.patterns);
    const qreal nodeOpacity = node.value(QStringLiteral("opacity")).toDouble(1.0);

    VectorObject object;
    object.type = ShapeType::Freehand;
    object.path = QTransform::fromScale(kPointsToPixels, kPointsToPixels).map(path);
    object.pos = QPointF(0.0, 0.0);
    object.rotation = 0.0;
    object.page = 0;
    object.layer = layer;
    object.name = name;
    object.hasFill = appearance.hasFill;
    object.fill = appearance.fill;
    object.hasStroke = appearance.hasStroke;
    object.stroke = appearance.stroke;
    object.strokeWidth = appearance.strokeWidth * kPointsToPixels;
    object.capStyle = appearance.cap;
    object.joinStyle = appearance.join;
    object.opacity = qBound(0.0, inheritedOpacity * nodeOpacity, 1.0);
    state.objects.append(object);
}

void importText(ImportState &state, const QJsonObject &node, int layer,
                qreal inheritedOpacity, const QString &name)
{
    const QJsonObject kind = node.value(QStringLiteral("kind")).toObject();
    QJsonObject style;
    const QString text = textOfRuns(kind.value(QStringLiteral("runs")).toArray(), &style);
    if (text.isEmpty())
        return;

    const QTransform transform = transformFromJson(kind.value(QStringLiteral("xf")));
    const qreal scaleX = std::hypot(transform.m11(), transform.m12());
    const qreal scaleY = std::hypot(transform.m21(), transform.m22());
    const qreal averageScale = (scaleX + scaleY) / 2.0;

    QFont font(style.value(QStringLiteral("font_family")).toString());
    const QString fontStyle = style.value(QStringLiteral("font_style")).toString();
    font.setBold(fontStyle.contains(QLatin1String("Bold"), Qt::CaseInsensitive));
    font.setItalic(fontStyle.contains(QLatin1String("Italic"), Qt::CaseInsensitive) ||
                   fontStyle.contains(QLatin1String("Oblique"), Qt::CaseInsensitive));
    const qreal sizeInPoints = style.value(QStringLiteral("size")).toDouble(12.0);
    font.setPixelSize(qMax(1, qRound(sizeInPoints * averageScale * kPointsToPixels)));

    const QJsonObject textKind = kind.value(QStringLiteral("kind")).toObject();
    const bool area = textKind.value(QStringLiteral("kind")).toString()
                      == QLatin1String("area");

    QRectF box;
    QPointF topLeft;
    if (area) {
        const QPainterPath frame =
            pathFromJson(textKind.value(QStringLiteral("frame")).toObject());
        const QRectF mapped = transform.map(frame).boundingRect();
        box = QRectF(0.0, 0.0, mapped.width() * kPointsToPixels,
                     mapped.height() * kPointsToPixels);
        topLeft = mapped.topLeft() * kPointsToPixels;
    } else {
        const QFontMetricsF metrics(font);
        const qreal width = qMax<qreal>(1.0, metrics.horizontalAdvance(text));
        const qreal height = qMax<qreal>(1.0, metrics.height());
        const QPointF baseline = transform.map(QPointF(0.0, 0.0)) * kPointsToPixels;
        const int align = textAlignFromName(
            kind.value(QStringLiteral("para")).toObject()
                .value(QStringLiteral("justify")).toString());
        qreal x = baseline.x();
        if (align == 1)
            x -= width / 2.0;
        else if (align == 2)
            x -= width;
        box = QRectF(0.0, 0.0, width, height);
        topLeft = QPointF(x, baseline.y() - metrics.ascent());
    }

    const Paint fillPaint = parsePaint(
        style.value(QStringLiteral("fill")).toObject(), state.patterns);
    const Paint strokePaint = parsePaint(
        style.value(QStringLiteral("stroke")).toObject(), state.patterns);
    const qreal nodeOpacity = node.value(QStringLiteral("opacity")).toDouble(1.0);

    VectorObject object;
    object.type = ShapeType::Text;
    object.text = text;
    object.font = font;
    object.textBox = box;
    object.pos = topLeft;
    object.page = 0;
    object.layer = layer;
    object.name = name;
    object.textAlign = textAlignFromName(
        kind.value(QStringLiteral("para")).toObject()
            .value(QStringLiteral("justify")).toString());
    object.hasFill = fillPaint.visible;
    object.fill = fillPaint.color;
    object.hasStroke = strokePaint.visible;
    object.stroke = strokePaint.color;
    object.strokeWidth =
        style.value(QStringLiteral("stroke_width")).toDouble(0.0)
        * averageScale * kPointsToPixels;
    object.opacity = qBound(0.0, inheritedOpacity * nodeOpacity, 1.0);
    state.objects.append(object);
}

void importImage(ImportState &state, const QJsonObject &node, int layer,
                 qreal inheritedOpacity, const QString &name)
{
    const QJsonObject kind = node.value(QStringLiteral("kind")).toObject();
    const QString key = kind.value(QStringLiteral("key")).toString();
    const QImage image = state.images.value(key);
    if (image.isNull())
        return;

    const QTransform transform = transformFromJson(kind.value(QStringLiteral("xf")));
    const qreal scaleX = std::hypot(transform.m11(), transform.m12());
    const qreal scaleY = std::hypot(transform.m21(), transform.m22());
    const qreal imageWidth = kind.value(QStringLiteral("width")).toDouble(image.width());
    const qreal imageHeight = kind.value(QStringLiteral("height")).toDouble(image.height());
    if (imageWidth <= 0.0 || imageHeight <= 0.0)
        return;

    const qreal width = imageWidth * scaleX * kPointsToPixels;
    const qreal height = imageHeight * scaleY * kPointsToPixels;
    const qreal rotation = qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));

    VectorObject object;
    object.type = ShapeType::Image;
    object.image = image;
    object.textBox = QRectF(0.0, 0.0, width, height);
    QPointF position = transform.map(QPointF(0.0, 0.0)) * kPointsToPixels;
    if (!qFuzzyIsNull(rotation)) {
        // VectorCraft rotates around the image origin; Vectora rotates around
        // the local centre, so compensate the placement.
        const QTransform rotationOnly = QTransform().rotate(rotation);
        position += QPointF(width / 2.0, height / 2.0)
                    - rotationOnly.map(QPointF(width / 2.0, height / 2.0));
        object.rotation = rotation;
    }
    object.pos = position;
    object.page = 0;
    object.layer = layer;
    object.name = name;
    object.hasFill = false;
    object.hasStroke = false;
    object.opacity =
        qBound(0.0, inheritedOpacity * node.value(QStringLiteral("opacity")).toDouble(1.0), 1.0);
    state.objects.append(object);
}

void importNode(ImportState &state, const QJsonObject &node, int layer,
                qreal opacity);

void importChildren(ImportState &state, const QJsonArray &children, int layer,
                    qreal opacity)
{
    for (const QJsonValue &child : children)
        importNode(state, child.toObject(), layer, opacity);
}

void importNode(ImportState &state, const QJsonObject &node, int layer, qreal opacity)
{
    if (!node.value(QStringLiteral("visible")).toBool(true))
        return;
    const QJsonObject kind = node.value(QStringLiteral("kind")).toObject();
    const QString type = kind.value(QStringLiteral("type")).toString();
    const QString name = node.value(QStringLiteral("name")).toString();
    const qreal nodeOpacity = node.value(QStringLiteral("opacity")).toDouble(1.0);
    const qreal combined = qBound(0.0, opacity * nodeOpacity, 1.0);

    if (type == QLatin1String("layer") || type == QLatin1String("group")) {
        importChildren(state, kind.value(QStringLiteral("children")).toArray(), layer, combined);
        return;
    }
    if (type == QLatin1String("path")) {
        if (node.value(QStringLiteral("clipping")).toBool())
            return;
        QPainterPath path = pathFromJson(kind.value(QStringLiteral("path")).toObject());
        appendPathObject(state, node, path, layer, opacity, name);
        return;
    }
    if (type == QLatin1String("compound")) {
        QPainterPath merged;
        for (const QJsonValue &child : kind.value(QStringLiteral("children")).toArray()) {
            const QJsonObject childNode = child.toObject();
            const QJsonObject childKind = childNode.value(QStringLiteral("kind")).toObject();
            if (childKind.value(QStringLiteral("type")).toString()
                != QLatin1String("path"))
                continue;
            merged.addPath(pathFromJson(
                childKind.value(QStringLiteral("path")).toObject()));
        }
        const QString rule = kind.value(QStringLiteral("rule")).toString();
        merged.setFillRule(rule == QLatin1String("EvenOdd") ? Qt::OddEvenFill
                                                           : Qt::WindingFill);
        appendPathObject(state, node, merged, layer, opacity, name);
        return;
    }
    if (type == QLatin1String("text")) {
        importText(state, node, layer, opacity, name);
        return;
    }
    if (type == QLatin1String("image")) {
        importImage(state, node, layer, opacity, name);
        return;
    }
}

int pageForBounds(const ImportState &state, const QRectF &bounds)
{
    if (state.artboards.isEmpty())
        return 0;
    const QPointF center = bounds.center();
    int best = -1;
    qreal bestArea = 0.0;
    for (int i = 0; i < state.artboards.size(); ++i) {
        const QRectF artboard = state.artboards.at(i);
        if (artboard.contains(center))
            return i;
        const QRectF intersection = artboard.intersected(bounds);
        const qreal area = intersection.width() * intersection.height();
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return best >= 0 && bestArea > 0.0 ? best : 0;
}

void shiftObject(VectorObject *object, const QPointF &origin)
{
    if (origin.isNull())
        return;
    QTransform shift;
    shift.translate(-origin.x(), -origin.y());
    object->path = shift.map(object->path);
    object->pos = shift.map(object->pos);
}

bool parseDocument(const QByteArray &data, QJsonObject *root, QByteArray *blobs,
                   QString *error)
{
    const int marker = data.indexOf(blobMarker());
    const QByteArray json = marker >= 0 ? data.left(marker) : data;
    if (blobs)
        *blobs = marker >= 0 ? data.mid(marker + blobMarker().size()) : QByteArray();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Файл VectorCraft повреждён: %1")
                         .arg(parseError.errorString());
        return false;
    }
    *root = document.object();
    return true;
}

// --- export helpers --------------------------------------------------------

QJsonObject colorToJson(const QColor &color)
{
    QJsonObject object;
    object.insert(QStringLiteral("model"), QStringLiteral("rgb"));
    object.insert(QStringLiteral("r"), color.redF());
    object.insert(QStringLiteral("g"), color.greenF());
    object.insert(QStringLiteral("b"), color.blueF());
    return object;
}

QJsonObject solidPaint(const QColor &color)
{
    QJsonObject paint;
    paint.insert(QStringLiteral("type"), QStringLiteral("solid"));
    paint.insert(QStringLiteral("color"), colorToJson(color));
    return paint;
}

QJsonObject nonePaint()
{
    QJsonObject paint;
    paint.insert(QStringLiteral("type"), QStringLiteral("none"));
    return paint;
}

QColor averageFill(const VectorObject &object)
{
    if (object.fillStyle == 0 || object.gradientStops.size() < 2)
        return object.fill;
    qreal r = 0.0;
    qreal g = 0.0;
    qreal b = 0.0;
    for (const GradientStop &stop : object.gradientStops) {
        r += stop.color.redF();
        g += stop.color.greenF();
        b += stop.color.blueF();
    }
    const qreal count = object.gradientStops.size();
    return QColor::fromRgbF(qBound(0.0, r / count, 1.0),
                            qBound(0.0, g / count, 1.0),
                            qBound(0.0, b / count, 1.0));
}

QJsonObject appearanceToJson(const VectorObject &object)
{
    QJsonArray items;
    if (object.hasFill && object.fillOpacity > 0.001 && object.opacity > 0.001) {
        QJsonObject fill;
        fill.insert(QStringLiteral("kind"), QStringLiteral("fill"));
        fill.insert(QStringLiteral("paint"), solidPaint(averageFill(object)));
        items.append(fill);
    }
    if (object.hasStroke && object.strokeWidth > 0.0 && object.strokeOpacity > 0.001
        && object.opacity > 0.001) {
        QJsonObject stroke;
        stroke.insert(QStringLiteral("kind"), QStringLiteral("stroke"));
        stroke.insert(QStringLiteral("paint"), solidPaint(object.stroke));
        stroke.insert(QStringLiteral("width"), object.strokeWidth * kPixelsToPoints);
        if (object.capStyle == 1)
            stroke.insert(QStringLiteral("cap"), QStringLiteral("Square"));
        else if (object.capStyle == 2)
            stroke.insert(QStringLiteral("cap"), QStringLiteral("Round"));
        if (object.joinStyle == 1)
            stroke.insert(QStringLiteral("join"), QStringLiteral("Bevel"));
        else if (object.joinStyle == 2)
            stroke.insert(QStringLiteral("join"), QStringLiteral("Round"));
        stroke.insert(QStringLiteral("miter_limit"), 4.0);
        items.append(stroke);
    }
    QJsonObject appearance;
    appearance.insert(QStringLiteral("items"), items);
    return appearance;
}

struct AnchorOut {
    QPointF point;
    bool hasIn = false;
    QPointF in;
    bool hasOut = false;
    QPointF out;
};

QJsonObject pathToJson(const QPainterPath &path)
{
    QJsonArray subpaths;
    int index = 0;
    while (index < path.elementCount()) {
        if (path.elementAt(index).type != QPainterPath::MoveToElement) {
            ++index;
            continue;
        }
        QVector<AnchorOut> anchors;
        AnchorOut first;
        first.point = QPointF(path.elementAt(index).x, path.elementAt(index).y);
        anchors.append(first);
        ++index;
        while (index < path.elementCount()
               && path.elementAt(index).type != QPainterPath::MoveToElement) {
            const QPainterPath::Element element = path.elementAt(index);
            if (element.type == QPainterPath::LineToElement) {
                AnchorOut anchor;
                anchor.point = QPointF(element.x, element.y);
                anchors.append(anchor);
                ++index;
            } else if (element.type == QPainterPath::CurveToElement
                       && index + 2 < path.elementCount()) {
                const QPainterPath::Element control2 = path.elementAt(index + 1);
                const QPainterPath::Element end = path.elementAt(index + 2);
                anchors.last().hasOut = true;
                anchors.last().out = QPointF(element.x, element.y);
                AnchorOut anchor;
                anchor.point = QPointF(end.x, end.y);
                anchor.hasIn = true;
                anchor.in = QPointF(control2.x, control2.y);
                anchors.append(anchor);
                index += 3;
            } else {
                ++index;
            }
        }

        bool closed = false;
        if (anchors.size() > 1
            && QLineF(anchors.first().point, anchors.last().point).length() < 0.01) {
            const AnchorOut last = anchors.takeLast();
            anchors.first().hasIn = last.hasIn;
            anchors.first().in = last.in;
            closed = true;
        }
        if (anchors.size() < 2)
            continue;

        QJsonArray anchorArray;
        for (const AnchorOut &anchor : anchors) {
            QJsonObject entry;
            QJsonArray point;
            point.append(anchor.point.x());
            point.append(anchor.point.y());
            entry.insert(QStringLiteral("p"), point);
            if (anchor.hasIn) {
                QJsonArray in;
                in.append(anchor.in.x());
                in.append(anchor.in.y());
                entry.insert(QStringLiteral("in"), in);
            }
            if (anchor.hasOut) {
                QJsonArray out;
                out.append(anchor.out.x());
                out.append(anchor.out.y());
                entry.insert(QStringLiteral("out"), out);
            }
            anchorArray.append(entry);
        }
        QJsonObject subpath;
        subpath.insert(QStringLiteral("anchors"), anchorArray);
        if (closed)
            subpath.insert(QStringLiteral("closed"), true);
        subpaths.append(subpath);
    }

    QJsonObject pathJson;
    pathJson.insert(QStringLiteral("subpaths"), subpaths);
    return pathJson;
}

QJsonArray transformToJson(const QTransform &transform)
{
    QJsonArray array;
    array.append(transform.m11());
    array.append(transform.m12());
    array.append(transform.m21());
    array.append(transform.m22());
    array.append(transform.dx());
    array.append(transform.dy());
    return array;
}

QTransform placementTransform(const VectorObject &object)
{
    const QRectF bounds = object.localBounds();
    QTransform placement;
    placement.translate(object.pos.x(), object.pos.y());
    if (!qFuzzyIsNull(object.rotation)) {
        placement.translate(bounds.center().x(), bounds.center().y());
        placement.rotate(object.rotation);
        placement.translate(-bounds.center().x(), -bounds.center().y());
    }
    return placement;
}

bool needsRaster(const VectorObject &object)
{
    for (const Effect &effect : object.effects) {
        if (effect.enabled)
            return true;
    }
    return object.shadow || object.type == ShapeType::Dimension
           || object.type == ShapeType::Code
           || (object.type == ShapeType::Text && object.textOnPath);
}

bool objectVisibleOnPage(const Document &document, const VectorObject &object)
{
    if (object.layer >= 0 && object.layer < document.layerVisible.size()
        && !document.layerVisible.at(object.layer))
        return false;
    if (!document.pages.isEmpty()) {
        const int masterIndex = (document.currentPage >= 0
                                 && document.currentPage < document.pages.size())
                                    ? document.pages.at(document.currentPage).master
                                    : -1;
        const bool onPage = object.page == document.currentPage;
        const bool masterObject = object.page < 0 && masterIndex >= 0
                                  && object.page == -1 - masterIndex;
        if (!onPage && !masterObject)
            return false;
    }
    return true;
}

struct ExportState {
    QJsonObject images;
    QByteArray blobs;
    int nextId = 1;
};

QString storeImage(ExportState &state, const QImage &image, bool *ok)
{
    *ok = false;
    if (image.isNull())
        return QString();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return QString();
    buffer.close();
    const QString key = QStringLiteral("img-")
                        + QString::fromLatin1(
                            QCryptographicHash::hash(png, QCryptographicHash::Sha1)
                                .toHex()
                                .left(16));
    if (!state.images.contains(key)) {
        QJsonArray blob;
        blob.append(state.blobs.size());
        blob.append(png.size());
        QJsonObject entry;
        entry.insert(QStringLiteral("mime"), QStringLiteral("image/png"));
        entry.insert(QStringLiteral("blob"), blob);
        state.images.insert(key, entry);
        state.blobs.append(png);
    }
    *ok = true;
    return key;
}

QJsonObject imageKind(const QRectF &localRect, const QImage &image,
                      const QTransform &toPoints, ExportState &state)
{
    bool ok = false;
    const QString key = storeImage(state, image, &ok);
    QJsonObject kind;
    if (!ok)
        return kind;
    QTransform mapping = toPoints;
    mapping.translate(localRect.left(), localRect.top());
    mapping.scale(localRect.width() / image.width(),
                  localRect.height() / image.height());
    kind.insert(QStringLiteral("type"), QStringLiteral("image"));
    kind.insert(QStringLiteral("key"), key);
    kind.insert(QStringLiteral("width"), image.width());
    kind.insert(QStringLiteral("height"), image.height());
    kind.insert(QStringLiteral("xf"), transformToJson(mapping));
    return kind;
}

QJsonObject rasterKind(const VectorObject &object, const QTransform &toPoints,
                       ExportState &state)
{
    const QRectF area = effectBounds(object).adjusted(-2.0, -2.0, 2.0, 2.0);
    if (area.width() < 1.0 || area.height() < 1.0)
        return QJsonObject();
    qreal scale = 2.0;
    const qreal maxDimension = qMax(area.width(), area.height());
    if (maxDimension * scale > 2000.0)
        scale = 2000.0 / maxDimension;
    QImage layer(qMax(1, qRound(area.width() * scale)),
                 qMax(1, qRound(area.height() * scale)),
                 QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter painter(&layer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.scale(scale, scale);
        painter.translate(-area.left(), -area.top());
        Document::paintObject(&painter, object);
    }
    return imageKind(area, layer, toPoints, state);
}

QJsonObject textKind(const VectorObject &object, const QTransform &toPoints)
{
    QFont font = object.font;
    if (font.pixelSize() <= 0)
        font.setPixelSize(qMax(1, qRound(font.pointSizeF() * kPointsToPixels)));
    const int pixelSize = qMax(1, font.pixelSize());

    QPainterPath frame;
    QRectF box = object.textBox;
    if (box.width() < 1.0 || box.height() < 1.0) {
        const QFontMetricsF metrics(font);
        const qreal width = qMax<qreal>(1.0, metrics.horizontalAdvance(object.text));
        box = QRectF(0.0, 0.0, width, metrics.height());
    }
    frame.addRect(box);

    QJsonObject frameKind;
    frameKind.insert(QStringLiteral("kind"), QStringLiteral("area"));
    frameKind.insert(QStringLiteral("frame"), pathToJson(frame));

    QJsonObject style;
    style.insert(QStringLiteral("font_family"), font.family());
    if (font.bold() && font.italic())
        style.insert(QStringLiteral("font_style"), QStringLiteral("Bold Italic"));
    else if (font.bold())
        style.insert(QStringLiteral("font_style"), QStringLiteral("Bold"));
    else if (font.italic())
        style.insert(QStringLiteral("font_style"), QStringLiteral("Italic"));
    else
        style.insert(QStringLiteral("font_style"), QStringLiteral("Regular"));
    style.insert(QStringLiteral("size"), double(pixelSize));
    style.insert(QStringLiteral("tracking"), object.letterSpacing);
    style.insert(QStringLiteral("baseline_shift"), 0.0);
    style.insert(QStringLiteral("h_scale"), 100.0);
    style.insert(QStringLiteral("v_scale"), 100.0);
    style.insert(QStringLiteral("rotation"), 0.0);
    style.insert(QStringLiteral("fill"), object.hasFill ? solidPaint(averageFill(object))
                                                        : nonePaint());
    style.insert(QStringLiteral("stroke"), object.hasStroke
                                               ? solidPaint(object.stroke)
                                               : nonePaint());
    style.insert(QStringLiteral("stroke_width"),
                 object.strokeWidth * kPixelsToPoints);
    style.insert(QStringLiteral("underline"), false);
    style.insert(QStringLiteral("strikethrough"), false);
    style.insert(QStringLiteral("all_caps"), false);

    QJsonObject run;
    run.insert(QStringLiteral("text"), object.text);
    run.insert(QStringLiteral("style"), style);
    QJsonArray runs;
    runs.append(run);

    QJsonObject para;
    if (object.textAlign == 1)
        para.insert(QStringLiteral("justify"), QStringLiteral("Center"));
    else if (object.textAlign == 2)
        para.insert(QStringLiteral("justify"), QStringLiteral("Right"));
    else
        para.insert(QStringLiteral("justify"), QStringLiteral("Left"));
    para.insert(QStringLiteral("left_indent"), 0.0);
    para.insert(QStringLiteral("right_indent"), 0.0);
    para.insert(QStringLiteral("first_line_indent"), 0.0);
    para.insert(QStringLiteral("space_before"), 0.0);
    para.insert(QStringLiteral("space_after"), 0.0);
    para.insert(QStringLiteral("hyphenate"), false);

    QJsonObject kind;
    kind.insert(QStringLiteral("type"), QStringLiteral("text"));
    kind.insert(QStringLiteral("kind"), frameKind);
    kind.insert(QStringLiteral("xf"), transformToJson(toPoints));
    kind.insert(QStringLiteral("runs"), runs);
    kind.insert(QStringLiteral("para"), para);
    return kind;
}

QJsonObject nodeForObject(const VectorObject &object, const QTransform &toPoints,
                          ExportState &state)
{
    QJsonObject node;
    if (needsRaster(object)) {
        const QJsonObject kind = rasterKind(object, toPoints, state);
        if (kind.isEmpty())
            return QJsonObject();
        node.insert(QStringLiteral("id"), state.nextId++);
        node.insert(QStringLiteral("kind"), kind);
        if (object.opacity < 0.999)
            node.insert(QStringLiteral("opacity"), object.opacity);
        if (!object.name.isEmpty())
            node.insert(QStringLiteral("name"), object.name);
        return node;
    }

    QJsonObject kind;
    if (object.type == ShapeType::Text) {
        kind = textKind(object, toPoints);
    } else if (object.type == ShapeType::Image && !object.image.isNull()) {
        QRectF box = object.textBox;
        if (box.width() < 1.0 || box.height() < 1.0)
            box = QRectF(0.0, 0.0, object.image.width(), object.image.height());
        kind = imageKind(box, object.image, toPoints, state);
        if (kind.isEmpty())
            return QJsonObject();
    } else {
        QPainterPath geometry = object.path;
        if (geometry.isEmpty() && object.type == ShapeType::Text)
            geometry = buildTextPath(object);
        if (geometry.isEmpty())
            return QJsonObject();
        kind.insert(QStringLiteral("type"), QStringLiteral("path"));
        kind.insert(QStringLiteral("path"), pathToJson(toPoints.map(geometry)));
    }

    node.insert(QStringLiteral("id"), state.nextId++);
    const QJsonObject appearance = appearanceToJson(object);
    if (!appearance.value(QStringLiteral("items")).toArray().isEmpty())
        node.insert(QStringLiteral("appearance"), appearance);
    if (object.opacity < 0.999)
        node.insert(QStringLiteral("opacity"), object.opacity);
    if (!object.name.isEmpty())
        node.insert(QStringLiteral("name"), object.name);
    node.insert(QStringLiteral("kind"), kind);
    return node;
}

QJsonObject defaultSwatch(const QString &name, const QJsonObject &paint)
{
    QJsonObject swatch;
    swatch.insert(QStringLiteral("name"), name);
    swatch.insert(QStringLiteral("paint"), paint);
    swatch.insert(QStringLiteral("global"), false);
    swatch.insert(QStringLiteral("spot"), false);
    return swatch;
}

} // namespace

bool isVectorCraftData(const QByteArray &data)
{
    const int marker = data.indexOf(blobMarker());
    const QByteArray json = marker >= 0 ? data.left(marker) : data;
    const int formatPos = json.indexOf("\"format\"");
    if (formatPos < 0)
        return false;
    return json.mid(formatPos, 64).contains("\"vectorcraft\"");
}

bool isVectorCraftFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return isVectorCraftData(file.readAll());
}

bool importFile(const QString &path, Document *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл: %1")
                         .arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();

    QJsonObject root;
    QByteArray blobs;
    if (!parseDocument(data, &root, &blobs, error))
        return false;
    if (root.value(QStringLiteral("format")).toString()
        != QLatin1String("vectorcraft")) {
        if (error)
            *error = QStringLiteral("Это не документ VectorCraft.");
        return false;
    }

    const QJsonObject documentJson = root.value(QStringLiteral("document")).toObject();
    if (documentJson.isEmpty()) {
        if (error)
            *error = QStringLiteral("В файле VectorCraft нет документа.");
        return false;
    }

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers.clear();
    document->layerVisible.clear();
    document->layerLocked.clear();
    document->groups.clear();
    document->blends.clear();
    document->symbols.clear();
    document->instances.clear();
    document->patterns.clear();
    document->masters.clear();

    ImportState state;
    state.document = document;
    state.patterns = documentJson.value(QStringLiteral("patterns")).toArray();

    // Raster images live after the JSON in a blob section.
    const QJsonObject images = root.value(QStringLiteral("images")).toObject();
    for (auto it = images.constBegin(); it != images.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QJsonArray range = entry.value(QStringLiteral("blob")).toArray();
        if (range.size() < 2)
            continue;
        const int offset = range.at(0).toInt();
        const int length = range.at(1).toInt();
        if (offset < 0 || length <= 0 || offset + length > blobs.size())
            continue;
        const QImage image = QImage::fromData(blobs.mid(offset, length));
        if (!image.isNull())
            state.images.insert(it.key(), image);
    }

    // Artboards become pages. Geometry is stored in points.
    const QJsonArray artboardArray =
        documentJson.value(QStringLiteral("artboards")).toArray();
    for (int index = 0; index < artboardArray.size(); ++index) {
        const QJsonObject artboard = artboardArray.at(index).toObject();
        const QJsonObject rect = artboard.value(QStringLiteral("rect")).toObject();
        const qreal x0 = rect.value(QStringLiteral("x0")).toDouble();
        const qreal y0 = rect.value(QStringLiteral("y0")).toDouble();
        const qreal x1 = rect.value(QStringLiteral("x1")).toDouble(x0 + 595.0);
        const qreal y1 = rect.value(QStringLiteral("y1")).toDouble(y0 + 842.0);
        const QSizeF size(qMax(1.0, (x1 - x0) * kPointsToPixels),
                          qMax(1.0, (y1 - y0) * kPointsToPixels));
        PageInfo page;
        page.name = artboard.value(QStringLiteral("name"))
                        .toString(QStringLiteral("Страница %1").arg(index + 1));
        page.size = size;
        page.background = QColor(Qt::white);
        document->pages.append(page);
        state.artboards.append(QRectF(QPointF(x0 * kPointsToPixels,
                                              y0 * kPointsToPixels),
                                      size));
        state.origins.append(QPointF(x0 * kPointsToPixels,
                                     y0 * kPointsToPixels));
    }
    if (document->pages.isEmpty()) {
        PageInfo page;
        page.name = QStringLiteral("Страница 1");
        page.size = QSizeF(794.0, 1123.0);
        page.background = QColor(Qt::white);
        document->pages.append(page);
        state.artboards.append(QRectF(QPointF(0, 0), page.size));
        state.origins.append(QPointF(0, 0));
    }

    const QJsonArray layers = documentJson.value(QStringLiteral("layers")).toArray();
    if (layers.isEmpty()) {
        document->layers.append(QStringLiteral("Слой 1"));
        document->layerVisible.append(true);
        document->layerLocked.append(false);
        importChildren(state,
                       documentJson.value(QStringLiteral("objects")).toArray(),
                       0, 1.0);
    } else {
        for (int index = 0; index < layers.size(); ++index) {
            const QJsonObject layer = layers.at(index).toObject();
            const QJsonObject kind = layer.value(QStringLiteral("kind")).toObject();
            const QString name = layer.value(QStringLiteral("name"))
                                     .toString(QStringLiteral("Слой %1").arg(index + 1));
            document->layers.append(name);
            document->layerVisible.append(true);
            document->layerLocked.append(false);
            importChildren(state, kind.value(QStringLiteral("children")).toArray(),
                           index, 1.0);
        }
    }

    // Assign every object to the artboard it belongs to and move it into that
    // page's local coordinate system.
    for (VectorObject object : state.objects) {
        const QRectF bounds = object.localBounds().translated(object.pos);
        const int page = pageForBounds(state, bounds);
        object.page = page;
        shiftObject(&object, state.origins.at(qBound(0, page, state.origins.size() - 1)));
        document->objects.append(object);
    }

    document->pageSize = document->pages.first().size;
    if (document->layers.isEmpty()) {
        document->layers.append(QStringLiteral("Слой 1"));
        document->layerVisible.append(true);
        document->layerLocked.append(false);
    }
    return !document->objects.isEmpty() || !document->pages.isEmpty();
}

bool exportFile(const Document &document, const QString &path, QString *error)
{
    ExportState state;

    const qreal gap = 20.0; // points between artboards
    QJsonArray artboards;
    QVector<QPointF> origins;
    const int pageCount = qMax(1, document.pages.size());
    qreal cursor = 0.0;
    for (int index = 0; index < pageCount; ++index) {
        const QSizeF pageSize = index < document.pages.size()
                                    ? document.pages.at(index).size
                                    : document.pageSize;
        const qreal width = pageSize.width() * kPixelsToPoints;
        const qreal height = pageSize.height() * kPixelsToPoints;
        QJsonObject rect;
        rect.insert(QStringLiteral("x0"), cursor);
        rect.insert(QStringLiteral("y0"), 0.0);
        rect.insert(QStringLiteral("x1"), cursor + width);
        rect.insert(QStringLiteral("y1"), height);
        QJsonObject artboard;
        artboard.insert(QStringLiteral("id"), index + 1);
        artboard.insert(QStringLiteral("name"),
                        index < document.pages.size() && !document.pages.at(index).name.isEmpty()
                            ? document.pages.at(index).name
                            : QStringLiteral("Artboard %1").arg(index + 1));
        artboard.insert(QStringLiteral("rect"), rect);
        artboard.insert(QStringLiteral("show_center_mark"), false);
        artboard.insert(QStringLiteral("show_cross_hairs"), false);
        artboards.append(artboard);
        origins.append(QPointF(cursor, 0.0));
        cursor += width + gap;
    }

    // One VectorCraft layer per Vectora layer; children are gathered from all
    // artboards because VectorCraft layers span the whole canvas.
    QJsonArray layers;
    const int layerCount = qMax(1, document.layers.size());
    state.nextId = layerCount + 1; // layers occupy ids 1..layerCount
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        QJsonArray children;
        for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
            Document pageDocument = document;
            pageDocument.currentPage = pageIndex < document.pages.size() ? pageIndex : 0;
            if (pageIndex < document.pages.size())
                pageDocument.pageSize = document.pages.at(pageIndex).size;
            for (const VectorObject &object : document.objects) {
                if (!objectVisibleOnPage(pageDocument, object))
                    continue;
                if (object.layer >= 0 && layerIndex < document.layers.size()
                    && object.layer != layerIndex)
                    continue;
                const QPointF origin =
                    origins.at(qBound(0, pageIndex, origins.size() - 1));
                QTransform toPoints = placementTransform(object);
                toPoints *= QTransform::fromScale(kPixelsToPoints, kPixelsToPoints);
                toPoints *= QTransform::fromTranslate(origin.x(), origin.y());
                const QJsonObject node = nodeForObject(object, toPoints, state);
                if (!node.isEmpty())
                    children.append(node);
            }
        }
        QJsonObject layerKind;
        layerKind.insert(QStringLiteral("type"), QStringLiteral("layer"));
        QJsonObject color;
        color.insert(QStringLiteral("Preset"), 0);
        layerKind.insert(QStringLiteral("color"), color);
        layerKind.insert(QStringLiteral("template"), false);
        layerKind.insert(QStringLiteral("printable"), true);
        layerKind.insert(QStringLiteral("children"), children);

        QJsonObject layer;
        layer.insert(QStringLiteral("id"), layerIndex + 1);
        layer.insert(QStringLiteral("name"),
                     layerIndex < document.layers.size()
                         ? document.layers.at(layerIndex)
                         : QStringLiteral("Layer 1"));
        layer.insert(QStringLiteral("kind"), layerKind);
        layers.append(layer);
    }

    QJsonArray swatches;
    swatches.append(defaultSwatch(QStringLiteral("[None]"), nonePaint()));
    swatches.append(defaultSwatch(QStringLiteral("White"),
                                  solidPaint(QColor(Qt::white))));
    swatches.append(defaultSwatch(QStringLiteral("Black"),
                                  solidPaint(QColor(Qt::black))));

    QJsonObject grid;
    grid.insert(QStringLiteral("spacing"), 10.0);
    grid.insert(QStringLiteral("subdivisions"), 2);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonObject metadata;
    metadata.insert(QStringLiteral("author"), QString());
    metadata.insert(QStringLiteral("authorTitle"), QString());
    metadata.insert(QStringLiteral("description"), QString());
    metadata.insert(QStringLiteral("keywords"), QJsonArray());
    metadata.insert(QStringLiteral("rating"), 0);
    metadata.insert(QStringLiteral("copyrightStatus"), QStringLiteral("unknown"));
    metadata.insert(QStringLiteral("copyrightNotice"), QString());
    metadata.insert(QStringLiteral("copyrightUrl"), QString());
    metadata.insert(QStringLiteral("created"), double(now));
    metadata.insert(QStringLiteral("modified"), double(now));

    QJsonObject center;
    center.insert(QStringLiteral("x"), 0.0);
    center.insert(QStringLiteral("y"), 0.0);
    QJsonObject lastView;
    lastView.insert(QStringLiteral("name"), QString());
    lastView.insert(QStringLiteral("center"), center);
    lastView.insert(QStringLiteral("zoom"), 1.0);
    lastView.insert(QStringLiteral("rotation"), 0.0);

    QJsonObject documentJson;
    documentJson.insert(QStringLiteral("version"), 1);
    documentJson.insert(QStringLiteral("title"),
                        QFileInfo(path).completeBaseName());
    documentJson.insert(QStringLiteral("units"), QStringLiteral("Points"));
    documentJson.insert(QStringLiteral("rulers"), QStringLiteral("artboard"));
    documentJson.insert(QStringLiteral("color_mode"), QStringLiteral("Rgb"));
    documentJson.insert(QStringLiteral("artboards"), artboards);
    documentJson.insert(QStringLiteral("layers"), layers);
    documentJson.insert(QStringLiteral("swatches"), swatches);
    documentJson.insert(QStringLiteral("swatch_groups"), QJsonArray());
    documentJson.insert(QStringLiteral("graphic_styles"), QJsonArray());
    documentJson.insert(QStringLiteral("symbols"), QJsonArray());
    documentJson.insert(QStringLiteral("guides"), QJsonArray());
    documentJson.insert(QStringLiteral("grid"), grid);
    documentJson.insert(QStringLiteral("raster_effects_ppi"), 300.0);
    documentJson.insert(QStringLiteral("images"), QJsonObject());
    documentJson.insert(QStringLiteral("patterns"), QJsonArray());
    documentJson.insert(QStringLiteral("next_id"), state.nextId);
    documentJson.insert(QStringLiteral("metadata"), metadata);
    documentJson.insert(QStringLiteral("last_view"), lastView);
    documentJson.insert(QStringLiteral("layers_open"), QJsonArray());

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectorcraft"));
    root.insert(QStringLiteral("version"), 4);
    root.insert(QStringLiteral("generator"), QStringLiteral("Vectora"));
    root.insert(QStringLiteral("document"), documentJson);
    root.insert(QStringLiteral("images"), state.images);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл для записи: %1")
                         .arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!state.blobs.isEmpty()) {
        file.write(blobMarker());
        file.write(state.blobs);
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить VectorCraft: %1")
                         .arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace VectorCraftIO
