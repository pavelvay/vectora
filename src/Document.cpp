#include "Document.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainterPath>
#include <QSaveFile>
#include <QTextDocument>
#include <QtMath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

QJsonArray pointArray(const QPointF &p)
{
    QJsonArray array;
    array.append(p.x());
    array.append(p.y());
    return array;
}

QPointF pointFromArray(const QJsonArray &array, int offset = 0)
{
    if (array.size() < offset + 2)
        return QPointF();
    return QPointF(array.at(offset).toDouble(), array.at(offset + 1).toDouble());
}

QJsonArray encodePath(const QPainterPath &path)
{
    QJsonArray out;
    int i = 0;
    while (i < path.elementCount()) {
        const QPainterPath::Element e = path.elementAt(i);
        QJsonObject command;
        if (e.type == QPainterPath::MoveToElement) {
            command.insert(QStringLiteral("c"), QStringLiteral("M"));
            command.insert(QStringLiteral("p"), pointArray(QPointF(e.x, e.y)));
            ++i;
        } else if (e.type == QPainterPath::LineToElement) {
            command.insert(QStringLiteral("c"), QStringLiteral("L"));
            command.insert(QStringLiteral("p"), pointArray(QPointF(e.x, e.y)));
            ++i;
        } else if (e.type == QPainterPath::CurveToElement) {
            if (i + 2 >= path.elementCount())
                break;
            const QPainterPath::Element c1 = path.elementAt(i);
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            QJsonArray points;
            points.append(c1.x);
            points.append(c1.y);
            points.append(c2.x);
            points.append(c2.y);
            points.append(end.x);
            points.append(end.y);
            command.insert(QStringLiteral("c"), QStringLiteral("C"));
            command.insert(QStringLiteral("p"), points);
            i += 3;
        } else {
            ++i;
            continue;
        }
        out.append(command);
    }
    return out;
}

QPainterPath decodePath(const QJsonArray &array)
{
    QPainterPath path;
    for (const QJsonValue &value : array) {
        const QJsonObject command = value.toObject();
        const QString type = command.value(QStringLiteral("c")).toString();
        const QJsonArray p = command.value(QStringLiteral("p")).toArray();
        if (type == QLatin1String("M")) {
            path.moveTo(pointFromArray(p));
        } else if (type == QLatin1String("L")) {
            path.lineTo(pointFromArray(p));
        } else if (type == QLatin1String("C") && p.size() >= 6) {
            path.cubicTo(QPointF(p.at(0).toDouble(), p.at(1).toDouble()),
                         QPointF(p.at(2).toDouble(), p.at(3).toDouble()),
                         QPointF(p.at(4).toDouble(), p.at(5).toDouble()));
        }
    }
    return path;
}

QString colorToJson(const QColor &color, bool enabled)
{
    return enabled && color.isValid() ? color.name(QColor::HexArgb) : QString();
}

QColor colorFromJson(const QJsonValue &value, bool *enabled, const QColor &fallback)
{
    if (value.isUndefined() || value.isNull() || value.toString().isEmpty()) {
        if (enabled)
            *enabled = false;
        return fallback;
    }
    const QColor color(value.toString());
    if (!color.isValid())
        return fallback;
    if (enabled)
        *enabled = true;
    return color;
}

QString xmlEscape(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

QString num(qreal value)
{
    return QString::number(value, 'f', 2);
}

QString svgPathData(const QPainterPath &path)
{
    QStringList parts;
    int i = 0;
    while (i < path.elementCount()) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.type == QPainterPath::MoveToElement) {
            parts << QStringLiteral("M %1 %2").arg(num(e.x), num(e.y));
            ++i;
        } else if (e.type == QPainterPath::LineToElement) {
            parts << QStringLiteral("L %1 %2").arg(num(e.x), num(e.y));
            ++i;
        } else if (e.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const auto c1 = path.elementAt(i);
            const auto c2 = path.elementAt(i + 1);
            const auto end = path.elementAt(i + 2);
            parts << QStringLiteral("C %1 %2 %3 %4 %5 %6")
                         .arg(num(c1.x), num(c1.y), num(c2.x), num(c2.y),
                              num(end.x), num(end.y));
            i += 3;
        } else {
            ++i;
        }
    }
    return parts.join(QLatin1Char(' '));
}

QImage imageFromJson(const QJsonValue &value)
{
    const QString base64 = value.toString();
    if (base64.isEmpty())
        return QImage();
    QImage image;
    image.loadFromData(QByteArray::fromBase64(base64.toLatin1()), "PNG");
    return image;
}

