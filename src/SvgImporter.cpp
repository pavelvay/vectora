#include "SvgImporter.h"

#include <QColor>
#include <QFile>
#include <QFontMetricsF>
#include <QMap>
#include <QRegularExpression>
#include <QStack>
#include <QTransform>
#include <QXmlStreamReader>
#include <QtMath>

#include "Document.h"

namespace {

struct GradientDef {
    bool radial = false;
    qreal x1 = 0.0;
    qreal y1 = 0.0;
    qreal x2 = 1.0;
    qreal y2 = 0.0;
    QVector<QPair<qreal, QColor>> stops;
};

struct SvgStyle {
    QColor fill = QColor("#000000");
    QColor stroke = QColor(Qt::transparent);
    bool hasFill = true;
    bool hasStroke = false;
    qreal strokeWidth = 1.0;
    qreal opacity = 1.0;
    QString fillUrl;
    QString strokeUrl;
    QString fontFamily = QStringLiteral("Adwaita Sans");
    qreal fontSize = 16.0;
    bool bold = false;
    int textAnchor = 0; // 0 left, 1 middle, 2 end
};

qreal lengthValue(const QString &text, qreal fallback = 0.0)
{
    static const QRegularExpression number(
        QStringLiteral("^\\s*([-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?)"));
    const auto match = number.match(text);
    if (!match.hasMatch())
        return fallback;
    return match.captured(1).toDouble();
}

QColor parseColor(const QString &value, const QColor &fallback)
{
    const QString text = value.trimmed();
    if (text.isEmpty() || text == QLatin1String("none"))
        return QColor(Qt::transparent);
    if (text.startsWith(QLatin1Char('#'))) {
        QString hex = text.mid(1);
        if (hex.size() == 3) {
            hex = QStringLiteral("%1%1%2%2%3%3")
                      .arg(hex.at(0), hex.at(1), hex.at(2));
        }
        QColor color(hex);
        if (color.isValid())
            return color;
    }
    const QColor named(text);
    return named.isValid() ? named : fallback;
}

void applyStyleString(SvgStyle &style, const QString &css)
{
    const QStringList items = css.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &item : items) {
        const int colon = item.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = item.left(colon).trimmed();
        const QString value = item.mid(colon + 1).trimmed();
        if (key == QLatin1String("fill")) {
            if (value.startsWith(QLatin1String("url("))) {
                style.fillUrl = value.mid(4, value.indexOf(QLatin1Char(')')) - 4);
                style.fillUrl.remove(QLatin1Char('#'));
            } else {
                style.fillUrl.clear();
                style.fill = parseColor(value, style.fill);
                style.hasFill = style.fill.alpha() > 0;
            }
        } else if (key == QLatin1String("stroke")) {
            style.stroke = parseColor(value, style.stroke);
            style.hasStroke = style.stroke.alpha() > 0;
        } else if (key == QLatin1String("stroke-width")) {
            style.strokeWidth = lengthValue(value, style.strokeWidth);
        } else if (key == QLatin1String("opacity")) {
            style.opacity *= qBound(0.0, lengthValue(value, 1.0), 1.0);
        } else if (key == QLatin1String("fill-opacity")) {
            QColor color = style.fill;
            color.setAlphaF(qBound(0.0, lengthValue(value, 1.0), 1.0)
                            * color.alphaF());
            style.fill = color;
            style.hasFill = color.alpha() > 0;
        } else if (key == QLatin1String("stroke-opacity")) {
            QColor color = style.stroke;
            color.setAlphaF(qBound(0.0, lengthValue(value, 1.0), 1.0)
                            * color.alphaF());
            style.stroke = color;
            style.hasStroke = color.alpha() > 0;
        } else if (key == QLatin1String("font-size")) {
            style.fontSize = lengthValue(value, style.fontSize);
        } else if (key == QLatin1String("font-family")) {
            style.fontFamily = value.split(QLatin1Char(',')).first().trimmed();
            style.fontFamily.remove(QLatin1Char('"'));
            style.fontFamily.remove(QLatin1Char('\''));
        } else if (key == QLatin1String("font-weight")) {
            style.bold = value == QLatin1String("bold")
                         || value.toInt() >= 600;
        } else if (key == QLatin1String("text-anchor")) {
            style.textAnchor = value == QLatin1String("middle")
                                   ? 1
                                   : (value == QLatin1String("end") ? 2 : 0);
        }
    }
}

