#include "Document.h"

#include "Patterns.h"

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
#include <QTransform>
#include <QtMath>

#include <cmath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kPxPerMm = 3.7795275591;

QString dimensionValueText(const VectorObject &object, qreal pixels)
{
    qreal value = pixels;
    QString suffix;
    switch (object.dimUnits) {
    case 3:
        suffix = QStringLiteral(" px");
        break;
    case 2:
        value = pixels / 96.0 * object.dimScale;
        suffix = QStringLiteral(" in");
        break;
    case 1:
        value = pixels / kPxPerMm * 10.0 * object.dimScale;
        suffix = QStringLiteral(" см");
        break;
    default:
        value = pixels / kPxPerMm * object.dimScale;
        suffix = QStringLiteral(" мм");
        break;
    }
    return QString::number(value, 'f', qBound(0, object.dimPrecision, 4)) + suffix;
}

void drawDimensionArrow(QPainter *painter, const QPointF &tip, const QPointF &direction,
                        const QColor &color, qreal size = 7.0)
{
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length < 0.001)
        return;
    const QPointF unit(direction.x() / length, direction.y() / length);
    const QPointF normal(-unit.y(), unit.x());
    QPolygonF arrow;
    arrow << tip
          << tip - unit * size + normal * (size * 0.36)
          << tip - unit * size - normal * (size * 0.36);
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(arrow);
    painter->restore();
}

void drawDimensionText(QPainter *painter, const VectorObject &object, const QPointF &center,
                       const QString &text, qreal rotation = 0.0)
{
    QFont font = object.font;
    if (font.pixelSize() <= 0)
        font.setPixelSize(11);
    painter->save();
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const QRectF box = metrics.boundingRect(text).adjusted(-3.0, -1.0, 3.0, 1.0);
    painter->translate(center);
    painter->rotate(rotation);
    const QRectF background(box.center().x() - box.width() / 2.0,
                            -box.height() - 4.0, box.width(), box.height());
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 220));
    painter->drawRoundedRect(background.adjusted(-2.0, -1.0, 2.0, 1.0), 2.0, 2.0);
    QColor textColor = object.fill;
    if (!textColor.isValid() || textColor.alpha() == 0)
        textColor = object.stroke;
    painter->setPen(textColor);
    painter->drawText(QPointF(-metrics.horizontalAdvance(text) / 2.0,
                              -metrics.ascent() - 4.0),
                      text);
    painter->restore();
}

void paintDimension(QPainter *painter, const VectorObject &object)
{
    painter->save();
    QColor color = object.stroke.isValid() ? object.stroke : QColor(0x1d, 0x21, 0x27);
    const qreal width = qMax<qreal>(0.8, object.strokeWidth);
    QPen linePen(color, width);
    linePen.setCapStyle(Qt::RoundCap);
    painter->setPen(linePen);
    painter->setBrush(Qt::NoBrush);

    const QPointF a = object.dimA;
    const QPointF b = object.dimB;
    const QPointF c = object.dimC;
    const qreal offset = object.dimOffset;

    if (object.dimensionType == 3) {
        // Angular dimension: a is the vertex, b and c are points on the rays.
        painter->setRenderHint(QPainter::Antialiasing, true);
        const qreal a1 = std::atan2(b.y() - a.y(), b.x() - a.x());
        const qreal a2 = std::atan2(c.y() - a.y(), c.x() - a.x());
        qreal sweep = a2 - a1;
        while (sweep <= -kPi)
            sweep += 2.0 * kPi;
        while (sweep > kPi)
            sweep -= 2.0 * kPi;
        const qreal radius = qMax<qreal>(18.0, qAbs(offset));
        const qreal r = radius;
        QRectF arcBox(a.x() - r, a.y() - r, r * 2.0, r * 2.0);
        QPainterPath arc;
        arc.arcMoveTo(arcBox, -a1 * 180.0 / kPi);
        arc.arcTo(arcBox, -a1 * 180.0 / kPi, -sweep * 180.0 / kPi);
        painter->drawPath(arc);
        painter->drawLine(a, a + QPointF(std::cos(a1), std::sin(a1)) * (r + 8.0));
        painter->drawLine(a, a + QPointF(std::cos(a2), std::sin(a2)) * (r + 8.0));

        const qreal mid = a1 + sweep / 2.0;
        const QPointF midPoint(a.x() + std::cos(mid) * (r + 14.0),
                               a.y() + std::sin(mid) * (r + 14.0));
        const qreal degrees = qAbs(sweep) * 180.0 / kPi;
        QString text = QString::number(degrees, 'f', qBound(0, object.dimPrecision, 4))
                       + QStringLiteral("°");
        drawDimensionText(painter, object, midPoint, text);
        painter->restore();
        return;
    }

    if (object.dimensionType == 4 || object.dimensionType == 5) {
        // Radius / diameter: leader from the centre through the edge point.
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPointF delta = b - a;
        const qreal pixels = std::hypot(delta.x(), delta.y());
        if (pixels < 0.5) {
            painter->restore();
            return;
        }
        const QPointF unit(delta.x() / pixels, delta.y() / pixels);
        const QPointF start = object.dimensionType == 4 ? a : a - unit * pixels;
        painter->drawLine(start, b);
        drawDimensionArrow(painter, b, unit, color);
        if (object.dimensionType == 5)
            drawDimensionArrow(painter, start, -unit, color);
        QString text = (object.dimensionType == 4 ? QStringLiteral("R ")
                                                 : QStringLiteral("Ø "))
                       + dimensionValueText(object, pixels);
        const QPointF normal(-unit.y(), unit.x());
        const QPointF center = (start + b) / 2.0 + normal * 12.0;
        qreal rotation = std::atan2(unit.y(), unit.x()) * 180.0 / kPi;
        if (rotation > 90.0 || rotation < -90.0)
            rotation += 180.0;
        drawDimensionText(painter, object, center, text, rotation);
        painter->restore();
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    QPointF p1;
    QPointF p2;
    QPointF dim1;
    QPointF dim2;
    QString text;
    qreal textRotation = 0.0;

    if (object.dimensionType == 0) {
        const qreal y = qMax(a.y(), b.y()) + offset;
        p1 = a;
        p2 = b;
        dim1 = QPointF(a.x(), y);
        dim2 = QPointF(b.x(), y);
        text = dimensionValueText(object, qAbs(b.x() - a.x()));
    } else if (object.dimensionType == 1) {
        const qreal x = qMax(a.x(), b.x()) + offset;
        p1 = a;
        p2 = b;
        dim1 = QPointF(x, a.y());
        dim2 = QPointF(x, b.y());
        text = dimensionValueText(object, qAbs(b.y() - a.y()));
        textRotation = -90.0;
    } else {
        QPointF direction = b - a;
        const qreal length = std::hypot(direction.x(), direction.y());
        if (length < 0.5) {
            painter->restore();
            return;
        }
        direction /= length;
        const QPointF normal(-direction.y(), direction.x());
        dim1 = a + normal * offset;
        dim2 = b + normal * offset;
        p1 = a;
        p2 = b;
        text = dimensionValueText(object, length);
        textRotation = std::atan2(direction.y(), direction.x()) * 180.0 / kPi;
        if (textRotation > 90.0 || textRotation < -90.0)
            textRotation += 180.0;
    }

    painter->drawLine(p1 + QPointF(0.0, 0.0), dim1);
    painter->drawLine(p2, dim2);
    painter->drawLine(dim1, dim2);
    const QPointF d1 = dim2 - dim1;
    const QPointF d2 = dim1 - dim2;
    drawDimensionArrow(painter, dim1, d2, color);
    drawDimensionArrow(painter, dim2, d1, color);

    drawDimensionText(painter, object, (dim1 + dim2) / 2.0, text, textRotation);
    painter->restore();
}



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

void applyGradientStops(QGradient &gradient, const VectorObject &object)
{
    if (object.gradientStops.size() >= 2) {
        for (const GradientStop &stop : object.gradientStops) {
            QColor color = stop.color;
            color.setAlphaF(qBound(0.0, color.alphaF() * stop.opacity, 1.0));
            gradient.setColorAt(qBound(0.0, stop.position, 1.0), color);
        }
        return;
    }
    gradient.setColorAt(0.0, object.fill);
    gradient.setColorAt(1.0, object.fill2);
}

QBrush fillBrush(const VectorObject &object, const QRectF &bounds)
{
    if (object.fillStyle == 0 || bounds.width() <= 0.01 || bounds.height() <= 0.01)
        return QBrush(object.fill);

    const QPointF center(bounds.left() + bounds.width() * object.gradientCenter.x(),
                         bounds.top() + bounds.height() * object.gradientCenter.y());
    const qreal radius = qMax<qreal>(0.01, object.gradientRadius)
                         * qMax(bounds.width(), bounds.height());

    if (object.fillStyle == 1 || object.fillStyle == 4) {
        const qreal angle = object.gradientAngle * kPi / 180.0;
        const QPointF direction(std::cos(angle), std::sin(angle));
        const qreal half = std::hypot(bounds.width(), bounds.height()) / 2.0;
        QLinearGradient gradient(bounds.center() - direction * half,
                                 bounds.center() + direction * half);
        applyGradientStops(gradient, object);
        return QBrush(gradient);
    }
    if (object.fillStyle == 2) {
        QRadialGradient gradient(bounds.center(),
                                 qMax(bounds.width(), bounds.height()) / 2.0);
        applyGradientStops(gradient, object);
        return QBrush(gradient);
    }
    if (object.fillStyle == 3) {
        QConicalGradient gradient(center, object.gradientAngle);
        applyGradientStops(gradient, object);
        return QBrush(gradient);
    }
    if (object.fillStyle == 5) {
        QRadialGradient gradient(center, radius, center);
        applyGradientStops(gradient, object);
        return QBrush(gradient);
    }
    if (object.fillStyle == 6) {
        constexpr int size = 64;
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        const QColor corners[4] = {object.fill, object.fill2,
                                   object.meshColor3, object.meshColor4};
        for (int y = 0; y < size; ++y) {
            const qreal v = qreal(y) / (size - 1);
            for (int x = 0; x < size; ++x) {
                const qreal u = qreal(x) / (size - 1);
                const auto blend = [](const QColor &a, const QColor &b, qreal t) {
                    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                            a.greenF() + (b.greenF() - a.greenF()) * t,
                                            a.blueF() + (b.blueF() - a.blueF()) * t,
                                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
                };
                const QColor top = blend(corners[0], corners[1], u);
                const QColor bottom = blend(corners[2], corners[3], u);
                image.setPixelColor(x, y, blend(top, bottom, v));
            }
        }
        QBrush brush(image);
        QTransform transform;
        transform.translate(bounds.left(), bounds.top());
        transform.scale(bounds.width() / size, bounds.height() / size);
        brush.setTransform(transform);
        return brush;
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
    case ShapeType::Dimension: return QStringLiteral("dimension");
    case ShapeType::Code: return QStringLiteral("code");
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
    if (id == QLatin1String("dimension"))
        return ShapeType::Dimension;
    if (id == QLatin1String("code"))
        return ShapeType::Code;
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
    if (type == ShapeType::Code)
        return textBox;
    if (type == ShapeType::Dimension) {
        QRectF box(dimA, QSizeF(0.0, 0.0));
        box = box.united(QRectF(dimB, QSizeF(0.0, 0.0)));
        box = box.united(QRectF(dimC, QSizeF(0.0, 0.0)));
        const qreal margin = qAbs(dimOffset) + 42.0;
        return box.adjusted(-margin, -margin, margin, margin);
    }
    return path.boundingRect();
}

void Document::clear()
{
    objects.clear();
}

namespace {

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
QFont::Tag makeFontTag(const QString &name)
{
    char tag[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 4 && i < name.size(); ++i)
        tag[i] = name.at(i).toLatin1();
    return QFont::Tag(tag);
}
#endif

QFont textFont(const VectorObject &object)
{
    QFont font = object.font;
    if (qAbs(object.letterSpacing) > 0.01)
        font.setLetterSpacing(QFont::AbsoluteSpacing, object.letterSpacing);
    if (qAbs(object.wordSpacing) > 0.01)
        font.setWordSpacing(object.wordSpacing);
    if (qAbs(object.horizontalScale - 1.0) > 0.001)
        font.setStretch(qBound(1, qRound(object.horizontalScale * 100.0), 4000));
    if (object.underline)
        font.setUnderline(true);
    if (object.strikeOut)
        font.setStrikeOut(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    for (const QString &feature : object.fontFeatures) {
        if (feature.size() == 4)
            font.setFeature(makeFontTag(feature), 1);
    }
    for (auto it = object.variableAxes.constBegin();
         it != object.variableAxes.constEnd(); ++it) {
        if (it.key().size() == 4)
            font.setVariableAxis(makeFontTag(it.key()), it.value());
    }
#endif
    if (object.script == 1 || object.script == 2) {
        const qreal size = font.pixelSize() > 0 ? font.pixelSize() : 18.0;
        font.setPixelSize(qMax(1, qRound(size * 0.66)));
    }
    return font;
}

struct TextLineInfo {
    QString text;
    bool paragraphEnd = false;
};

QVector<TextLineInfo> layoutText(const QString &text, const QFontMetricsF &metrics,
                                 qreal width)
{
    QVector<TextLineInfo> result;
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString &paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            result.append({QString(), true});
            continue;
        }
        const QStringList words = paragraph.split(QLatin1Char(' '));
        QString line;
        for (const QString &word : words) {
            const QString candidate = line.isEmpty() ? word
                                                     : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > width) {
                result.append({line, false});
                line = word;
            } else {
                line = candidate;
            }
        }
        result.append({line, true});
    }
    while (result.size() > 1 && result.last().text.isEmpty())
        result.removeLast();
    return result;
}

QStringList wrapTextLines(const QString &text, const QFontMetricsF &metrics,
                          qreal width)
{
    QStringList result;
    for (const TextLineInfo &line : layoutText(text, metrics, width))
        result.append(line.text);
    return result;
}

qreal scriptOffset(const VectorObject &object, const QFontMetricsF &metrics)
{
    if (object.script == 1)
        return -metrics.ascent() * 0.45;
    if (object.script == 2)
        return metrics.descent() * 0.6;
    return 0.0;
}

void drawTextLines(QPainter *painter, const VectorObject &object,
                   const QBrush &brush, const QPointF &offset)
{
    QFont font = textFont(object);
    QFontMetricsF metrics(font);
    const QVector<TextLineInfo> lines = layoutText(object.text, metrics,
                                                   object.textBox.width());
    const qreal lineHeight = metrics.height() * qMax<qreal>(0.5, object.lineSpacing);
    qreal y = object.textBox.top() + metrics.ascent() + offset.y()
              + object.baselineShift + scriptOffset(object, metrics);
    painter->setFont(font);
    painter->setPen(QPen(brush, 1.0));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i).text;
        qreal x = object.textBox.left() + offset.x()
                  + (i == 0 ? object.firstLineIndent : 0.0);
        const qreal width = metrics.horizontalAdvance(line);
        if (object.textAlign == 1)
            x += (object.textBox.width() - width) / 2.0;
        else if (object.textAlign == 2)
            x += object.textBox.width() - width;

        if (qAbs(object.verticalScale - 1.0) > 0.001) {
            painter->save();
            painter->translate(0.0, y);
            painter->scale(1.0, object.verticalScale);
            painter->drawText(QPointF(x, 0.0), line);
            painter->restore();
        } else {
            painter->drawText(QPointF(x, y), line);
        }
        y += lineHeight;
        if (lines.at(i).paragraphEnd)
            y += object.paragraphSpacing;
    }
}