QString imageToJson(const QImage &image)
{
    if (image.isNull())
        return QString();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QPainterPath starPath(qreal outer, qreal inner, int points)
{
    QPainterPath path;
    for (int i = 0; i < points * 2; ++i) {
        const qreal radius = (i % 2 == 0) ? outer : inner;
        const qreal angle = -kPi / 2.0 + i * kPi / points;
        const QPointF p(radius * std::cos(angle), radius * std::sin(angle));
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

Qt::Alignment textAlignment(int align)
{
    Qt::Alignment horizontal = Qt::AlignLeft;
    if (align == 1)
        horizontal = Qt::AlignHCenter;
    else if (align == 2)
        horizontal = Qt::AlignRight;
    return horizontal | Qt::AlignTop;
}

QFont fontFromJson(const QJsonObject &object)
{
    QFont font(object.value(QStringLiteral("family")).toString(QStringLiteral("Adwaita Sans")));
    font.setPixelSize(qMax(1, object.value(QStringLiteral("size")).toInt(18)));
    font.setBold(object.value(QStringLiteral("bold")).toBool());
    font.setItalic(object.value(QStringLiteral("italic")).toBool());
    font.setUnderline(object.value(QStringLiteral("underline")).toBool());
    return font;
}

QJsonObject fontToJson(const QFont &font)
{
    QJsonObject object;
    object.insert(QStringLiteral("family"), font.family());
    object.insert(QStringLiteral("size"),
                  font.pixelSize() > 0 ? font.pixelSize() : 18);
    object.insert(QStringLiteral("bold"), font.bold());
    object.insert(QStringLiteral("italic"), font.italic());
    object.insert(QStringLiteral("underline"), font.underline());
    return object;
}

} // namespace

namespace {

QBrush fillBrush(const VectorObject &object, const QRectF &bounds)
{
    if (object.fillStyle == 1 && bounds.width() > 0.01 && bounds.height() > 0.01) {
        const qreal angle = object.gradientAngle * kPi / 180.0;
        const QPointF direction(std::cos(angle), std::sin(angle));
        const qreal half = std::hypot(bounds.width(), bounds.height()) / 2.0;
        QLinearGradient gradient(bounds.center() - direction * half,
                                 bounds.center() + direction * half);
        gradient.setColorAt(0.0, object.fill);
        gradient.setColorAt(1.0, object.fill2);
        return QBrush(gradient);
    }
    if (object.fillStyle == 2 && bounds.width() > 0.01 && bounds.height() > 0.01) {
        QRadialGradient gradient(bounds.center(),
                                 qMax(bounds.width(), bounds.height()) / 2.0);
        gradient.setColorAt(0.0, object.fill);
        gradient.setColorAt(1.0, object.fill2);
        return QBrush(gradient);
    }
    return QBrush(object.fill);
}

} // namespace

QString shapeTypeId(ShapeType type)
{
    switch (type) {
    case ShapeType::Rectangle: return QStringLiteral("rect");
    case ShapeType::Ellipse: return QStringLiteral("ellipse");
    case ShapeType::Polygon: return QStringLiteral("polygon");
    case ShapeType::Star: return QStringLiteral("star");
    case ShapeType::Spiral: return QStringLiteral("spiral");
    case ShapeType::Line: return QStringLiteral("line");
    case ShapeType::Bezier: return QStringLiteral("bezier");
    case ShapeType::Freehand: return QStringLiteral("freehand");
    case ShapeType::Artistic: return QStringLiteral("artistic");
    case ShapeType::Text: return QStringLiteral("text");
    case ShapeType::Image: return QStringLiteral("image");
    }
    return QStringLiteral("rect");
}

ShapeType shapeTypeFromId(const QString &id)
{
    if (id == QLatin1String("ellipse"))
        return ShapeType::Ellipse;
    if (id == QLatin1String("polygon"))
        return ShapeType::Polygon;
    if (id == QLatin1String("star"))
        return ShapeType::Star;
    if (id == QLatin1String("spiral"))
        return ShapeType::Spiral;
    if (id == QLatin1String("line"))
        return ShapeType::Line;
    if (id == QLatin1String("bezier"))
        return ShapeType::Bezier;
    if (id == QLatin1String("freehand"))
        return ShapeType::Freehand;
    if (id == QLatin1String("artistic"))
        return ShapeType::Artistic;
    if (id == QLatin1String("text"))
        return ShapeType::Text;
    if (id == QLatin1String("image"))
        return ShapeType::Image;
    return ShapeType::Rectangle;
}

QRectF VectorObject::localBounds() const
{
    if (type == ShapeType::Text || type == ShapeType::Image)
        return textBox;
    return path.boundingRect();
}

void Document::clear()
{
    objects.clear();
}

namespace {

QFont textFont(const VectorObject &object)
{
    QFont font = object.font;
    if (qAbs(object.letterSpacing) > 0.01)
        font.setLetterSpacing(QFont::AbsoluteSpacing, object.letterSpacing);
    return font;
}

QStringList wrapTextLines(const QString &text, const QFontMetricsF &metrics,
                          qreal width)
{
    QStringList result;
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString &paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            result.append(QString());
            continue;
        }
        const QStringList words = paragraph.split(QLatin1Char(' '));
        QString line;
        for (const QString &word : words) {
            const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > width) {
                result.append(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        result.append(line);
    }
    while (result.size() > 1 && result.last().isEmpty())
        result.removeLast();
    return result;
}

void drawTextLines(QPainter *painter, const VectorObject &object,
                   const QBrush &brush, const QPointF &offset)
{
    QFont font = textFont(object);
    QFontMetricsF metrics(font);
    const QStringList lines = wrapTextLines(object.text, metrics,
                                            object.textBox.width());
    const qreal lineHeight = metrics.height() * qMax<qreal>(0.5, object.lineSpacing);
    qreal y = object.textBox.top() + metrics.ascent() + offset.y();
    painter->setFont(font);
    painter->setPen(QPen(brush, 1.0));
    for (const QString &line : lines) {
        qreal x = object.textBox.left() + offset.x();
        const qreal width = metrics.horizontalAdvance(line);
        if (object.textAlign == 1)
            x += (object.textBox.width() - width) / 2.0;
        else if (object.textAlign == 2)
            x += object.textBox.width() - width;
        painter->drawText(QPointF(x, y), line);
        y += lineHeight;
    }
}

} // namespace

QPainterPath buildTextPath(const VectorObject &object)
{
    QPainterPath path;
    QFont font = textFont(object);
    QFontMetricsF metrics(font);
    const QStringList lines = wrapTextLines(object.text, metrics,
                                            object.textBox.width());
    const qreal lineHeight = metrics.height() * qMax<qreal>(0.5, object.lineSpacing);
    qreal y = object.textBox.top() + metrics.ascent();
    for (const QString &line : lines) {
        qreal x = object.textBox.left();
        const qreal width = metrics.horizontalAdvance(line);
        if (object.textAlign == 1)
            x += (object.textBox.width() - width) / 2.0;
        else if (object.textAlign == 2)
            x += object.textBox.width() - width;
        if (!line.isEmpty())
            path.addText(QPointF(x, y), font, line);
        y += lineHeight;
    }
    return path;
}

void Document::paintObject(QPainter *painter, const VectorObject &object)
{
    painter->save();
    painter->setOpacity(object.opacity);

    if (object.shadow) {
        painter->save();
        painter->translate(object.shadowOffset);
        QColor shadowColor = object.shadowColor;
        shadowColor.setAlphaF(qBound(0.0, object.shadowOpacity, 1.0)
                              * shadowColor.alphaF());
        if (!object.path.isEmpty() && object.hasFill) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(shadowColor);
            painter->drawPath(object.path);
        }
        if (object.type == ShapeType::Text && !object.text.isEmpty()) {
            QColor color = shadowColor;
            drawTextLines(painter, object, QBrush(color), object.shadowOffset);
        }
        painter->restore();
    }

    if (!object.path.isEmpty()) {
        if (object.hasFill && object.fill.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fillBrush(object, object.path.boundingRect()));
            painter->drawPath(object.path);
        }
        if (object.hasStroke && object.strokeWidth > 0.0 && object.stroke.alpha() > 0) {
            QPen pen(object.stroke, object.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(object.path);
        }
    }

    if (object.type == ShapeType::Text && !object.text.isEmpty()) {
        QBrush brush = fillBrush(object, object.textBox);
        drawTextLines(painter, object, brush, QPointF());
    }

    if (object.type == ShapeType::Image && !object.image.isNull())
        painter->drawImage(object.textBox, object.image);

    painter->restore();
}

void Document::render(QPainter *painter) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->fillRect(QRectF(0.0, 0.0, pageSize.width(), pageSize.height()), Qt::white);

    for (const VectorObject &object : objects) {
        if (object.layer >= 0 && object.layer < layerVisible.size()
            && !layerVisible.at(object.layer))
            continue;
        const QRectF bounds = object.localBounds();
        painter->save();
        painter->translate(object.pos);
        painter->translate(bounds.center());
        painter->rotate(object.rotation);
        painter->translate(-bounds.center());
        paintObject(painter, object);
        painter->restore();
    }
    painter->restore();
}