QTransform parseTransform(const QString &text)
{
    QTransform result;
    static const QRegularExpression operation(
        QStringLiteral("(matrix|translate|scale|rotate|skewX|skewY)\\s*\\(([^)]*)\\)"));
    auto it = operation.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString name = match.captured(1);
        QStringList parts = match.captured(2).split(
            QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
        QVector<qreal> values;
        for (const QString &part : parts)
            values.append(part.toDouble());

        QTransform transform;
        if (name == QLatin1String("matrix") && values.size() >= 6) {
            transform = QTransform(values[0], values[1], values[2], values[3],
                                   values[4], values[5]);
        } else if (name == QLatin1String("translate") && values.size() >= 1) {
            transform.translate(values[0], values.value(1));
        } else if (name == QLatin1String("scale") && values.size() >= 1) {
            transform.scale(values[0], values.value(1, values[0]));
        } else if (name == QLatin1String("rotate") && values.size() >= 1) {
            if (values.size() >= 3) {
                transform.translate(values[1], values[2]);
                transform.rotate(values[0]);
                transform.translate(-values[1], -values[2]);
            } else {
                transform.rotate(values[0]);
            }
        } else if (name == QLatin1String("skewX") && values.size() >= 1) {
            transform.shear(std::tan(qDegreesToRadians(values[0])), 0.0);
        } else if (name == QLatin1String("skewY") && values.size() >= 1) {
            transform.shear(0.0, std::tan(qDegreesToRadians(values[0])));
        }
        result = result * transform;
    }
    return result;
}