QPainterPath buildTextOnPathGeometry(const VectorObject &object)
{
    QPainterPath result;
    if (object.textPathCurve.isEmpty() || object.text.isEmpty())
        return result;

    QFont font = textFont(object);
    QFontMetricsF metrics(font);
    const qreal length = object.textPathCurve.length();
    if (length <= 0.01)
        return result;

    const QString text = object.text.simplified();
    const qreal start = qBound(0.0, object.textPathOffset, 1.0);
    qreal advance = start * length;

    for (const QChar &character : text) {
        const QString glyphText(character);
        const qreal glyphWidth = metrics.horizontalAdvance(glyphText);
        const qreal center = advance + glyphWidth / 2.0;
        if (center > length)
            break;
        qreal t = center / length;
        if (object.textPathReverse)
            t = 1.0 - t;
        const QPointF position = object.textPathCurve.pointAtPercent(t);
        qreal angle = object.textPathCurve.angleAtPercent(t);
        if (object.textPathReverse)
            angle += 180.0;

        QTransform transform;
        transform.translate(position.x(), position.y());
        transform.rotate(-angle);
        const QPainterPath glyph = transform.map(
            [&]() {
                QPainterPath path;
                path.addText(QPointF(-glyphWidth / 2.0, 0.0), font, glyphText);
                return path;
            }());
        result.addPath(glyph);
        advance += glyphWidth + object.letterSpacing;
    }
    return result;
}

} // namespace