QPainterPath buildRectanglePath(const QRectF &bounds, qreal cornerRadius)
{
    QPainterPath path;
    const qreal radius = qBound(0.0, cornerRadius,
                                qMin(bounds.width(), bounds.height()) / 2.0);
    if (radius > 0.01)
        path.addRoundedRect(bounds, radius, radius);
    else
        path.addRect(bounds);
    return path;
}

QPainterPath buildPolygonPath(const QRectF &bounds, int sides)
{
    const int count = qBound(3, sides, 64);
    QPainterPath path;
    const QPointF center = bounds.center();
    const qreal rx = bounds.width() / 2.0;
    const qreal ry = bounds.height() / 2.0;
    for (int i = 0; i < count; ++i) {
        const qreal angle = -kPi / 2.0 + i * 2.0 * kPi / count;
        const QPointF point(center.x() + rx * std::cos(angle),
                            center.y() + ry * std::sin(angle));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

QPainterPath buildStarPath(const QRectF &bounds, int points, qreal ratio)
{
    const int count = qBound(3, points, 50);
    const qreal innerRatio = qBound(0.05, ratio, 0.95);
    QPainterPath path;
    const QPointF center = bounds.center();
    const qreal rx = bounds.width() / 2.0;
    const qreal ry = bounds.height() / 2.0;
    for (int i = 0; i < count * 2; ++i) {
        const qreal factor = (i % 2 == 0) ? 1.0 : innerRatio;
        const qreal angle = -kPi / 2.0 + i * kPi / count;
        const QPointF point(center.x() + rx * factor * std::cos(angle),
                            center.y() + ry * factor * std::sin(angle));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

QPainterPath buildSpiralPath(const QRectF &bounds, qreal turns)
{
    const qreal count = qBound(0.5, turns, 30.0);
    QPainterPath path;
    const QPointF center = bounds.center();
    const qreal rx = bounds.width() / 2.0;
    const qreal ry = bounds.height() / 2.0;
    const int steps = qMax(96, int(count * 120.0));
    for (int i = 0; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        const qreal angle = t * count * 2.0 * kPi;
        const QPointF point(center.x() + rx * t * std::cos(angle),
                            center.y() + ry * t * std::sin(angle));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    return path;
}

QColor interpolateColor(const QColor &from, const QColor &to, qreal t)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QJsonObject Document::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-document"));
    root.insert(QStringLiteral("version"), 1);

    QJsonObject page;
    page.insert(QStringLiteral("width"), pageSize.width());
    page.insert(QStringLiteral("height"), pageSize.height());
    root.insert(QStringLiteral("page"), page);

    QJsonArray layerArray;
    for (const QString &layer : layers)
        layerArray.append(layer);
    root.insert(QStringLiteral("layers"), layerArray);

    QJsonArray visibleArray;
    QJsonArray lockedArray;
    for (int i = 0; i < layers.size(); ++i) {
        visibleArray.append(i < layerVisible.size() ? layerVisible.at(i) : true);
        lockedArray.append(i < layerLocked.size() ? layerLocked.at(i) : false);
    }
    root.insert(QStringLiteral("layerVisible"), visibleArray);
    root.insert(QStringLiteral("layerLocked"), lockedArray);

    QJsonArray objectArray;
    for (const VectorObject &object : objects) {
        QJsonObject entry;
        entry.insert(QStringLiteral("type"), shapeTypeId(object.type));
        entry.insert(QStringLiteral("layer"), object.layer);
        entry.insert(QStringLiteral("group"), object.group);
        entry.insert(QStringLiteral("id"), object.id);
        entry.insert(QStringLiteral("masterId"), object.masterId);
        entry.insert(QStringLiteral("cornerRadius"), object.cornerRadius);
        entry.insert(QStringLiteral("sides"), object.sides);
        entry.insert(QStringLiteral("starPoints"), object.starPoints);
        entry.insert(QStringLiteral("starRatio"), object.starRatio);
        entry.insert(QStringLiteral("spiralTurns"), object.spiralTurns);
        entry.insert(QStringLiteral("pos"), pointArray(object.pos));
        entry.insert(QStringLiteral("rotation"), object.rotation);
        entry.insert(QStringLiteral("path"), encodePath(object.path));
        entry.insert(QStringLiteral("fill"), colorToJson(object.fill, object.hasFill));
        entry.insert(QStringLiteral("fill2"), object.fill2.name(QColor::HexArgb));
        entry.insert(QStringLiteral("fillStyle"), object.fillStyle);
        entry.insert(QStringLiteral("gradientAngle"), object.gradientAngle);
        entry.insert(QStringLiteral("pivotX"), object.pivot.x());
        entry.insert(QStringLiteral("pivotY"), object.pivot.y());
        entry.insert(QStringLiteral("stroke"), colorToJson(object.stroke, object.hasStroke));
        entry.insert(QStringLiteral("strokeWidth"), object.strokeWidth);
        entry.insert(QStringLiteral("opacity"), object.opacity);
        entry.insert(QStringLiteral("shadow"), object.shadow);
        entry.insert(QStringLiteral("locked"), object.locked);
        entry.insert(QStringLiteral("shadowOffset"),
                     pointArray(object.shadowOffset));
        entry.insert(QStringLiteral("shadowOpacity"), object.shadowOpacity);
        entry.insert(QStringLiteral("shadowColor"),
                     object.shadowColor.name(QColor::HexArgb));

        if (object.type == ShapeType::Text) {
            entry.insert(QStringLiteral("text"), object.text);
            entry.insert(QStringLiteral("font"), fontToJson(object.font));
            entry.insert(QStringLiteral("textBox"),
                         QJsonArray{object.textBox.x(), object.textBox.y(),
                                    object.textBox.width(), object.textBox.height()});
            entry.insert(QStringLiteral("textAlign"), object.textAlign);
            entry.insert(QStringLiteral("letterSpacing"), object.letterSpacing);
            entry.insert(QStringLiteral("lineSpacing"), object.lineSpacing);
        }
        if (object.type == ShapeType::Image) {
            entry.insert(QStringLiteral("textBox"),
                         QJsonArray{object.textBox.x(), object.textBox.y(),
                                    object.textBox.width(), object.textBox.height()});
            const QString encoded = imageToJson(object.image);
            if (!encoded.isEmpty())
                entry.insert(QStringLiteral("image"), encoded);
        }
        objectArray.append(entry);
    }
    QJsonArray groupArray;
    for (const VectorGroup &group : groups) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), group.id);
        entry.insert(QStringLiteral("parent"), group.parent);
        groupArray.append(entry);
    }
    root.insert(QStringLiteral("groups"), groupArray);

    QJsonArray guidesXArray;
    QJsonArray guidesYArray;
    for (qreal value : guidesX)
        guidesXArray.append(value);
    for (qreal value : guidesY)
        guidesYArray.append(value);
    root.insert(QStringLiteral("guidesX"), guidesXArray);
    root.insert(QStringLiteral("guidesY"), guidesYArray);

    root.insert(QStringLiteral("objects"), objectArray);
    return root;
}

Document Document::fromJson(const QJsonObject &json, QString *error)
{
    Document document;
    if (json.value(QStringLiteral("format")).toString() != QLatin1String("vectora-document")) {
        if (error)
            *error = QStringLiteral("Это не документ Vectora.");
        return document;
    }

    const QJsonObject page = json.value(QStringLiteral("page")).toObject();
    const qreal width = page.value(QStringLiteral("width")).toDouble(794.0);
    const qreal height = page.value(QStringLiteral("height")).toDouble(1123.0);
    if (width > 0.0 && height > 0.0)
        document.pageSize = QSizeF(width, height);

    const QJsonArray layerArray = json.value(QStringLiteral("layers")).toArray();
    if (!layerArray.isEmpty()) {
        document.layers.clear();
        for (const QJsonValue &value : layerArray) {
            const QString name = value.toString().trimmed();
            document.layers.append(name.isEmpty() ? QStringLiteral("Слой") : name);
        }
    }

    for (const QJsonValue &value : json.value(QStringLiteral("guidesX")).toArray())
        document.guidesX.append(value.toDouble());
    for (const QJsonValue &value : json.value(QStringLiteral("guidesY")).toArray())
        document.guidesY.append(value.toDouble());

    const QJsonArray groupArray = json.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &value : groupArray) {
        const QJsonObject entry = value.toObject();
        VectorGroup group;
        group.id = entry.value(QStringLiteral("id")).toInt();
        group.parent = entry.value(QStringLiteral("parent")).toInt();
        if (group.id > 0)
            document.groups.append(group);
    }

    document.layerVisible.fill(true, document.layers.size());
    document.layerLocked.fill(false, document.layers.size());
    const QJsonArray visibleArray = json.value(QStringLiteral("layerVisible")).toArray();
    const QJsonArray lockedArray = json.value(QStringLiteral("layerLocked")).toArray();
    for (int i = 0; i < document.layers.size(); ++i) {
        if (i < visibleArray.size())
            document.layerVisible[i] = visibleArray.at(i).toBool(true);
        if (i < lockedArray.size())
            document.layerLocked[i] = lockedArray.at(i).toBool(false);
    }

    const QJsonArray array = json.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject entry = value.toObject();
        VectorObject object;
        object.type = shapeTypeFromId(entry.value(QStringLiteral("type")).toString());
        object.layer = entry.value(QStringLiteral("layer")).toInt(0);
        object.group = entry.value(QStringLiteral("group")).toInt(0);
        object.id = entry.value(QStringLiteral("id")).toInt(0);
        object.masterId = entry.value(QStringLiteral("masterId")).toInt(0);
        object.cornerRadius = entry.value(QStringLiteral("cornerRadius")).toDouble(0.0);
        object.sides = entry.value(QStringLiteral("sides")).toInt(6);
        object.starPoints = entry.value(QStringLiteral("starPoints")).toInt(5);
        object.starRatio = entry.value(QStringLiteral("starRatio")).toDouble(0.45);
        object.spiralTurns = entry.value(QStringLiteral("spiralTurns")).toDouble(3.0);
        object.pos = pointFromArray(entry.value(QStringLiteral("pos")).toArray());
        object.rotation = entry.value(QStringLiteral("rotation")).toDouble(0.0);
        object.path = decodePath(entry.value(QStringLiteral("path")).toArray());
        object.fill = colorFromJson(entry.value(QStringLiteral("fill")), &object.hasFill,
                                    QColor("#4f7cc0"));
        const QColor fill2(entry.value(QStringLiteral("fill2"))
                               .toString(QStringLiteral("#ffffffff")));
        if (fill2.isValid())
            object.fill2 = fill2;
        object.fillStyle = entry.value(QStringLiteral("fillStyle")).toInt(0);
        object.gradientAngle = entry.value(QStringLiteral("gradientAngle")).toDouble(0.0);
        object.pivot = QPointF(entry.value(QStringLiteral("pivotX")).toDouble(0.5),
                               entry.value(QStringLiteral("pivotY")).toDouble(0.5));
        object.stroke = colorFromJson(entry.value(QStringLiteral("stroke")), &object.hasStroke,
                                      QColor("#1d2127"));
        object.strokeWidth = entry.value(QStringLiteral("strokeWidth")).toDouble(2.0);
        object.opacity = entry.value(QStringLiteral("opacity")).toDouble(1.0);
        object.shadow = entry.value(QStringLiteral("shadow")).toBool(false);
        object.locked = entry.value(QStringLiteral("locked")).toBool(false);
        const QJsonArray shadowOffset = entry.value(QStringLiteral("shadowOffset")).toArray();
        if (shadowOffset.size() >= 2) {
            object.shadowOffset = QPointF(shadowOffset.at(0).toDouble(),
                                          shadowOffset.at(1).toDouble());
        }
        object.shadowOpacity = entry.value(QStringLiteral("shadowOpacity")).toDouble(0.28);
        const QColor shadowColor(entry.value(QStringLiteral("shadowColor"))
                                     .toString(QStringLiteral("#ff000000")));
        if (shadowColor.isValid())
            object.shadowColor = shadowColor;

        if (object.type == ShapeType::Text) {
            object.text = entry.value(QStringLiteral("text")).toString();
            object.font = fontFromJson(entry.value(QStringLiteral("font")).toObject());
            object.letterSpacing = entry.value(QStringLiteral("letterSpacing")).toDouble(0.0);
            object.lineSpacing = entry.value(QStringLiteral("lineSpacing")).toDouble(1.0);
            const QJsonArray box = entry.value(QStringLiteral("textBox")).toArray();
            if (box.size() >= 4)
                object.textBox = QRectF(box.at(0).toDouble(), box.at(1).toDouble(),
                                        box.at(2).toDouble(), box.at(3).toDouble());
            object.textAlign = entry.value(QStringLiteral("textAlign")).toInt(0);
        }
        if (object.type == ShapeType::Image) {
            const QJsonArray box = entry.value(QStringLiteral("textBox")).toArray();
            if (box.size() >= 4)
                object.textBox = QRectF(box.at(0).toDouble(), box.at(1).toDouble(),
                                        box.at(2).toDouble(), box.at(3).toDouble());
            object.image = imageFromJson(entry.value(QStringLiteral("image")));
        }

        if (object.layer < 0 || object.layer >= document.layers.size())
            object.layer = 0;

        if (!object.path.isEmpty()
            || object.type == ShapeType::Text
            || object.type == ShapeType::Image) {
            document.objects.append(object);
        }
    }
    return document;
}