QVector<qreal> pathNumbers(const QString &data)
{
    QVector<qreal> result;
    static const QRegularExpression number(
        QStringLiteral("[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
    auto it = number.globalMatch(data);
    while (it.hasNext())
        result.append(it.next().captured(0).toDouble());
    return result;
}

QPainterPath parsePathData(const QString &data)
{
    QPainterPath path;
    const QVector<qreal> numbers = pathNumbers(data);
    static const QRegularExpression commandRe(QStringLiteral("[MmLlHhVvCcSsQqTtAaZz]"));
    auto it = commandRe.globalMatch(data);

    int index = 0;
    QChar command;
    QPointF current;
    QPointF start;
    QPointF previousControl;
    bool hasPreviousControl = false;

    const auto point = [&](qreal x, qreal y, bool relative) {
        return relative ? current + QPointF(x, y) : QPointF(x, y);
    };

    while (it.hasNext()) {
        const auto match = it.next();
        command = match.captured(0).at(0);
        const QChar upper = command.toUpper();
        const bool relative = command.isLower();

        if (upper == QLatin1Char('Z')) {
            path.closeSubpath();
            current = start;
            hasPreviousControl = false;
            continue;
        }

        const int count = (upper == QLatin1Char('M') || upper == QLatin1Char('L')
                               || upper == QLatin1Char('T'))
                              ? 2
                              : (upper == QLatin1Char('H') || upper == QLatin1Char('V'))
                                    ? 1
                                    : (upper == QLatin1Char('C'))
                                          ? 6
                                          : (upper == QLatin1Char('S')
                                             || upper == QLatin1Char('Q'))
                                                ? 4
                                                : 7;
        if (index + count > numbers.size())
            break;

        if (upper == QLatin1Char('M')) {
            const QPointF target = point(numbers[index], numbers[index + 1], relative);
            path.moveTo(target);
            current = target;
            start = target;
            index += 2;
        } else if (upper == QLatin1Char('L')) {
            current = point(numbers[index], numbers[index + 1], relative);
            path.lineTo(current);
            index += 2;
        } else if (upper == QLatin1Char('H')) {
            current.setX(relative ? current.x() + numbers[index] : numbers[index]);
            path.lineTo(current);
            ++index;
        } else if (upper == QLatin1Char('V')) {
            current.setY(relative ? current.y() + numbers[index] : numbers[index]);
            path.lineTo(current);
            ++index;
        } else if (upper == QLatin1Char('C')) {
            const QPointF c1 = point(numbers[index], numbers[index + 1], relative);
            const QPointF c2 = point(numbers[index + 2], numbers[index + 3], relative);
            const QPointF end = point(numbers[index + 4], numbers[index + 5], relative);
            path.cubicTo(c1, c2, end);
            previousControl = c2;
            hasPreviousControl = true;
            current = end;
            index += 6;
        } else if (upper == QLatin1Char('S')) {
            const QPointF c1 = hasPreviousControl
                                   ? current * 2.0 - previousControl
                                   : current;
            const QPointF c2 = point(numbers[index], numbers[index + 1], relative);
            const QPointF end = point(numbers[index + 2], numbers[index + 3], relative);
            path.cubicTo(c1, c2, end);
            previousControl = c2;
            hasPreviousControl = true;
            current = end;
            index += 4;
        } else if (upper == QLatin1Char('Q')) {
            const QPointF control = point(numbers[index], numbers[index + 1], relative);
            const QPointF end = point(numbers[index + 2], numbers[index + 3], relative);
            path.quadTo(control, end);
            previousControl = control;
            hasPreviousControl = true;
            current = end;
            index += 4;
        } else if (upper == QLatin1Char('T')) {
            const QPointF control = hasPreviousControl
                                        ? current * 2.0 - previousControl
                                        : current;
            const QPointF end = point(numbers[index], numbers[index + 1], relative);
            path.quadTo(control, end);
            previousControl = control;
            hasPreviousControl = true;
            current = end;
            index += 2;
        } else if (upper == QLatin1Char('A')) {
            const QPointF end = point(numbers[index + 5], numbers[index + 6], relative);
            path.lineTo(end);
            current = end;
            index += 7;
        }
    }
    return path;
}

QPainterPath polygonPath(const QString &points, bool close)
{
    QPainterPath path;
    const QVector<qreal> values = pathNumbers(points);
    for (int i = 0; i + 1 < values.size(); i += 2) {
        if (i == 0)
            path.moveTo(values[i], values[i + 1]);
        else
            path.lineTo(values[i], values[i + 1]);
    }
    if (close)
        path.closeSubpath();
    return path;
}

void applyShapeStyle(VectorObject &object, const SvgStyle &style,
                     const QTransform &transform,
                     const QMap<QString, GradientDef> &gradients)
{
    object.fill = style.fill;
    object.hasFill = style.hasFill;
    object.stroke = style.stroke;
    object.hasStroke = style.hasStroke;
    object.strokeWidth = style.strokeWidth;
    object.opacity = qBound(0.0, style.opacity, 1.0);

    if (style.hasFill && !style.fillUrl.isEmpty()
        && gradients.contains(style.fillUrl)) {
        const GradientDef gradient = gradients.value(style.fillUrl);
        if (!gradient.stops.isEmpty()) {
            object.fill = gradient.stops.first().second;
            object.fill2 = gradient.stops.last().second;
            object.fillStyle = gradient.radial ? 2 : 1;
            const QPointF direction(gradient.x2 - gradient.x1,
                                    gradient.y2 - gradient.y1);
            if (direction.manhattanLength() > 0.001) {
                object.gradientAngle = qRadiansToDegrees(
                    std::atan2(direction.y(), direction.x()));
            }
            object.hasFill = true;
        }
    }

    const qreal scale = std::sqrt(qAbs(transform.determinant()));
    if (scale > 0.0001)
        object.strokeWidth *= scale;
}

} // namespace

bool importSvgFile(const QString &path, Document *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть SVG: %1").arg(file.errorString());
        return false;
    }

    QXmlStreamReader xml(&file);
    Document result;
    result.layers = {QStringLiteral("Импорт SVG")};
    result.layerVisible = {true};
    result.layerLocked = {false};

    QMap<QString, GradientDef> gradients;
    QStack<QTransform> transforms;
    QStack<SvgStyle> styles;
    QStack<int> groupStack;
    transforms.push(QTransform());
    styles.push(SvgStyle());
    groupStack.push(0);

    QTransform rootTransform;
    bool inDefs = false;
    int nextGroupId = 1;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString name = xml.name().toString();
            const QXmlStreamAttributes attributes = xml.attributes();

            if (name == QLatin1String("svg")) {
                const qreal width = lengthValue(attributes.value(QStringLiteral("width")).toString(), 794.0);
                const qreal height = lengthValue(attributes.value(QStringLiteral("height")).toString(), 1123.0);
                if (width > 1.0 && height > 1.0)
                    result.pageSize = QSizeF(width, height);
                const QStringList viewBox = attributes.value(QStringLiteral("viewBox"))
                                                .toString().split(
                                                    QRegularExpression(QStringLiteral("[\\s,]+")),
                                                    Qt::SkipEmptyParts);
                if (viewBox.size() == 4) {
                    const qreal vbWidth = viewBox.at(2).toDouble();
                    const qreal vbHeight = viewBox.at(3).toDouble();
                    if (vbWidth > 0.0 && vbHeight > 0.0) {
                        rootTransform.translate(
                            -viewBox.at(0).toDouble() * width / vbWidth,
                            -viewBox.at(1).toDouble() * height / vbHeight);
                        rootTransform.scale(width / vbWidth, height / vbHeight);
                    }
                }
                continue;
            }

            if (name == QLatin1String("defs")) {
                inDefs = true;
                continue;
            }

            if (inDefs && (name == QLatin1String("linearGradient")
                           || name == QLatin1String("radialGradient"))) {
                GradientDef gradient;
                gradient.radial = name == QLatin1String("radialGradient");
                gradient.x1 = lengthValue(attributes.value(QStringLiteral("x1")).toString(), 0.0);
                gradient.y1 = lengthValue(attributes.value(QStringLiteral("y1")).toString(), 0.0);
                gradient.x2 = lengthValue(attributes.value(QStringLiteral("x2")).toString(), 1.0);
                gradient.y2 = lengthValue(attributes.value(QStringLiteral("y2")).toString(), 0.0);
                const QString id = attributes.value(QStringLiteral("id")).toString();

                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name().toString() == name)
                        break;
                    if (xml.isStartElement() && xml.name().toString() == QLatin1String("stop")) {
                        const QXmlStreamAttributes stopAttributes = xml.attributes();
                        const qreal offset = qBound(
                            0.0, lengthValue(stopAttributes.value(QStringLiteral("offset")).toString(), 0.0), 1.0);
                        QColor color = parseColor(
                            stopAttributes.value(QStringLiteral("stop-color")).toString(),
                            QColor("#000000"));
                        const qreal opacity = lengthValue(
                            stopAttributes.value(QStringLiteral("stop-opacity")).toString(), 1.0);
                        color.setAlphaF(color.alphaF() * qBound(0.0, opacity, 1.0));
                        gradient.stops.append({offset, color});
                    }
                }
                if (!id.isEmpty())
                    gradients.insert(id, gradient);
                continue;
            }

            SvgStyle style = styles.top();
            applyStyleString(style, attributes.value(QStringLiteral("style")).toString());
            if (attributes.hasAttribute(QStringLiteral("fill")))
                applyStyleString(style,
                                 QStringLiteral("fill:%1")
                                     .arg(attributes.value(QStringLiteral("fill")).toString()));
            if (attributes.hasAttribute(QStringLiteral("stroke")))
                applyStyleString(style,
                                 QStringLiteral("stroke:%1")
                                     .arg(attributes.value(QStringLiteral("stroke")).toString()));
            if (attributes.hasAttribute(QStringLiteral("stroke-width")))
                applyStyleString(style,
                                 QStringLiteral("stroke-width:%1")
                                     .arg(attributes.value(QStringLiteral("stroke-width")).toString()));
            if (attributes.hasAttribute(QStringLiteral("opacity")))
                applyStyleString(style,
                                 QStringLiteral("opacity:%1")
                                     .arg(attributes.value(QStringLiteral("opacity")).toString()));
            if (attributes.hasAttribute(QStringLiteral("fill-opacity")))
                applyStyleString(style,
                                 QStringLiteral("fill-opacity:%1")
                                     .arg(attributes.value(QStringLiteral("fill-opacity")).toString()));
            if (attributes.hasAttribute(QStringLiteral("font-size")))
                applyStyleString(style,
                                 QStringLiteral("font-size:%1")
                                     .arg(attributes.value(QStringLiteral("font-size")).toString()));
            if (attributes.hasAttribute(QStringLiteral("font-family")))
                applyStyleString(style,
                                 QStringLiteral("font-family:%1")
                                     .arg(attributes.value(QStringLiteral("font-family")).toString()));
            if (attributes.hasAttribute(QStringLiteral("font-weight")))
                applyStyleString(style,
                                 QStringLiteral("font-weight:%1")
                                     .arg(attributes.value(QStringLiteral("font-weight")).toString()));
            if (attributes.hasAttribute(QStringLiteral("text-anchor")))
                applyStyleString(style,
                                 QStringLiteral("text-anchor:%1")
                                     .arg(attributes.value(QStringLiteral("text-anchor")).toString()));

            QTransform transform = transforms.top()
                                   * parseTransform(attributes.value(QStringLiteral("transform")).toString());

            if (name == QLatin1String("g")) {
                const int groupId = nextGroupId++;
                result.groups.append({groupId, groupStack.top()});
                transforms.push(transform);
                styles.push(style);
                groupStack.push(groupId);
                continue;
            }

            if (name == QLatin1String("text")) {
                const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements)
                                         .simplified();
                if (text.isEmpty())
                    continue;
                const qreal x = lengthValue(attributes.value(QStringLiteral("x")).toString(), 0.0);
                const qreal y = lengthValue(attributes.value(QStringLiteral("y")).toString(), 0.0);
                const QPointF position = rootTransform.map(transform.map(QPointF(x, y)));
                const qreal scale = std::sqrt(qAbs(transform.determinant()));
                const qreal rotation = qRadiansToDegrees(
                    std::atan2(transform.m12(), transform.m11()));

                QFont font(style.fontFamily);
                font.setPixelSize(qMax(4, qRound(style.fontSize * scale)));
                font.setBold(style.bold);
                QFontMetricsF metrics(font);
                const qreal width = metrics.horizontalAdvance(text) + 8.0;

                VectorObject object;
                object.type = ShapeType::Text;
                object.text = text;
                object.font = font;
                object.fill = style.fill;
                object.hasFill = true;
                object.stroke = style.stroke;
                object.hasStroke = style.hasStroke;
                object.strokeWidth = style.strokeWidth;
                object.opacity = style.opacity;
                object.textAlign = style.textAnchor;
                object.textBox = QRectF(0, 0, width, metrics.height() + 4);
                qreal offsetX = 0.0;
                if (style.textAnchor == 1)
                    offsetX = -width / 2.0;
                else if (style.textAnchor == 2)
                    offsetX = -width;
                object.pos = position - QPointF(offsetX * std::cos(qDegreesToRadians(rotation)),
                                                metrics.ascent());
                object.rotation = rotation;
                object.layer = 0;
                object.group = groupStack.top();
                applyShapeStyle(object, style, transform, gradients);
                result.objects.append(object);
                continue;
            }

            QPainterPath path;
            if (name == QLatin1String("path")) {
                path = parsePathData(attributes.value(QStringLiteral("d")).toString());
            } else if (name == QLatin1String("rect")) {
                const QRectF rect(lengthValue(attributes.value(QStringLiteral("x")).toString()),
                                  lengthValue(attributes.value(QStringLiteral("y")).toString()),
                                  lengthValue(attributes.value(QStringLiteral("width")).toString()),
                                  lengthValue(attributes.value(QStringLiteral("height")).toString()));
                if (rect.width() > 0.0 && rect.height() > 0.0) {
                    const qreal rx = lengthValue(
                        attributes.value(QStringLiteral("rx")).toString(), 0.0);
                    path = buildRectanglePath(rect, rx);
                }
            } else if (name == QLatin1String("circle")) {
                const qreal cx = lengthValue(attributes.value(QStringLiteral("cx")).toString());
                const qreal cy = lengthValue(attributes.value(QStringLiteral("cy")).toString());
                const qreal r = lengthValue(attributes.value(QStringLiteral("r")).toString());
                if (r > 0.0)
                    path.addEllipse(QPointF(cx, cy), r, r);
            } else if (name == QLatin1String("ellipse")) {
                const qreal cx = lengthValue(attributes.value(QStringLiteral("cx")).toString());
                const qreal cy = lengthValue(attributes.value(QStringLiteral("cy")).toString());
                const qreal rx = lengthValue(attributes.value(QStringLiteral("rx")).toString());
                const qreal ry = lengthValue(attributes.value(QStringLiteral("ry")).toString());
                if (rx > 0.0 && ry > 0.0)
                    path.addEllipse(QPointF(cx, cy), rx, ry);
            } else if (name == QLatin1String("line")) {
                path.moveTo(lengthValue(attributes.value(QStringLiteral("x1")).toString()),
                            lengthValue(attributes.value(QStringLiteral("y1")).toString()));
                path.lineTo(lengthValue(attributes.value(QStringLiteral("x2")).toString()),
                            lengthValue(attributes.value(QStringLiteral("y2")).toString()));
            } else if (name == QLatin1String("polyline")
                       || name == QLatin1String("polygon")) {
                path = polygonPath(attributes.value(QStringLiteral("points")).toString(),
                                   name == QLatin1String("polygon"));
            }

            if (path.isEmpty())
                continue;

            path = rootTransform.map(transform.map(path));
            VectorObject object;
            object.type = ShapeType::Bezier;
            object.path = path;
            object.pos = QPointF(0, 0);
            object.layer = 0;
            object.group = groupStack.top();
            applyShapeStyle(object, style, transform, gradients);
            result.objects.append(object);
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString();
            if (name == QLatin1String("defs"))
                inDefs = false;
            else if (name == QLatin1String("g") && transforms.size() > 1) {
                transforms.pop();
                styles.pop();
                groupStack.pop();
            }
        }
    }

    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("Ошибка SVG: %1").arg(xml.errorString());
        return false;
    }
    if (result.objects.isEmpty()) {
        if (error)
            *error = QStringLiteral("В SVG не найдено поддерживаемых объектов.");
        return false;
    }

    *document = result;
    return true;
}