QPainterPath buildTextPath(const VectorObject &object)
{
    if (object.type == ShapeType::Text && object.textOnPath
        && !object.textPathCurve.isEmpty())
        return buildTextOnPathGeometry(object);

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

namespace {

QPainterPath dimensionOutlineImpl(const VectorObject &object)
{
    QPainterPath result;
    if (object.type != ShapeType::Dimension)
        return result;

    const auto addSegment = [&result](const QPointF &a, const QPointF &b) {
        QPainterPath line;
        line.moveTo(a);
        line.lineTo(b);
        QPainterPathStroker stroker;
        stroker.setWidth(10.0);
        result.addPath(stroker.createStroke(line));
    };

    const QPointF a = object.dimA;
    const QPointF b = object.dimB;
    const QPointF c = object.dimC;

    if (object.dimensionType == 3) {
        const qreal a1 = std::atan2(b.y() - a.y(), b.x() - a.x());
        const qreal a2 = std::atan2(c.y() - a.y(), c.x() - a.x());
        qreal sweep = a2 - a1;
        while (sweep <= -kPi)
            sweep += 2.0 * kPi;
        while (sweep > kPi)
            sweep -= 2.0 * kPi;
        const qreal radius = qMax<qreal>(18.0, qAbs(object.dimOffset));
        addSegment(a, a + QPointF(std::cos(a1), std::sin(a1)) * (radius + 8.0));
        addSegment(a, a + QPointF(std::cos(a2), std::sin(a2)) * (radius + 8.0));
        QPainterPath arc;
        const QRectF box(a.x() - radius, a.y() - radius,
                         radius * 2.0, radius * 2.0);
        arc.arcMoveTo(box, -a1 * 180.0 / kPi);
        arc.arcTo(box, -a1 * 180.0 / kPi, -sweep * 180.0 / kPi);
        QPainterPathStroker stroker;
        stroker.setWidth(10.0);
        result.addPath(stroker.createStroke(arc));
        return result;
    }

    if (object.dimensionType == 4 || object.dimensionType == 5) {
        const qreal pixels = QLineF(a, b).length();
        if (pixels > 0.5) {
            const QPointF unit((b.x() - a.x()) / pixels, (b.y() - a.y()) / pixels);
            const QPointF start = object.dimensionType == 4 ? a : a - unit * pixels;
            addSegment(start, b);
        }
        return result;
    }

    QPointF dim1;
    QPointF dim2;
    if (object.dimensionType == 0) {
        const qreal y = qMax(a.y(), b.y()) + object.dimOffset;
        dim1 = QPointF(a.x(), y);
        dim2 = QPointF(b.x(), y);
    } else if (object.dimensionType == 1) {
        const qreal x = qMax(a.x(), b.x()) + object.dimOffset;
        dim1 = QPointF(x, a.y());
        dim2 = QPointF(x, b.y());
    } else {
        QPointF direction = b - a;
        const qreal length = std::hypot(direction.x(), direction.y());
        if (length < 0.5)
            return result;
        direction /= length;
        const QPointF normal(-direction.y(), direction.x());
        dim1 = a + normal * object.dimOffset;
        dim2 = b + normal * object.dimOffset;
    }
    addSegment(a, dim1);
    addSegment(b, dim2);
    addSegment(dim1, dim2);
    return result;
}

struct CodeLayout {
    QRectF moduleArea;
    qreal unit = 1.0;
    qreal barHeight = 0.0;
    QRectF textArea;
    bool linear = false;
};

// Square in the centre of a QR code reserved for an embedded logo. The dialog
// keeps the fraction below 30% and raises the error correction to H.
QRectF codeLogoRect(const VectorObject &object, const CodeLayout &layout)
{
    if (layout.linear || object.codeLogo.isNull())
        return QRectF();
    const qreal side = qMin(layout.moduleArea.width(), layout.moduleArea.height());
    const qreal logoSize = side * qBound<qreal>(0.08, object.codeLogoScale, 0.30);
    if (logoSize < 4.0)
        return QRectF();
    return QRectF(layout.moduleArea.center().x() - logoSize / 2.0,
                  layout.moduleArea.center().y() - logoSize / 2.0,
                  logoSize, logoSize);
}

CodeLayout codeLayout(const VectorObject &object)
{
    CodeLayout layout;
    const qreal width = qMax<qreal>(1.0, object.textBox.width());
    const qreal height = qMax<qreal>(1.0, object.textBox.height());
    const int margin = qMax(0, object.codeMargin);
    layout.linear = object.codeModulesY <= 1;
    if (layout.linear) {
        const qreal textHeight = object.codeShowText
                                     ? qBound<qreal>(8.0, height * 0.14, 18.0)
                                     : 0.0;
        layout.textArea = QRectF(0.0, height - textHeight, width, textHeight);
        layout.barHeight = qMax<qreal>(4.0, height - textHeight - 4.0);
        const qreal totalModules = object.codeModulesX + margin * 2;
        layout.unit = totalModules > 0 ? width / totalModules : 1.0;
        const qreal codeWidth = object.codeModulesX * layout.unit;
        layout.moduleArea = QRectF((width - codeWidth) / 2.0, 2.0,
                                   codeWidth, layout.barHeight);
    } else {
        const qreal totalX = object.codeModulesX + margin * 2;
        const qreal totalY = object.codeModulesY + margin * 2;
        layout.unit = qMin(width / qMax<qreal>(1.0, totalX),
                           height / qMax<qreal>(1.0, totalY));
        const qreal codeWidth = object.codeModulesX * layout.unit;
        const qreal codeHeight = object.codeModulesY * layout.unit;
        layout.moduleArea = QRectF((width - codeWidth) / 2.0,
                                   (height - codeHeight) / 2.0,
                                   codeWidth, codeHeight);
    }
    return layout;
}

void paintCodeObject(QPainter *painter, const VectorObject &object)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    if (object.codeBackground.alpha() > 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(object.codeBackground);
        painter->drawRect(QRectF(0.0, 0.0, object.textBox.width(),
                                 object.textBox.height()));
    }

    const CodeLayout layout = codeLayout(object);
    painter->setPen(Qt::NoPen);
    painter->setBrush(object.codeColor);
    if (layout.linear) {
        for (int i = 0; i < object.codeCells.size(); ++i) {
            if (!object.codeCells.at(i))
                continue;
            painter->drawRect(QRectF(layout.moduleArea.left() + i * layout.unit,
                                     2.0, layout.unit + 0.15,
                                     layout.barHeight));
        }
    } else {
        for (int y = 0; y < object.codeModulesY; ++y) {
            for (int x = 0; x < object.codeModulesX; ++x) {
                const int index = y * object.codeModulesX + x;
                if (index >= object.codeCells.size() || !object.codeCells.at(index))
                    continue;
                painter->drawRect(QRectF(layout.moduleArea.left() + x * layout.unit,
                                         layout.moduleArea.top() + y * layout.unit,
                                         layout.unit + 0.15,
                                         layout.unit + 0.15));
            }
        }

        const QRectF logoRect = codeLogoRect(object, layout);
        if (!logoRect.isNull()) {
            const qreal pad = qMax(layout.unit * 0.75, logoRect.width() * 0.08);
            const QRectF plate = logoRect.adjusted(-pad, -pad, pad, pad);
            const qreal radius = qMin(plate.width(), plate.height()) * 0.12;
            painter->setPen(Qt::NoPen);
            painter->setBrush(object.codeBackground.alpha() > 0
                                  ? object.codeBackground
                                  : QColor(Qt::white));
            painter->drawRoundedRect(plate, radius, radius);

            const QSizeF imageSize(object.codeLogo.width(),
                                   object.codeLogo.height());
            if (imageSize.width() > 0.0 && imageSize.height() > 0.0) {
                const qreal scale =
                    qMin(logoRect.width() / imageSize.width(),
                         logoRect.height() / imageSize.height());
                const QSizeF targetSize(imageSize.width() * scale,
                                        imageSize.height() * scale);
                const QRectF target(logoRect.center().x() - targetSize.width() / 2.0,
                                    logoRect.center().y() - targetSize.height() / 2.0,
                                    targetSize.width(), targetSize.height());
                QPainterPath clip;
                clip.addRoundedRect(logoRect, radius * 0.6, radius * 0.6);
                painter->save();
                painter->setClipPath(clip);
                painter->drawImage(target, object.codeLogo);
                painter->restore();
            }
        }
    }

    if (layout.linear && object.codeShowText && !object.codeHumanText.isEmpty()) {
        QFont font = object.font;
        const int pixelSize = qBound(7, qRound(layout.textArea.height() * 0.82), 18);
        font.setPixelSize(pixelSize);
        painter->setFont(font);
        painter->setPen(object.codeColor);
        painter->drawText(layout.textArea, Qt::AlignHCenter | Qt::AlignVCenter,
                          object.codeHumanText);
    }
    painter->restore();
}

void paintObjectBase(QPainter *painter, const VectorObject &object,
                     bool applyBlendMode)
{
    painter->save();
    painter->setOpacity(object.opacity);

    if (object.type == ShapeType::Dimension) {
        paintDimension(painter, object);
        painter->restore();
        return;
    }
    if (object.type == ShapeType::Code) {
        paintCodeObject(painter, object);
        painter->restore();
        return;
    }

    static const QPainter::CompositionMode blendModes[] = {
        QPainter::CompositionMode_SourceOver,
        QPainter::CompositionMode_Multiply,
        QPainter::CompositionMode_Screen,
        QPainter::CompositionMode_Overlay,
        QPainter::CompositionMode_Darken,
        QPainter::CompositionMode_Lighten,
        QPainter::CompositionMode_ColorDodge,
        QPainter::CompositionMode_ColorBurn,
        QPainter::CompositionMode_HardLight,
        QPainter::CompositionMode_SoftLight,
        QPainter::CompositionMode_Difference,
        QPainter::CompositionMode_Exclusion,
    };
    if (applyBlendMode && object.blendMode > 0)
        painter->setCompositionMode(blendModes[qBound(0, object.blendMode, 11)]);

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
        const bool patternFill = object.hasFill && !object.patternShapes.isEmpty()
                                 && object.fillOpacity > 0.001;
        if (patternFill) {
            painter->save();
            painter->setOpacity(object.opacity * qBound(0.0, object.fillOpacity, 1.0));
            painter->save();
            painter->setClipPath(object.path);
            paintPatternFill(painter, object.pattern, object.patternShapes,
                             object.path.boundingRect());
            painter->restore();
            painter->restore();
        } else if (object.hasFill && object.fill.alpha() > 0
                   && object.fillOpacity > 0.001) {
            painter->save();
            painter->setOpacity(object.opacity * qBound(0.0, object.fillOpacity, 1.0));
            painter->setPen(Qt::NoPen);
            painter->setBrush(fillBrush(object, object.path.boundingRect()));
            painter->drawPath(object.path);
            painter->restore();
        }
        if (object.hasStroke && object.strokeWidth > 0.0 && object.stroke.alpha() > 0
            && object.strokeOpacity > 0.001) {
            painter->save();
            painter->setOpacity(object.opacity
                                * qBound(0.0, object.strokeOpacity, 1.0));
            QPen pen(object.stroke, object.strokeWidth);
            pen.setCapStyle(object.capStyle == 0 ? Qt::FlatCap
                                                 : (object.capStyle == 1
                                                        ? Qt::SquareCap
                                                        : Qt::RoundCap));
            pen.setJoinStyle(object.joinStyle == 0 ? Qt::MiterJoin
                                                   : (object.joinStyle == 1
                                                          ? Qt::BevelJoin
                                                          : Qt::RoundJoin));
            pen.setMiterLimit(object.miterLimit);
            if (!object.dashPattern.isEmpty())
                pen.setDashPattern(object.dashPattern);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(object.path);
            painter->restore();
        }
    }

    if (object.type == ShapeType::Text && !object.text.isEmpty()) {
        painter->save();
        painter->setOpacity(object.opacity * qBound(0.0, object.fillOpacity, 1.0));
        QBrush brush = fillBrush(object, object.textBox);
        if (object.textOnPath && !object.textPathCurve.isEmpty()) {
            const QPainterPath glyphs = buildTextOnPathGeometry(object);
            painter->setPen(Qt::NoPen);
            painter->setBrush(brush);
            painter->drawPath(glyphs);
            if (object.hasStroke && object.strokeWidth > 0.0) {
                QPen pen(object.stroke, object.strokeWidth);
                pen.setJoinStyle(Qt::RoundJoin);
                pen.setCapStyle(Qt::RoundCap);
                painter->setPen(pen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(glyphs);
            }
        } else {
            if (object.textInShape && !object.textShape.isEmpty())
                painter->setClipPath(object.textShape);
            drawTextLines(painter, object, brush, QPointF());
        }
        painter->restore();
    }

    if (object.type == ShapeType::Image && !object.image.isNull())
        painter->drawImage(object.textBox, object.image);

    painter->restore();
}

} // namespace

QPainterPath dimensionOutline(const VectorObject &object)
{
    return dimensionOutlineImpl(object);
}