bool Document::save(const QString &path, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл для записи: %1")
                         .arg(file.errorString());
        return false;
    }

    const QJsonDocument json(toJson());
    if (file.write(json.toJson(QJsonDocument::Indented)) < 0) {
        if (error)
            *error = QStringLiteral("Ошибка записи: %1").arg(file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить файл: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool Document::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("Файл повреждён: %1").arg(parseError.errorString());
        return false;
    }
    if (!json.isObject()) {
        if (error)
            *error = QStringLiteral("Неизвестный формат файла.");
        return false;
    }

    const Document loaded = Document::fromJson(json.object(), error);
    if (error && !error->isEmpty())
        return false;

    pageSize = loaded.pageSize;
    layers = loaded.layers;
    groups = loaded.groups;
    guidesX = loaded.guidesX;
    guidesY = loaded.guidesY;
    layerVisible = loaded.layerVisible;
    layerLocked = loaded.layerLocked;
    objects = loaded.objects;
    return true;
}

QString Document::toSvg() const
{
    QString svg;
    svg += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                          "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                          "width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n")
               .arg(num(pageSize.width()), num(pageSize.height()));
    svg += QStringLiteral("  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n");

    int objectIndex = 0;
    for (const VectorObject &object : objects) {
        if (object.layer >= 0 && object.layer < layerVisible.size()
            && !layerVisible.at(object.layer))
            continue;
        const QRectF bounds = object.localBounds();
        const QString gradientId = QStringLiteral("grad%1").arg(objectIndex++);
        const QRectF fillBounds = object.type == ShapeType::Text ? object.textBox
                                                                 : object.path.boundingRect();
        const bool gradientFill = object.hasFill && object.fillStyle != 0
                                  && fillBounds.width() > 0.01
                                  && fillBounds.height() > 0.01;
        QString gradientMarkup;
        if (gradientFill) {
            if (object.fillStyle == 1) {
                const qreal angle = object.gradientAngle * kPi / 180.0;
                const QPointF direction(std::cos(angle), std::sin(angle));
                const qreal half = std::hypot(fillBounds.width(),
                                              fillBounds.height()) / 2.0;
                const QPointF start = fillBounds.center() - direction * half;
                const QPointF end = fillBounds.center() + direction * half;
                gradientMarkup = QStringLiteral(
                    "<defs><linearGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" "
                    "x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\">"
                    "<stop offset=\"0\" stop-color=\"%6\" stop-opacity=\"%7\"/>"
                    "<stop offset=\"1\" stop-color=\"%8\" stop-opacity=\"%9\"/>"
                    "</linearGradient></defs>")
                                     .arg(gradientId, num(start.x()), num(start.y()),
                                          num(end.x()), num(end.y()),
                                          object.fill.name(QColor::HexRgb),
                                          num(object.fill.alphaF()),
                                          object.fill2.name(QColor::HexRgb),
                                          num(object.fill2.alphaF()));
            } else {
                gradientMarkup = QStringLiteral(
                    "<defs><radialGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" "
                    "cx=\"%2\" cy=\"%3\" r=\"%4\">"
                    "<stop offset=\"0\" stop-color=\"%5\" stop-opacity=\"%6\"/>"
                    "<stop offset=\"1\" stop-color=\"%7\" stop-opacity=\"%8\"/>"
                    "</radialGradient></defs>")
                                     .arg(gradientId,
                                          num(fillBounds.center().x()),
                                          num(fillBounds.center().y()),
                                          num(qMax(fillBounds.width(),
                                                   fillBounds.height()) / 2.0),
                                          object.fill.name(QColor::HexRgb),
                                          num(object.fill.alphaF()),
                                          object.fill2.name(QColor::HexRgb),
                                          num(object.fill2.alphaF()));
            }
        }

        svg += QStringLiteral("  <g transform=\"translate(%1,%2) rotate(%3 %4 %5)\" opacity=\"%6\">\n")
                   .arg(num(object.pos.x()), num(object.pos.y()), num(object.rotation),
                        num(bounds.center().x()), num(bounds.center().y()),
                        num(object.opacity));
        if (!gradientMarkup.isEmpty())
            svg += QStringLiteral("    ") + gradientMarkup + QStringLiteral("\n");

        if (object.shadow && !object.path.isEmpty() && object.hasFill) {
            const QColor shadow = object.shadowColor;
            svg += QStringLiteral("    <path d=\"%1\" fill=\"%2\" fill-opacity=\"%3\" "
                                  "transform=\"translate(%4,%5)\"/>\n")
                       .arg(svgPathData(object.path), shadow.name(QColor::HexRgb),
                            num(object.shadowOpacity * shadow.alphaF()),
                            num(object.shadowOffset.x()), num(object.shadowOffset.y()));
        }

        if (!object.path.isEmpty()) {
            const QString fill = gradientFill
                                     ? QStringLiteral("url(#%1)").arg(gradientId)
                                     : (object.hasFill
                                            ? object.fill.name(QColor::HexRgb)
                                            : QStringLiteral("none"));
            const QString stroke = object.hasStroke
                                       ? object.stroke.name(QColor::HexRgb)
                                       : QStringLiteral("none");
            QString opacityAttributes;
            if (object.hasFill && !gradientFill && object.fill.alpha() < 255)
                opacityAttributes += QStringLiteral(" fill-opacity=\"%1\"")
                                         .arg(num(object.fill.alphaF()));
            if (object.hasStroke && object.stroke.alpha() < 255)
                opacityAttributes += QStringLiteral(" stroke-opacity=\"%1\"")
                                         .arg(num(object.stroke.alphaF()));

            svg += QStringLiteral("    <path d=\"%1\" fill=\"%2\" stroke=\"%3\" "
                                  "stroke-width=\"%4\" stroke-linejoin=\"round\" "
                                  "stroke-linecap=\"round\"%5/>\n")
                       .arg(svgPathData(object.path), fill, stroke,
                            num(object.strokeWidth), opacityAttributes);
        }

        if (object.type == ShapeType::Text && !object.text.isEmpty()) {
            const QStringList lines = object.text.split(QLatin1Char('\n'));
            QFontMetricsF metrics(object.font);
            const qreal lineHeight = metrics.lineSpacing()
                                     * qMax<qreal>(0.5, object.lineSpacing);
            const qreal ascent = metrics.ascent();
            const QString anchor = object.textAlign == 1
                                       ? QStringLiteral("middle")
                                       : (object.textAlign == 2 ? QStringLiteral("end")
                                                                : QStringLiteral("start"));
            const qreal x = object.textAlign == 1
                                ? object.textBox.width() / 2.0
                                : (object.textAlign == 2 ? object.textBox.width() : 0.0);
            for (int i = 0; i < lines.size(); ++i) {
                QString textOpacity;
                if (!gradientFill && object.fill.alpha() < 255)
                    textOpacity = QStringLiteral(" fill-opacity=\"%1\"")
                                      .arg(num(object.fill.alphaF()));
                const QString textFill = gradientFill
                                             ? QStringLiteral("url(#%1)").arg(gradientId)
                                             : object.fill.name(QColor::HexRgb);
                svg += QStringLiteral("    <text x=\"%1\" y=\"%2\" fill=\"%3\" "
                                      "font-family=\"%4\" font-size=\"%5\" "
                                      "font-weight=\"%6\" text-anchor=\"%7\" "
                                      "letter-spacing=\"%10\"%8>%9</text>\n")
                           .arg(num(x), num(ascent + i * lineHeight),
                                textFill,
                                xmlEscape(object.font.family()),
                                num(object.font.pixelSize() > 0
                                        ? object.font.pixelSize()
                                        : 18.0),
                                object.font.bold() ? QStringLiteral("bold")
                                                   : QStringLiteral("normal"),
                                anchor, textOpacity, xmlEscape(lines.at(i)),
                                num(object.letterSpacing));
            }
        }

        if (object.type == ShapeType::Image && !object.image.isNull()) {
            svg += QStringLiteral("    <image x=\"0\" y=\"0\" width=\"%1\" height=\"%2\" "
                                  "xlink:href=\"data:image/png;base64,%3\"/>\n")
                       .arg(num(object.textBox.width()), num(object.textBox.height()),
                            imageToJson(object.image));
        }

        svg += QStringLiteral("  </g>\n");
    }

    svg += QStringLiteral("</svg>\n");
    return svg;
}