void Document::paintObject(QPainter *painter, const VectorObject &object)
{
    bool hasEffects = false;
    for (const Effect &effect : object.effects) {
        if (effect.enabled) {
            hasEffects = true;
            break;
        }
    }
    if (!hasEffects) {
        paintObjectBase(painter, object, true);
        return;
    }

    const QRectF bounds = object.localBounds();
    QRectF expanded = effectBounds(object.effects, bounds).adjusted(-2.0, -2.0, 2.0, 2.0);
    if (expanded.width() < 1.0 || expanded.height() < 1.0) {
        paintObjectBase(painter, object, true);
        return;
    }

    qreal scale = 1.0;
    if (painter->device()) {
        const QTransform device = painter->deviceTransform();
        const qreal determinant = qAbs(device.determinant());
        scale = determinant > 0.0001 ? std::sqrt(determinant) : 1.0;
    }
    scale = qBound(0.5, scale, 3.0);
    const qreal maxDimension = qMax(expanded.width(), expanded.height());
    if (maxDimension * scale > 4096.0)
        scale = 4096.0 / maxDimension;

    const QSize pixelSize(qMax(1, qRound(expanded.width() * scale)),
                          qMax(1, qRound(expanded.height() * scale)));
    QImage base(pixelSize, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    {
        QPainter layerPainter(&base);
        layerPainter.setRenderHint(QPainter::Antialiasing, true);
        layerPainter.setRenderHint(QPainter::TextAntialiasing, true);
        layerPainter.scale(scale, scale);
        layerPainter.translate(-expanded.left(), -expanded.top());
        paintObjectBase(&layerPainter, object, false);
    }

    QPainterPath geometry = object.path;
    if (geometry.isEmpty()) {
        if (object.type == ShapeType::Text)
            geometry = buildTextPath(object);
        else if (object.type == ShapeType::Image)
            geometry.addRect(object.textBox);
    }

    EffectLayer layer;
    layer.image = base;
    layer.rect = expanded;
    layer = applyEffectStack(layer, object.effects, bounds, scale, geometry);

    static const QPainter::CompositionMode blendModes[] = {
        QPainter::CompositionMode_SourceOver,
        QPainter::CompositionMode_Multiply,
        QPainter::CompositionMode_Screen,
        QPainter::CompositionMode_Overlay,
        QPainter::CompositionMode_Darken,
        QPainter::CompositionMode_Lighten,
        QPainter::CompositionMode_ColorDodge,
        QPainter::CompositionMode_ColorBurn,
        QPainter::CompositionMode_HardLight,
        QPainter::CompositionMode_SoftLight,
        QPainter::CompositionMode_Difference,
        QPainter::CompositionMode_Exclusion,
    };
    painter->save();
    if (object.blendMode > 0)
        painter->setCompositionMode(blendModes[qBound(0, object.blendMode, 11)]);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(layer.rect, layer.image);
    painter->restore();
}

QRectF effectBounds(const VectorObject &object)
{
    return ::effectBounds(object.effects, object.localBounds());
}


void Document::render(QPainter *painter) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    QColor pageBackground(Qt::white);
    if (currentPage >= 0 && currentPage < pages.size())
        pageBackground = pages.at(currentPage).background;
    painter->fillRect(QRectF(0.0, 0.0, pageSize.width(), pageSize.height()),
                      pageBackground);

    const int masterIndex = (currentPage >= 0 && currentPage < pages.size())
                                ? pages.at(currentPage).master
                                : -1;
    for (const VectorObject &object : objects) {
        if (object.layer >= 0 && object.layer < layerVisible.size()
            && !layerVisible.at(object.layer))
            continue;
        if (!pages.isEmpty()) {
            const bool onPage = object.page == currentPage;
            const bool masterObject = object.page < 0 && masterIndex >= 0
                                      && object.page == -1 - masterIndex;
            if (!onPage && !masterObject)
                continue;
        }
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

QString colorToHex(const QColor &color, bool alpha)
{
    return color.name(alpha ? QColor::HexArgb : QColor::HexRgb).toUpper();
}

QColor colorFromHex(const QString &text, const QColor &fallback)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return fallback;
    if (!value.startsWith(QLatin1Char('#')))
        value.prepend(QLatin1Char('#'));
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

void colorToHsl(const QColor &color, qreal *h, qreal *s, qreal *l)
{
    int hh = 0;
    int ss = 0;
    int ll = 0;
    color.getHsl(&hh, &ss, &ll);
    if (h)
        *h = hh;
    if (s)
        *s = ss / 255.0;
    if (l)
        *l = ll / 255.0;
}

QColor colorFromHsl(qreal h, qreal s, qreal l, int alpha)
{
    return QColor::fromHsl(qRound(h), qRound(qBound(0.0, s, 1.0) * 255.0),
                           qRound(qBound(0.0, l, 1.0) * 255.0), alpha);
}

void colorToCmyk(const QColor &color, qreal *c, qreal *m, qreal *y, qreal *k)
{
    const qreal r = color.redF();
    const qreal g = color.greenF();
    const qreal b = color.blueF();
    const qreal black = 1.0 - qMax(r, qMax(g, b));
    const qreal scale = black >= 0.999 ? 0.0 : 1.0 / (1.0 - black);
    if (c)
        *c = (1.0 - r - black) * scale;
    if (m)
        *m = (1.0 - g - black) * scale;
    if (y)
        *y = (1.0 - b - black) * scale;
    if (k)
        *k = black;
}

QColor colorFromCmyk(qreal c, qreal m, qreal y, qreal k, int alpha)
{
    c = qBound(0.0, c, 1.0);
    m = qBound(0.0, m, 1.0);
    y = qBound(0.0, y, 1.0);
    k = qBound(0.0, k, 1.0);
    return QColor::fromRgbF((1.0 - c) * (1.0 - k), (1.0 - m) * (1.0 - k),
                            (1.0 - y) * (1.0 - k), alpha / 255.0);
}

namespace {

qreal srgbToLinear(qreal value)
{
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

qreal linearToSrgb(qreal value)
{
    return value <= 0.0031308 ? value * 12.92
                              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

} // namespace

void colorToLab(const QColor &color, qreal *l, qreal *a, qreal *b)
{
    const qreal r = srgbToLinear(color.redF());
    const qreal g = srgbToLinear(color.greenF());
    const qreal bl = srgbToLinear(color.blueF());

    // sRGB D65 -> XYZ
    const qreal x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * bl) / 0.95047;
    const qreal y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * bl;
    const qreal z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * bl) / 1.08883;

    const auto f = [](qreal t) {
        return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0;
    };
    if (l)
        *l = 116.0 * f(y) - 16.0;
    if (a)
        *a = 500.0 * (f(x) - f(y));
    if (b)
        *b = 200.0 * (f(y) - f(z));
}

QColor colorFromLab(qreal l, qreal a, qreal b, int alpha)
{
    const qreal fy = (l + 16.0) / 116.0;
    const qreal fx = fy + a / 500.0;
    const qreal fz = fy - b / 200.0;

    const auto finv = [](qreal t) {
        return std::pow(t, 3.0) > 0.008856 ? std::pow(t, 3.0)
                                           : (t - 16.0 / 116.0) / 7.787;
    };
    const qreal x = 0.95047 * finv(fx);
    const qreal y = finv(fy);
    const qreal z = 1.08883 * finv(fz);

    const qreal r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
    const qreal g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
    const qreal bl = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;

    return QColor::fromRgbF(qBound(0.0, linearToSrgb(r), 1.0),
                            qBound(0.0, linearToSrgb(g), 1.0),
                            qBound(0.0, linearToSrgb(bl), 1.0),
                            alpha / 255.0);
}

namespace {

QJsonObject objectToJson(const VectorObject &object)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("type"), shapeTypeId(object.type));
    entry.insert(QStringLiteral("page"), object.page == -9999 ? 0 : object.page);
    entry.insert(QStringLiteral("layer"), object.layer);
    entry.insert(QStringLiteral("group"), object.group);
    entry.insert(QStringLiteral("id"), object.id);
    if (!object.name.isEmpty())
        entry.insert(QStringLiteral("name"), object.name);
    entry.insert(QStringLiteral("masterId"), object.masterId);
    entry.insert(QStringLiteral("blendId"), object.blendId);
    entry.insert(QStringLiteral("symbolId"), object.symbolId);
    entry.insert(QStringLiteral("instanceId"), object.instanceId);
    entry.insert(QStringLiteral("patternId"), object.patternId);
    if (!object.patternShapes.isEmpty()) {
        entry.insert(QStringLiteral("pattern"),
                     patternSettingsToJson(object.pattern));
        QJsonArray patternShapeArray;
        for (const PatternShape &shape : object.patternShapes) {
            QJsonObject shapeEntry;
            shapeEntry.insert(QStringLiteral("path"), encodePath(shape.path));
            shapeEntry.insert(QStringLiteral("fill"),
                              shape.fill.name(QColor::HexArgb));
            shapeEntry.insert(QStringLiteral("stroke"),
                              shape.stroke.name(QColor::HexArgb));
            shapeEntry.insert(QStringLiteral("strokeWidth"), shape.strokeWidth);
            shapeEntry.insert(QStringLiteral("hasFill"), shape.hasFill);
            shapeEntry.insert(QStringLiteral("hasStroke"), shape.hasStroke);
            shapeEntry.insert(QStringLiteral("opacity"), shape.opacity);
            patternShapeArray.append(shapeEntry);
        }
        entry.insert(QStringLiteral("patternShapes"), patternShapeArray);
    }
    if (object.type == ShapeType::Dimension) {
        entry.insert(QStringLiteral("dimensionType"), object.dimensionType);
        entry.insert(QStringLiteral("dimA"), pointArray(object.dimA));
        entry.insert(QStringLiteral("dimB"), pointArray(object.dimB));
        entry.insert(QStringLiteral("dimC"), pointArray(object.dimC));
        entry.insert(QStringLiteral("dimOffset"), object.dimOffset);
        entry.insert(QStringLiteral("dimUnits"), object.dimUnits);
        entry.insert(QStringLiteral("dimPrecision"), object.dimPrecision);
        entry.insert(QStringLiteral("dimScale"), object.dimScale);
        entry.insert(QStringLiteral("dimAssociative"), object.dimAssociative);
        entry.insert(QStringLiteral("dimRefA"), object.dimRefA);
        entry.insert(QStringLiteral("dimRefB"), object.dimRefB);
        entry.insert(QStringLiteral("dimRefC"), object.dimRefC);
        entry.insert(QStringLiteral("dimAnchorA"), pointArray(object.dimAnchorA));
        entry.insert(QStringLiteral("dimAnchorB"), pointArray(object.dimAnchorB));
        entry.insert(QStringLiteral("dimAnchorC"), pointArray(object.dimAnchorC));
    }
    if (object.type == ShapeType::Code) {
        entry.insert(QStringLiteral("codeKind"), object.codeKind);
        entry.insert(QStringLiteral("codeData"), object.codeData);
        entry.insert(QStringLiteral("codeEcc"), object.codeEcc);
        entry.insert(QStringLiteral("codeMargin"), object.codeMargin);
        entry.insert(QStringLiteral("codeColor"),
                     object.codeColor.name(QColor::HexArgb));
        entry.insert(QStringLiteral("codeBackground"),
                     object.codeBackground.name(QColor::HexArgb));
        entry.insert(QStringLiteral("codeShowText"), object.codeShowText);
        entry.insert(QStringLiteral("codeModulesX"), object.codeModulesX);
        entry.insert(QStringLiteral("codeModulesY"), object.codeModulesY);
        entry.insert(QStringLiteral("codeHumanText"), object.codeHumanText);
        entry.insert(QStringLiteral("codeTextBox"),
                     QJsonArray{object.textBox.x(), object.textBox.y(),
                                object.textBox.width(), object.textBox.height()});
        QJsonArray rows;
        for (int y = 0; y < object.codeModulesY; ++y) {
            QString row;
            row.reserve(object.codeModulesX);
            for (int x = 0; x < object.codeModulesX; ++x) {
                const int index = y * object.codeModulesX + x;
                row.append(index < object.codeCells.size()
                               && object.codeCells.at(index) ? QLatin1Char('1')
                                                             : QLatin1Char('0'));
            }
            rows.append(row);
        }
        entry.insert(QStringLiteral("codeCells"), rows);
        entry.insert(QStringLiteral("codeLogoScale"), object.codeLogoScale);
        const QString logo = imageToJson(object.codeLogo);
        if (!logo.isEmpty())
            entry.insert(QStringLiteral("codeLogo"), logo);
    }
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
    entry.insert(QStringLiteral("gradientCenter"),
                 pointArray(object.gradientCenter));
    entry.insert(QStringLiteral("gradientRadius"), object.gradientRadius);
    entry.insert(QStringLiteral("meshColor3"),
                 object.meshColor3.name(QColor::HexArgb));
    entry.insert(QStringLiteral("meshColor4"),
                 object.meshColor4.name(QColor::HexArgb));
    QJsonArray stopArray;
    for (const GradientStop &stop : object.gradientStops) {
        QJsonObject stopEntry;
        stopEntry.insert(QStringLiteral("position"), stop.position);
        stopEntry.insert(QStringLiteral("color"),
                         stop.color.name(QColor::HexArgb));
        stopEntry.insert(QStringLiteral("opacity"), stop.opacity);
        stopArray.append(stopEntry);
    }
    entry.insert(QStringLiteral("gradientStops"), stopArray);
    entry.insert(QStringLiteral("pivotX"), object.pivot.x());
    entry.insert(QStringLiteral("pivotY"), object.pivot.y());
    entry.insert(QStringLiteral("stroke"), colorToJson(object.stroke, object.hasStroke));
    entry.insert(QStringLiteral("strokeWidth"), object.strokeWidth);
    entry.insert(QStringLiteral("capStyle"), object.capStyle);
    entry.insert(QStringLiteral("joinStyle"), object.joinStyle);
    entry.insert(QStringLiteral("miterLimit"), object.miterLimit);
    QJsonArray dashArray;
    for (qreal dash : object.dashPattern)
        dashArray.append(dash);
    entry.insert(QStringLiteral("dashPattern"), dashArray);
    entry.insert(QStringLiteral("opacity"), object.opacity);
    entry.insert(QStringLiteral("fillOpacity"), object.fillOpacity);
    entry.insert(QStringLiteral("strokeOpacity"), object.strokeOpacity);
    entry.insert(QStringLiteral("blendMode"), object.blendMode);
    entry.insert(QStringLiteral("shadow"), object.shadow);
    entry.insert(QStringLiteral("locked"), object.locked);
    entry.insert(QStringLiteral("shadowOffset"),
                 pointArray(object.shadowOffset));
    entry.insert(QStringLiteral("shadowOpacity"), object.shadowOpacity);
    entry.insert(QStringLiteral("shadowColor"),
                 object.shadowColor.name(QColor::HexArgb));

    QJsonArray effectArray;
    for (const Effect &effect : object.effects) {
        QJsonObject effectEntry;
        effectEntry.insert(QStringLiteral("type"), effect.type);
        effectEntry.insert(QStringLiteral("enabled"), effect.enabled);
        effectEntry.insert(QStringLiteral("opacity"), effect.opacity);
        effectEntry.insert(QStringLiteral("color"),
                           effect.color.name(QColor::HexArgb));
        effectEntry.insert(QStringLiteral("offset"), pointArray(effect.offset));
        effectEntry.insert(QStringLiteral("radius"), effect.radius);
        effectEntry.insert(QStringLiteral("intensity"), effect.intensity);
        effectEntry.insert(QStringLiteral("angle"), effect.angle);
        effectEntry.insert(QStringLiteral("distance"), effect.distance);
        effectEntry.insert(QStringLiteral("amount"), effect.amount);
        effectEntry.insert(QStringLiteral("center"), pointArray(effect.center));
        QJsonArray quadArray;
        for (const QPointF &point : effect.quad) {
            quadArray.append(point.x());
            quadArray.append(point.y());
        }
        effectEntry.insert(QStringLiteral("quad"), quadArray);
        if (!effect.mask.isEmpty())
            effectEntry.insert(QStringLiteral("mask"), encodePath(effect.mask));
        effectArray.append(effectEntry);
    }
    entry.insert(QStringLiteral("effects"), effectArray);

    if (object.type == ShapeType::Text) {
        entry.insert(QStringLiteral("text"), object.text);
        entry.insert(QStringLiteral("font"), fontToJson(object.font));
        entry.insert(QStringLiteral("textBox"),
                     QJsonArray{object.textBox.x(), object.textBox.y(),
                                object.textBox.width(), object.textBox.height()});
        entry.insert(QStringLiteral("textAlign"), object.textAlign);
        entry.insert(QStringLiteral("letterSpacing"), object.letterSpacing);
        entry.insert(QStringLiteral("lineSpacing"), object.lineSpacing);
        entry.insert(QStringLiteral("underline"), object.underline);
        entry.insert(QStringLiteral("strikeOut"), object.strikeOut);
        entry.insert(QStringLiteral("script"), object.script);
        entry.insert(QStringLiteral("wordSpacing"), object.wordSpacing);
        entry.insert(QStringLiteral("horizontalScale"), object.horizontalScale);
        entry.insert(QStringLiteral("verticalScale"), object.verticalScale);
        entry.insert(QStringLiteral("baselineShift"), object.baselineShift);
        entry.insert(QStringLiteral("paragraphSpacing"), object.paragraphSpacing);
        entry.insert(QStringLiteral("firstLineIndent"), object.firstLineIndent);
        entry.insert(QStringLiteral("textOnPath"), object.textOnPath);
        entry.insert(QStringLiteral("textPathOffset"), object.textPathOffset);
        entry.insert(QStringLiteral("textPathReverse"), object.textPathReverse);
        entry.insert(QStringLiteral("textInShape"), object.textInShape);
        entry.insert(QStringLiteral("fontFeatures"),
                     QJsonArray::fromStringList(object.fontFeatures));
        QJsonObject axesEntry;
        for (auto it = object.variableAxes.constBegin();
             it != object.variableAxes.constEnd(); ++it)
            axesEntry.insert(it.key(), it.value());
        if (!axesEntry.isEmpty())
            entry.insert(QStringLiteral("variableAxes"), axesEntry);
        if (!object.textPathCurve.isEmpty())
            entry.insert(QStringLiteral("textPathCurve"),
                         encodePath(object.textPathCurve));
        if (!object.textShape.isEmpty())
            entry.insert(QStringLiteral("textShape"),
                         encodePath(object.textShape));
    }
    if (object.type == ShapeType::Image) {
        entry.insert(QStringLiteral("textBox"),
                     QJsonArray{object.textBox.x(), object.textBox.y(),
                                object.textBox.width(), object.textBox.height()});
        const QString encoded = imageToJson(object.image);
        if (!encoded.isEmpty())
            entry.insert(QStringLiteral("image"), encoded);
    }
    return entry;
}

VectorObject objectFromJson(const QJsonObject &entry)
{
    VectorObject object;
    object.type = shapeTypeFromId(entry.value(QStringLiteral("type")).toString());
    object.page = entry.value(QStringLiteral("page")).toInt(0);
    object.layer = entry.value(QStringLiteral("layer")).toInt(0);
    object.group = entry.value(QStringLiteral("group")).toInt(0);
    object.id = entry.value(QStringLiteral("id")).toInt(0);
    object.name = entry.value(QStringLiteral("name")).toString();
    object.masterId = entry.value(QStringLiteral("masterId")).toInt(0);
    object.blendId = entry.value(QStringLiteral("blendId")).toInt(0);
    object.symbolId = entry.value(QStringLiteral("symbolId")).toInt(0);
    object.instanceId = entry.value(QStringLiteral("instanceId")).toInt(0);
    object.patternId = entry.value(QStringLiteral("patternId")).toInt(0);
    const QJsonObject patternEntry =
        entry.value(QStringLiteral("pattern")).toObject();
    if (!patternEntry.isEmpty())
        object.pattern = patternSettingsFromJson(patternEntry);
    for (const QJsonValue &shapeValue :
         entry.value(QStringLiteral("patternShapes")).toArray()) {
        const QJsonObject shapeEntry = shapeValue.toObject();
        PatternShape shape;
        shape.path = decodePath(shapeEntry.value(QStringLiteral("path")).toArray());
        shape.fill = QColor(shapeEntry.value(QStringLiteral("fill"))
                                .toString(QStringLiteral("#ff4f7cc0")));
        shape.stroke = QColor(shapeEntry.value(QStringLiteral("stroke"))
                                  .toString(QStringLiteral("#ff1d2127")));
        shape.strokeWidth =
            shapeEntry.value(QStringLiteral("strokeWidth")).toDouble(0.0);
        shape.hasFill = shapeEntry.value(QStringLiteral("hasFill")).toBool(true);
        shape.hasStroke =
            shapeEntry.value(QStringLiteral("hasStroke")).toBool(false);
        shape.opacity = shapeEntry.value(QStringLiteral("opacity")).toDouble(1.0);
        object.patternShapes.append(shape);
    }
    if (object.type == ShapeType::Dimension) {
        object.dimensionType =
            entry.value(QStringLiteral("dimensionType")).toInt(0);
        object.dimA = pointFromArray(entry.value(QStringLiteral("dimA")).toArray());
        object.dimB = pointFromArray(entry.value(QStringLiteral("dimB")).toArray());
        object.dimC = pointFromArray(entry.value(QStringLiteral("dimC")).toArray());
        object.dimOffset =
            entry.value(QStringLiteral("dimOffset")).toDouble(24.0);
        object.dimUnits = entry.value(QStringLiteral("dimUnits")).toInt(0);
        object.dimPrecision =
            entry.value(QStringLiteral("dimPrecision")).toInt(1);
        object.dimScale = entry.value(QStringLiteral("dimScale")).toDouble(1.0);
        object.dimAssociative =
            entry.value(QStringLiteral("dimAssociative")).toBool(true);
        object.dimRefA = entry.value(QStringLiteral("dimRefA")).toInt(0);
        object.dimRefB = entry.value(QStringLiteral("dimRefB")).toInt(0);
        object.dimRefC = entry.value(QStringLiteral("dimRefC")).toInt(0);
        object.dimAnchorA =
            pointFromArray(entry.value(QStringLiteral("dimAnchorA")).toArray());
        object.dimAnchorB =
            pointFromArray(entry.value(QStringLiteral("dimAnchorB")).toArray());
        object.dimAnchorC =
            pointFromArray(entry.value(QStringLiteral("dimAnchorC")).toArray());
    }
    if (object.type == ShapeType::Code) {
        object.codeKind = entry.value(QStringLiteral("codeKind")).toInt(0);
        object.codeData = entry.value(QStringLiteral("codeData")).toString();
        object.codeEcc = entry.value(QStringLiteral("codeEcc")).toInt(1);
        object.codeMargin = entry.value(QStringLiteral("codeMargin")).toInt(4);
        object.codeColor = QColor(entry.value(QStringLiteral("codeColor"))
                                      .toString(QStringLiteral("#ff000000")));
        object.codeBackground =
            QColor(entry.value(QStringLiteral("codeBackground"))
                       .toString(QStringLiteral("#ffffffff")));
        object.codeShowText =
            entry.value(QStringLiteral("codeShowText")).toBool(true);
        object.codeModulesX =
            entry.value(QStringLiteral("codeModulesX")).toInt(0);
        object.codeModulesY =
            entry.value(QStringLiteral("codeModulesY")).toInt(0);
        object.codeHumanText =
            entry.value(QStringLiteral("codeHumanText")).toString();
        const QJsonArray box =
            entry.value(QStringLiteral("codeTextBox")).toArray();
        if (box.size() >= 4) {
            object.textBox = QRectF(box.at(0).toDouble(), box.at(1).toDouble(),
                                    box.at(2).toDouble(), box.at(3).toDouble());
        }
        for (const QJsonValue &rowValue :
             entry.value(QStringLiteral("codeCells")).toArray()) {
            const QString row = rowValue.toString();
            for (const QChar c : row)
                object.codeCells.append(c == QLatin1Char('1'));
        }
        object.codeLogoScale =
            entry.value(QStringLiteral("codeLogoScale")).toDouble(0.22);
        object.codeLogo = imageFromJson(entry.value(QStringLiteral("codeLogo")));
    }
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
    const QJsonArray centerArray =
        entry.value(QStringLiteral("gradientCenter")).toArray();
    if (centerArray.size() == 2)
        object.gradientCenter = QPointF(centerArray.at(0).toDouble(0.5),
                                        centerArray.at(1).toDouble(0.5));
    object.gradientRadius =
        entry.value(QStringLiteral("gradientRadius")).toDouble(0.5);
    const QColor mesh3(entry.value(QStringLiteral("meshColor3")).toString());
    if (mesh3.isValid())
        object.meshColor3 = mesh3;
    const QColor mesh4(entry.value(QStringLiteral("meshColor4")).toString());
    if (mesh4.isValid())
        object.meshColor4 = mesh4;
    for (const QJsonValue &value :
         entry.value(QStringLiteral("gradientStops")).toArray()) {
        const QJsonObject stopEntry = value.toObject();
        GradientStop stop;
        stop.position = stopEntry.value(QStringLiteral("position")).toDouble();
        stop.color = QColor(stopEntry.value(QStringLiteral("color")).toString());
        stop.opacity = stopEntry.value(QStringLiteral("opacity")).toDouble(1.0);
        object.gradientStops.append(stop);
    }
    object.pivot = QPointF(entry.value(QStringLiteral("pivotX")).toDouble(0.5),
                           entry.value(QStringLiteral("pivotY")).toDouble(0.5));
    object.stroke = colorFromJson(entry.value(QStringLiteral("stroke")), &object.hasStroke,
                                  QColor("#1d2127"));
    object.strokeWidth = entry.value(QStringLiteral("strokeWidth")).toDouble(2.0);
    object.capStyle = entry.value(QStringLiteral("capStyle")).toInt(2);
    object.joinStyle = entry.value(QStringLiteral("joinStyle")).toInt(2);
    object.miterLimit = entry.value(QStringLiteral("miterLimit")).toDouble(2.0);
    for (const QJsonValue &value : entry.value(QStringLiteral("dashPattern")).toArray())
        object.dashPattern.append(value.toDouble());
    object.opacity = entry.value(QStringLiteral("opacity")).toDouble(1.0);
    object.fillOpacity =
        entry.value(QStringLiteral("fillOpacity")).toDouble(1.0);
    object.strokeOpacity =
        entry.value(QStringLiteral("strokeOpacity")).toDouble(1.0);
    object.blendMode = entry.value(QStringLiteral("blendMode")).toInt(0);
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

    for (const QJsonValue &value :
         entry.value(QStringLiteral("effects")).toArray()) {
        const QJsonObject effectEntry = value.toObject();
        Effect effect;
        effect.type = effectEntry.value(QStringLiteral("type")).toString();
        if (effect.type.isEmpty())
            continue;
        effect.enabled = effectEntry.value(QStringLiteral("enabled")).toBool(true);
        effect.opacity = effectEntry.value(QStringLiteral("opacity")).toDouble(1.0);
        effect.color = QColor(effectEntry.value(QStringLiteral("color"))
                                  .toString(QStringLiteral("#ff000000")));
        effect.offset = pointFromArray(
            effectEntry.value(QStringLiteral("offset")).toArray());
        effect.radius = effectEntry.value(QStringLiteral("radius")).toDouble(8.0);
        effect.intensity =
            effectEntry.value(QStringLiteral("intensity")).toDouble(1.0);
        effect.angle = effectEntry.value(QStringLiteral("angle")).toDouble(0.0);
        effect.distance =
            effectEntry.value(QStringLiteral("distance")).toDouble(0.0);
        effect.amount = effectEntry.value(QStringLiteral("amount")).toDouble(0.0);
        effect.center = pointFromArray(
            effectEntry.value(QStringLiteral("center")).toArray());
        const QJsonArray quadArray =
            effectEntry.value(QStringLiteral("quad")).toArray();
        for (int i = 0; i + 1 < quadArray.size(); i += 2)
            effect.quad.append(QPointF(quadArray.at(i).toDouble(),
                                       quadArray.at(i + 1).toDouble()));
        effect.mask = decodePath(
            effectEntry.value(QStringLiteral("mask")).toArray());
        object.effects.append(effect);
    }

    if (object.type == ShapeType::Text) {
        object.text = entry.value(QStringLiteral("text")).toString();
        object.font = fontFromJson(entry.value(QStringLiteral("font")).toObject());
        object.letterSpacing = entry.value(QStringLiteral("letterSpacing")).toDouble(0.0);
        object.underline = entry.value(QStringLiteral("underline")).toBool(false);
        object.strikeOut = entry.value(QStringLiteral("strikeOut")).toBool(false);
        object.script = entry.value(QStringLiteral("script")).toInt(0);
        object.wordSpacing = entry.value(QStringLiteral("wordSpacing")).toDouble(0.0);
        object.horizontalScale =
            entry.value(QStringLiteral("horizontalScale")).toDouble(1.0);
        object.verticalScale =
            entry.value(QStringLiteral("verticalScale")).toDouble(1.0);
        object.baselineShift =
            entry.value(QStringLiteral("baselineShift")).toDouble(0.0);
        object.paragraphSpacing =
            entry.value(QStringLiteral("paragraphSpacing")).toDouble(0.0);
        object.firstLineIndent =
            entry.value(QStringLiteral("firstLineIndent")).toDouble(0.0);
        object.textOnPath = entry.value(QStringLiteral("textOnPath")).toBool(false);
        object.textPathOffset =
            entry.value(QStringLiteral("textPathOffset")).toDouble(0.0);
        object.textPathReverse =
            entry.value(QStringLiteral("textPathReverse")).toBool(false);
        object.textInShape = entry.value(QStringLiteral("textInShape")).toBool(false);
        for (const QJsonValue &value :
             entry.value(QStringLiteral("fontFeatures")).toArray())
            object.fontFeatures.append(value.toString());
        const QJsonObject axesEntry =
            entry.value(QStringLiteral("variableAxes")).toObject();
        for (auto it = axesEntry.constBegin(); it != axesEntry.constEnd(); ++it)
            object.variableAxes.insert(it.key(), it.value().toDouble());
        object.textPathCurve = decodePath(
            entry.value(QStringLiteral("textPathCurve")).toArray());
        object.textShape = decodePath(
            entry.value(QStringLiteral("textShape")).toArray());
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
    return object;
}

} // namespace

QJsonObject vectorObjectToJson(const VectorObject &object)
{
    return objectToJson(object);
}

VectorObject vectorObjectFromJson(const QJsonObject &entry)
{
    return objectFromJson(entry);
}

QJsonArray encodePathData(const QPainterPath &path)
{
    return encodePath(path);
}

QPainterPath decodePathData(const QJsonArray &array)
{
    return decodePath(array);
}

QJsonObject Document::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-document"));
    root.insert(QStringLiteral("version"), 1);

    root.insert(QStringLiteral("drawingScale"), drawingScale);

    QJsonObject page;
    page.insert(QStringLiteral("width"), pageSize.width());
    page.insert(QStringLiteral("height"), pageSize.height());
    root.insert(QStringLiteral("page"), page);

    QJsonArray pageArray;
    for (const PageInfo &info : pages) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), info.name);
        entry.insert(QStringLiteral("width"), info.size.width());
        entry.insert(QStringLiteral("height"), info.size.height());
        entry.insert(QStringLiteral("background"),
                     info.background.name(QColor::HexArgb));
        entry.insert(QStringLiteral("margin"), info.margin);
        entry.insert(QStringLiteral("bleed"), info.bleed);
        entry.insert(QStringLiteral("master"), info.master);
        pageArray.append(entry);
    }
    root.insert(QStringLiteral("pages"), pageArray);

    QJsonArray masterArray;
    for (const MasterPage &master : masters) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), master.name);
        entry.insert(QStringLiteral("header"), master.header);
        entry.insert(QStringLiteral("footer"), master.footer);
        entry.insert(QStringLiteral("pageNumbers"), master.pageNumbers);
        masterArray.append(entry);
    }
    root.insert(QStringLiteral("masters"), masterArray);
    root.insert(QStringLiteral("currentPage"), currentPage);

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
    for (const VectorObject &object : objects)
        objectArray.append(objectToJson(object));
    root.insert(QStringLiteral("objects"), objectArray);

    QJsonArray symbolArray;
    for (const SymbolDefinition &symbol : symbols) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), symbol.id);
        entry.insert(QStringLiteral("name"), symbol.name);
        entry.insert(QStringLiteral("bounds"),
                     QJsonArray{symbol.bounds.x(), symbol.bounds.y(),
                                symbol.bounds.width(), symbol.bounds.height()});
        QJsonArray symbolObjects;
        for (const VectorObject &object : symbol.objects)
            symbolObjects.append(objectToJson(object));
        entry.insert(QStringLiteral("objects"), symbolObjects);
        symbolArray.append(entry);
    }
    root.insert(QStringLiteral("symbols"), symbolArray);

    QJsonArray instanceArray;
    for (const SymbolInstance &instance : instances) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), instance.id);
        entry.insert(QStringLiteral("symbolId"), instance.symbolId);
        entry.insert(QStringLiteral("rotation"), instance.rotation);
        entry.insert(QStringLiteral("page"), instance.page);
        entry.insert(QStringLiteral("group"), instance.group);
        entry.insert(QStringLiteral("bounds"),
                     QJsonArray{instance.bounds.x(), instance.bounds.y(),
                                instance.bounds.width(), instance.bounds.height()});
        instanceArray.append(entry);
    }
    root.insert(QStringLiteral("instances"), instanceArray);

    QJsonArray patternArray;
    for (const PatternDefinition &pattern : patterns)
        patternArray.append(patternDefinitionToJson(pattern));
    root.insert(QStringLiteral("patterns"), patternArray);

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

    QJsonArray blendArray;
    for (const BlendRecord &blend : blends) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), blend.id);
        entry.insert(QStringLiteral("fromId"), blend.fromId);
        entry.insert(QStringLiteral("toId"), blend.toId);
        entry.insert(QStringLiteral("steps"), blend.steps);
        entry.insert(QStringLiteral("spacing"), blend.spacing);
        entry.insert(QStringLiteral("active"), blend.active);
        blendArray.append(entry);
    }
    root.insert(QStringLiteral("blends"), blendArray);
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
    document.drawingScale = json.value(QStringLiteral("drawingScale")).toDouble(1.0);

    for (const QJsonValue &value : json.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject entry = value.toObject();
        PageInfo info;
        info.name = entry.value(QStringLiteral("name"))
                        .toString(QStringLiteral("Страница"));
        info.size = QSizeF(entry.value(QStringLiteral("width")).toDouble(width),
                           entry.value(QStringLiteral("height")).toDouble(height));
        const QColor background(entry.value(QStringLiteral("background")).toString());
        if (background.isValid())
            info.background = background;
        info.margin = entry.value(QStringLiteral("margin")).toDouble(37.8);
        info.bleed = entry.value(QStringLiteral("bleed")).toDouble(11.34);
        info.master = entry.value(QStringLiteral("master")).toInt(-1);
        document.pages.append(info);
    }
    if (document.pages.isEmpty())
        document.pages.append(PageInfo{QStringLiteral("Страница 1"),
                                       document.pageSize, QColor(Qt::white),
                                       37.8, 11.34, -1});
    document.pageSize = document.pages.first().size;

    for (const QJsonValue &value : json.value(QStringLiteral("masters")).toArray()) {
        const QJsonObject entry = value.toObject();
        MasterPage master;
        master.name = entry.value(QStringLiteral("name"))
                          .toString(QStringLiteral("Мастер"));
        master.header = entry.value(QStringLiteral("header")).toString();
        master.footer = entry.value(QStringLiteral("footer")).toString();
        master.pageNumbers =
            entry.value(QStringLiteral("pageNumbers")).toBool(false);
        document.masters.append(master);
    }
    document.currentPage = json.value(QStringLiteral("currentPage")).toInt(0);
    if (document.currentPage >= document.pages.size())
        document.currentPage = document.pages.size() - 1;

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

    for (const QJsonValue &value : json.value(QStringLiteral("blends")).toArray()) {
        const QJsonObject entry = value.toObject();
        BlendRecord blend;
        blend.id = entry.value(QStringLiteral("id")).toInt();
        blend.fromId = entry.value(QStringLiteral("fromId")).toInt();
        blend.toId = entry.value(QStringLiteral("toId")).toInt();
        blend.steps = entry.value(QStringLiteral("steps")).toInt(7);
        blend.spacing = entry.value(QStringLiteral("spacing")).toDouble(0.0);
        blend.active = entry.value(QStringLiteral("active")).toBool(true);
        document.blends.append(blend);
    }

    for (const QJsonValue &value : json.value(QStringLiteral("symbols")).toArray()) {
        const QJsonObject entry = value.toObject();
        SymbolDefinition symbol;
        symbol.id = entry.value(QStringLiteral("id")).toInt();
        if (symbol.id <= 0)
            continue;
        symbol.name = entry.value(QStringLiteral("name"))
                          .toString(QStringLiteral("Символ"));
        const QJsonArray bounds = entry.value(QStringLiteral("bounds")).toArray();
        if (bounds.size() == 4) {
            symbol.bounds = QRectF(bounds.at(0).toDouble(), bounds.at(1).toDouble(),
                                   bounds.at(2).toDouble(), bounds.at(3).toDouble());
        }
        for (const QJsonValue &objectValue :
             entry.value(QStringLiteral("objects")).toArray()) {
            symbol.objects.append(objectFromJson(objectValue.toObject()));
        }
        document.symbols.append(symbol);
    }

    for (const QJsonValue &value : json.value(QStringLiteral("patterns")).toArray()) {
        PatternDefinition pattern = patternDefinitionFromJson(value.toObject());
        if (pattern.id > 0)
            document.patterns.append(pattern);
    }

    for (const QJsonValue &value : json.value(QStringLiteral("instances")).toArray()) {
        const QJsonObject entry = value.toObject();
        SymbolInstance instance;
        instance.id = entry.value(QStringLiteral("id")).toInt();
        instance.symbolId = entry.value(QStringLiteral("symbolId")).toInt();
        if (instance.id <= 0 || instance.symbolId <= 0)
            continue;
        instance.rotation = entry.value(QStringLiteral("rotation")).toDouble(0.0);
        instance.page = entry.value(QStringLiteral("page")).toInt(0);
        instance.group = entry.value(QStringLiteral("group")).toInt(0);
        const QJsonArray bounds = entry.value(QStringLiteral("bounds")).toArray();
        if (bounds.size() == 4) {
            instance.bounds = QRectF(bounds.at(0).toDouble(), bounds.at(1).toDouble(),
                                     bounds.at(2).toDouble(), bounds.at(3).toDouble());
        }
        document.instances.append(instance);
    }

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
        VectorObject object = objectFromJson(value.toObject());
        if (object.layer < 0 || object.layer >= document.layers.size())
            object.layer = 0;

        if (!object.path.isEmpty()
            || object.type == ShapeType::Text
            || object.type == ShapeType::Image
            || object.type == ShapeType::Dimension
            || object.type == ShapeType::Code) {
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
    pages = loaded.pages;
    masters = loaded.masters;
    currentPage = loaded.currentPage;
    layers = loaded.layers;
    groups = loaded.groups;
    drawingScale = loaded.drawingScale;
    guidesX = loaded.guidesX;
    guidesY = loaded.guidesY;
    blends = loaded.blends;
    symbols = loaded.symbols;
    instances = loaded.instances;
    patterns = loaded.patterns;
    layerVisible = loaded.layerVisible;
    layerLocked = loaded.layerLocked;
    objects = loaded.objects;
    return true;
}