Document Document::sample()
{
    Document document;

    const auto makeText = [](const QString &text, const QRectF &box, int pixelSize,
                             const QColor &color, int align, bool bold) {
        VectorObject object;
        object.type = ShapeType::Text;
        object.text = text;
        object.pos = box.topLeft();
        object.textBox = QRectF(0.0, 0.0, box.width(), box.height());
        object.fill = color;
        object.hasFill = true;
        object.hasStroke = false;
        object.textAlign = align;
        QFont font(QStringLiteral("Adwaita Sans"));
        font.setPixelSize(pixelSize);
        font.setBold(bold);
        object.font = font;
        return object;
    };

    document.objects.append(makeText(QStringLiteral("VECTORA"),
                                     QRectF(0, 96, 794, 76), 58, QColor("#1d2127"), 1, true));
    document.objects.append(makeText(QStringLiteral("векторный редактор"),
                                     QRectF(0, 182, 794, 30), 19, QColor("#6b7280"), 1, false));

    const auto makePath = [](ShapeType type, const QPainterPath &path, const QPointF &pos,
                             const QColor &fill, const QColor &stroke, qreal strokeWidth) {
        VectorObject object;
        object.type = type;
        object.path = path;
        object.pos = pos;
        object.fill = fill;
        object.hasFill = fill.alpha() > 0;
        object.stroke = stroke;
        object.hasStroke = stroke.alpha() > 0 && strokeWidth > 0.0;
        object.strokeWidth = strokeWidth;
        return object;
    };

    QPainterPath rule;
    rule.moveTo(0, 0);
    rule.lineTo(140, 0);
    document.objects.append(makePath(ShapeType::Line, rule, QPointF(327, 236),
                                     QColor(Qt::transparent), QColor("#4f7cc0"), 4));

    QPainterPath card;
    card.addRoundedRect(QRectF(0, 0, 250, 250), 28, 28);
    VectorObject cardObject = makePath(ShapeType::Rectangle, card, QPointF(100, 320),
                                       QColor("#6b8cf0"), QColor(Qt::transparent), 0);
    cardObject.rotation = -6;
    cardObject.cornerRadius = 28;
    cardObject.fill = QColor("#5b8def");
    cardObject.fill2 = QColor("#8a5cf6");
    cardObject.fillStyle = 1;
    cardObject.gradientAngle = 45;
    document.objects.append(cardObject);

    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, 176, 176));
    document.objects.append(makePath(ShapeType::Ellipse, circle, QPointF(438, 352),
                                     QColor("#ff8a3d"), QColor(Qt::transparent), 0));

    QPainterPath star = starPath(78, 33, 5);
    VectorObject starObject = makePath(ShapeType::Star, star, QPointF(660, 640),
                                       QColor("#16a3a3"), QColor("#0f7f7f"), 3);
    starObject.starPoints = 5;
    starObject.starRatio = 33.0 / 78.0;
    document.objects.append(starObject);

    QPainterPath dot;
    dot.addEllipse(QRectF(0, 0, 54, 54));
    document.objects.append(makePath(ShapeType::Ellipse, dot, QPointF(500, 690),
                                     QColor("#1d2127"), QColor(Qt::transparent), 0));

    QPainterPath curve;
    curve.moveTo(0, 0);
    curve.cubicTo(130, -70, 330, 80, 520, -10);
    document.objects.append(makePath(ShapeType::Bezier, curve, QPointF(128, 742),
                                     QColor(Qt::transparent), QColor("#e0466b"), 5));

    document.objects.append(makeText(QStringLiteral("Ваш дизайн\nздесь"),
                                     QRectF(100, 842, 520, 120), 44, QColor("#1d2127"), 0, true));
    document.objects.append(makeText(QStringLiteral("интерфейс · этап 2"),
                                     QRectF(102, 990, 320, 26), 15, QColor("#8b929d"), 0, false));
    document.objects.append(makeText(QStringLiteral("A4 · 210 × 297 мм"),
                                     QRectF(586, 1064, 180, 26), 15, QColor("#9aa1ac"), 2, false));

    for (VectorObject &object : document.objects)
        object.layer = 0;
    document.layerVisible = {true};
    document.layerLocked = {false};

    return document;
}