QString Document::toSvg(bool withBackground) const
{
    QString svg;
    svg += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                          "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                          "width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n")
               .arg(num(pageSize.width()), num(pageSize.height()));
    if (withBackground)
        svg += QStringLiteral("  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n");

    const int masterIndex = (currentPage >= 0 && currentPage < pages.size())
                                ? pages.at(currentPage).master
                                : -1;
    int objectIndex = 0;
    for (const VectorObject &object : objects) {
        if (object.layer >= 0 && object.layer < layerVisible.size()
            && !layerVisible.at(object.layer))
            continue;
        if (!pages.isEmpty()) {
            const bool onPage = object.page == currentPage;
            const bool masterObject = object.page < 0 && masterIndex >= 0
                                      && object.page == -1 - masterIndex;
            if (!onPage && !masterObject)
                continue;
        }
        const QRectF bounds = object.localBounds();

        if (object.type == ShapeType::Code) {
            svg += QStringLiteral("  <g transform=\"translate(%1,%2) rotate(%3 %4 %5)\">\n")
                       .arg(num(object.pos.x()), num(object.pos.y()), num(object.rotation),
                            num(bounds.center().x()), num(bounds.center().y()));
            if (object.codeBackground.alpha() > 0) {
                svg += QStringLiteral("    <rect x=\"0\" y=\"0\" width=\"%1\" height=\"%2\" "
                                      "fill=\"%3\" fill-opacity=\"%4\"/>\n")
                           .arg(num(object.textBox.width()),
                                num(object.textBox.height()),
                                object.codeBackground.name(QColor::HexRgb),
                                num(object.codeBackground.alphaF()));
            }
            const CodeLayout layout = codeLayout(object);
            const QColor color = object.codeColor;
            if (layout.linear) {
                for (int i = 0; i < object.codeCells.size(); ++i) {
                    if (!object.codeCells.at(i))
                        continue;
                    svg += QStringLiteral("    <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                                          "fill=\"%5\" fill-opacity=\"%6\"/>\n")
                               .arg(num(layout.moduleArea.left() + i * layout.unit),
                                    num(2.0), num(layout.unit + 0.15),
                                    num(layout.barHeight),
                                    color.name(QColor::HexRgb), num(color.alphaF()));
                }
                if (object.codeShowText && !object.codeHumanText.isEmpty()) {
                    const int pixelSize =
                        qBound(7, qRound(layout.textArea.height() * 0.82), 18);
                    svg += QStringLiteral("    <text x=\"%1\" y=\"%2\" "
                                          "font-family=\"Adwaita Sans\" font-size=\"%3\" "
                                          "text-anchor=\"middle\" fill=\"%4\">%5</text>\n")
                               .arg(num(object.textBox.width() / 2.0),
                                    num(layout.textArea.center().y() + pixelSize * 0.35),
                                    QString::number(pixelSize), color.name(QColor::HexRgb),
                                    object.codeHumanText.toHtmlEscaped());
                }
            } else {
                for (int y = 0; y < object.codeModulesY; ++y) {
                    for (int x = 0; x < object.codeModulesX; ++x) {
                        const int index = y * object.codeModulesX + x;
                        if (index >= object.codeCells.size()
                            || !object.codeCells.at(index))
                            continue;
                        svg += QStringLiteral(
                                   "    <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                                   "fill=\"%5\" fill-opacity=\"%6\"/>\n")
                                   .arg(num(layout.moduleArea.left() + x * layout.unit),
                                        num(layout.moduleArea.top() + y * layout.unit),
                                        num(layout.unit + 0.15),
                                        num(layout.unit + 0.15),
                                        color.name(QColor::HexRgb),
                                        num(color.alphaF()));
                    }
                }
                const QRectF logoRect = codeLogoRect(object, layout);
                if (!logoRect.isNull()) {
                    const qreal pad = qMax(layout.unit * 0.75,
                                           logoRect.width() * 0.08);
                    const QRectF plate = logoRect.adjusted(-pad, -pad, pad, pad);
                    const QColor plateColor = object.codeBackground.alpha() > 0
                                                  ? object.codeBackground
                                                  : QColor(Qt::white);
                    svg += QStringLiteral(
                               "    <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                               "rx=\"%5\" fill=\"%6\" fill-opacity=\"%7\"/>\n")
                               .arg(num(plate.x()), num(plate.y()),
                                    num(plate.width()), num(plate.height()),
                                    num(plate.width() * 0.12),
                                    plateColor.name(QColor::HexRgb),
                                    num(plateColor.alphaF()));
                    svg += QStringLiteral(
                               "    <image x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                               "preserveAspectRatio=\"xMidYMid meet\" "
                               "xlink:href=\"data:image/png;base64,%5\"/>\n")
                               .arg(num(logoRect.x()), num(logoRect.y()),
                                    num(logoRect.width()), num(logoRect.height()),
                                    imageToJson(object.codeLogo));
                }
            }
            svg += QStringLiteral("  </g>\n");
            continue;
        }

        bool hasEffects = false;
        for (const Effect &effect : object.effects) {
            if (effect.enabled) {
                hasEffects = true;
                break;
            }
        }
        const bool rasterize = hasEffects || object.type == ShapeType::Dimension;
        if (rasterize) {
            // Effects are raster by nature, so the flattened result is embedded
            // as a PNG layer; the SVG keeps the exact pixels seen on the canvas.
            const QRectF area = effectBounds(object).adjusted(-2.0, -2.0, 2.0, 2.0);
            const qreal exportScale = 2.0;
            QImage layer(qMax(1, qRound(area.width() * exportScale)),
                         qMax(1, qRound(area.height() * exportScale)),
                         QImage::Format_ARGB32_Premultiplied);
            layer.fill(Qt::transparent);
            {
                QPainter painter(&layer);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.scale(exportScale, exportScale);
                painter.translate(-area.left(), -area.top());
                paintObject(&painter, object);
            }
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            layer.save(&buffer, "PNG");
            buffer.close();
            svg += QStringLiteral("  <g transform=\"translate(%1,%2) rotate(%3 %4 %5)\">\n")
                       .arg(num(object.pos.x()), num(object.pos.y()), num(object.rotation),
                            num(bounds.center().x()), num(bounds.center().y()));
            svg += QStringLiteral("    <image x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                                  "xlink:href=\"data:image/png;base64,%5\"/>\n")
                       .arg(num(area.left()), num(area.top()), num(area.width()),
                            num(area.height()), QString::fromLatin1(png.toBase64()));
            svg += QStringLiteral("  </g>\n");
            continue;
        }

        const QString gradientId = QStringLiteral("grad%1").arg(objectIndex++);
        const QRectF fillBounds = object.type == ShapeType::Text ? object.textBox
                                                                 : object.path.boundingRect();
        const bool gradientFill = object.hasFill && object.fillStyle != 0
                                  && fillBounds.width() > 0.01
                                  && fillBounds.height() > 0.01;
        QString gradientMarkup;
        if (gradientFill) {
            QVector<GradientStop> stops = object.gradientStops;
            if (stops.size() < 2) {
                stops = {GradientStop{0.0, object.fill, 1.0},
                         GradientStop{1.0, object.fill2, 1.0}};
            }
            QString stopMarkup;
            for (const GradientStop &stop : stops) {
                stopMarkup += QStringLiteral(
                                  "<stop offset=\"%1\" stop-color=\"%2\" "
                                  "stop-opacity=\"%3\"/>")
                                  .arg(num(qBound(0.0, stop.position, 1.0)),
                                       stop.color.name(QColor::HexRgb),
                                       num(stop.color.alphaF()
                                           * qBound(0.0, stop.opacity, 1.0)));
            }

            const bool linearLike = object.fillStyle == 1 || object.fillStyle == 4
                                    || object.fillStyle == 6;
            if (linearLike) {
                const qreal angle = object.gradientAngle * kPi / 180.0;
                const QPointF direction(std::cos(angle), std::sin(angle));
                const qreal half = std::hypot(fillBounds.width(),
                                              fillBounds.height()) / 2.0;
                const QPointF start = fillBounds.center() - direction * half;
                const QPointF end = fillBounds.center() + direction * half;
                gradientMarkup = QStringLiteral(
                    "<defs><linearGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" "
                    "x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\">%6"
                    "</linearGradient></defs>")
                                     .arg(gradientId, num(start.x()), num(start.y()),
                                          num(end.x()), num(end.y()), stopMarkup);
            } else {
                const QPointF center(
                    fillBounds.left() + fillBounds.width() * object.gradientCenter.x(),
                    fillBounds.top() + fillBounds.height() * object.gradientCenter.y());
                const qreal radius = qMax<qreal>(0.01, object.gradientRadius)
                                     * qMax(fillBounds.width(), fillBounds.height());
                gradientMarkup = QStringLiteral(
                    "<defs><radialGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" "
                    "cx=\"%2\" cy=\"%3\" r=\"%4\">%5"
                    "</radialGradient></defs>")
                                     .arg(gradientId, num(center.x()), num(center.y()),
                                          num(radius), stopMarkup);
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

            const QString lineCap = object.capStyle == 0
                                        ? QStringLiteral("butt")
                                        : (object.capStyle == 1 ? QStringLiteral("square")
                                                                : QStringLiteral("round"));
            const QString lineJoin = object.joinStyle == 0
                                         ? QStringLiteral("miter")
                                         : (object.joinStyle == 1
                                                ? QStringLiteral("bevel")
                                                : QStringLiteral("round"));
            QString dashAttributes;
            if (!object.dashPattern.isEmpty()) {
                QStringList dashes;
                for (qreal dash : object.dashPattern)
                    dashes << num(dash * object.strokeWidth);
                dashAttributes = QStringLiteral(" stroke-dasharray=\"%1\"")
                                     .arg(dashes.join(QLatin1Char(' ')));
            }
            svg += QStringLiteral("    <path d=\"%1\" fill=\"%2\" stroke=\"%3\" "
                                  "stroke-width=\"%4\" stroke-linejoin=\"%5\" "
                                  "stroke-linecap=\"%6\" stroke-miterlimit=\"%7\"%8%9/>\n")
                       .arg(svgPathData(object.path), fill, stroke,
                            num(object.strokeWidth), lineJoin, lineCap,
                            num(object.miterLimit), dashAttributes, opacityAttributes);
        }

        if (object.type == ShapeType::Text && !object.text.isEmpty()
            && ((object.textOnPath && !object.textPathCurve.isEmpty())
                || (object.textInShape && !object.textShape.isEmpty()))) {
            const QPainterPath glyphs = buildTextPath(object);
            const QString textFill = gradientFill
                                         ? QStringLiteral("url(#%1)").arg(gradientId)
                                         : object.fill.name(QColor::HexRgb);
            svg += QStringLiteral("    <path d=\"%1\" fill=\"%2\"/>\n")
                       .arg(svgPathData(glyphs), textFill);
        } else if (object.type == ShapeType::Text && !object.text.isEmpty()) {
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
