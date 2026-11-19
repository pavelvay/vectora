#include "CmxIO.h"

#include <QByteArray>
#include <QColor>
#include <QBuffer>
#include <QFileInfo>
#include <QPainterPath>
#include <QSaveFile>
#include <QTransform>
#include <QtEndian>
#include <QtMath>

#include <cstring>

#include "CdrIO.h"
#include "Document.h"

#if defined(VECTORA_HAVE_LIBCDR)
#include <librevenge/librevenge.h>
#include <librevenge-stream/librevenge-stream.h>
#include <libcdr/libcdr.h>
#endif

namespace CmxIO {
namespace {

constexpr qreal kPxToPt = 72.0 / 96.0;

// CMX instruction codes.
constexpr quint16 kBeginPage = 9;
constexpr quint16 kBeginLayer = 11;
constexpr quint16 kPolyCurve = 67;
constexpr quint16 kEndLayer = 12;
constexpr quint16 kEndPage = 10;

// PolyCurve style flags and node types.
constexpr quint8 kFillFlag = 0x01;
constexpr quint8 kStrokeFlag = 0x02;
constexpr quint16 kFillUniform = 1;
constexpr quint8 kNodeMove = 0x00;
constexpr quint8 kNodeLine = 0x40;
constexpr quint8 kNodeCurve = 0x80;
constexpr quint8 kNodeArc = 0xC0;
constexpr quint8 kNodeUser = 0x04;
constexpr quint8 kNodeClosed = 0x08;

void put16(QByteArray &out, int value)
{
    const quint16 word = quint16(value);
    char bytes[2] = {char(word & 0xff), char((word >> 8) & 0xff)};
    out.append(bytes, 2);
}

void put32(QByteArray &out, quint32 value)
{
    char bytes[4] = {char(value & 0xff), char((value >> 8) & 0xff),
                     char((value >> 16) & 0xff), char((value >> 24) & 0xff)};
    out.append(bytes, 4);
}

void putDouble(QByteArray &out, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    bits = qToLittleEndian(bits);
    char bytes[8];
    std::memcpy(bytes, &bits, 8);
    out.append(bytes, 8);
}

QByteArray leafChunk(const char *identifier, const QByteArray &body)
{
    QByteArray out(identifier, 4);
    put32(out, quint32(body.size()));
    out.append(body);
    if (out.size() % 2 != 0)
        out.append('\0');
    return out;
}

QByteArray listChunk(const char *name, const QByteArray &children)
{
    QByteArray out("LIST", 4);
    put32(out, quint32(4 + children.size()));
    out.append(name, 4);
    out.append(children);
    return out;
}

QByteArray instruction(quint16 code, const QByteArray &body)
{
    QByteArray out;
    put16(out, 4 + body.size());
    put16(out, code);
    out.append(body);
    return out;
}

QByteArray infoChunk(const char *identifier, const QString &text)
{
    QByteArray body = text.toUtf8();
    const int padding = (body.size() / 32 + 1) * 32 - body.size();
    body.append(QByteArray(padding, '\0'));
    return leafChunk(identifier, body);
}

QColor flattened(const QColor &color, qreal opacity)
{
    const qreal alpha = qBound(0.0, color.alphaF() * opacity, 1.0);
    return QColor(qRound(color.red() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.green() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.blue() * alpha + 255.0 * (1.0 - alpha)));
}

QColor solidFill(const VectorObject &object)
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

bool visibleOnPage(const Document &document, const VectorObject &object)
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

QTransform objectTransform(const VectorObject &object)
{
    const QRectF bounds = object.localBounds();
    QTransform transform;
    transform.translate(object.pos.x(), object.pos.y());
    if (!qFuzzyIsNull(object.rotation)) {
        transform.translate(bounds.center().x(), bounds.center().y());
        transform.rotate(object.rotation);
        transform.translate(-bounds.center().x(), -bounds.center().y());
    }
    return transform;
}

struct DrawItem {
    QPainterPath path;
    QRectF bounds;
    const VectorObject *object = nullptr;
};

struct CmxRect {
    int left = 0;
    int bottom = 0;
    int right = 0;
    int top = 0;
};

CmxRect cmxRect(const QRectF &rect, qreal coef)
{
    CmxRect result;
    result.left = qRound(rect.left() * kPxToPt * coef);
    result.bottom = qRound(rect.bottom() * kPxToPt * coef);
    result.right = qRound(rect.right() * kPxToPt * coef);
    result.top = qRound(rect.top() * kPxToPt * coef);
    return result;
}

QPoint cmxPoint(const QPointF &point, qreal coef)
{
    return QPoint(qRound(point.x() * kPxToPt * coef),
                  qRound(point.y() * kPxToPt * coef));
}

struct StyleTables {
    QVector<QColor> colors;
    QVector<quint16> pens;
    QVector<QByteArray> lineStyles;
    QVector<QByteArray> outlines;

    int colorIndex(const QColor &color)
    {
        for (int i = 0; i < colors.size(); ++i) {
            if (colors.at(i) == color)
                return i + 1;
        }
        colors.append(color);
        return colors.size();
    }

    int outlineIndex(const VectorObject &object, qreal coef)
    {
        const QColor color = flattened(object.stroke, object.opacity * object.strokeOpacity);
        const int colorRef = colorIndex(color);
        const quint16 width = quint16(qMax(1, qRound(object.strokeWidth * kPxToPt * coef)));
        for (int i = 0; i < outlines.size(); ++i) {
            const QByteArray &record = outlines.at(i);
            if (record.size() != 12)
                continue;
            const int style = quint8(record.at(0)) | (quint8(record.at(1)) << 8);
            const int screen = quint8(record.at(2)) | (quint8(record.at(3)) << 8);
            const int ref = quint8(record.at(4)) | (quint8(record.at(5)) << 8);
            const int arrows = quint8(record.at(6)) | (quint8(record.at(7)) << 8);
            const int pen = quint8(record.at(8)) | (quint8(record.at(9)) << 8);
            const int dash = quint8(record.at(10)) | (quint8(record.at(11)) << 8);
            if (ref == colorRef && pen != 0 && width == pens.at(pen - 1)
                && screen == 1 && arrows == 1 && dash == 0) {
                return i + 1;
            }
            Q_UNUSED(style)
        }

        const quint8 joinCap = quint8(((object.joinStyle & 0x0f) << 4)
                                      | (object.capStyle & 0x0f));
        QByteArray lineStyle;
        put16(lineStyle, 0x0002); // solid line
        lineStyle[0] = char(2);
        lineStyle[1] = char(joinCap);

        const int lineStyleRef = lineStyles.size() + 1;
        lineStyles.append(lineStyle);
        pens.append(width);

        QByteArray outline;
        put16(outline, lineStyleRef);
        put16(outline, 1); // screen
        put16(outline, colorRef);
        put16(outline, 1); // arrowheads
        put16(outline, pens.size());
        put16(outline, 0); // dash
        outlines.append(outline);
        return outlines.size();
    }
};

QByteArray polyCurve(const DrawItem &item, qreal coef, StyleTables &tables,
                     const QVector<QPoint> &points, const QVector<quint8> &nodes,
                     const CmxRect &rect)
{
    const VectorObject &object = *item.object;
    const bool fill = object.hasFill && object.fillOpacity > 0.001
                      && object.opacity > 0.001;
    const bool stroke = object.hasStroke && object.strokeWidth > 0.0
                        && object.strokeOpacity > 0.001 && object.opacity > 0.001;

    QByteArray body;
    body.append(char((fill ? kFillFlag : 0) | (stroke ? kStrokeFlag : 0)));
    if (fill) {
        put16(body, kFillUniform);
        put16(body, tables.colorIndex(flattened(solidFill(object),
                                                object.opacity * object.fillOpacity)));
        put16(body, 1); // screen
    }
    if (stroke)
        put16(body, tables.outlineIndex(object, coef));

    put16(body, points.size());
    for (const QPoint &point : points) {
        put16(body, point.x());
        put16(body, point.y());
    }
    for (quint8 node : nodes)
        body.append(char(node));

    put16(body, rect.left);
    put16(body, rect.bottom);
    put16(body, rect.right);
    put16(body, rect.top);
    return instruction(kPolyCurve, body);
}

bool appendPath(const QPainterPath &path, qreal coef, QVector<QPoint> *points,
                QVector<quint8> *nodes)
{
    int index = 0;
    while (index < path.elementCount()) {
        if (path.elementAt(index).type != QPainterPath::MoveToElement) {
            ++index;
            continue;
        }
        const int subpathStart = points->size();
        QPoint start = cmxPoint(QPointF(path.elementAt(index).x,
                                        path.elementAt(index).y), coef);
        points->append(start);
        nodes->append(kNodeMove | kNodeUser);
        QPoint last = start;
        ++index;

        while (index < path.elementCount()
               && path.elementAt(index).type != QPainterPath::MoveToElement) {
            const QPainterPath::Element element = path.elementAt(index);
            if (element.type == QPainterPath::LineToElement) {
                last = cmxPoint(QPointF(element.x, element.y), coef);
                points->append(last);
                nodes->append(kNodeLine | kNodeUser);
                ++index;
            } else if (element.type == QPainterPath::CurveToElement
                       && index + 2 < path.elementCount()) {
                const QPainterPath::Element c2 = path.elementAt(index + 1);
                const QPainterPath::Element end = path.elementAt(index + 2);
                points->append(cmxPoint(QPointF(element.x, element.y), coef));
                nodes->append(kNodeArc);
                points->append(cmxPoint(QPointF(c2.x, c2.y), coef));
                nodes->append(kNodeArc);
                last = cmxPoint(QPointF(end.x, end.y), coef);
                points->append(last);
                nodes->append(kNodeCurve | kNodeUser);
                index += 3;
            } else {
                ++index;
            }
        }

        // Qt closes a subpath by adding a line back to its start. CMX marks
        // the last node of a closed subpath instead of repeating the point.
        if (points->size() - subpathStart > 1 && last == start) {
            // Keep the endpoint that Qt added for closeSubpath(): CMX marks
            // the last node as closed instead of dropping that segment.
            (*nodes)[nodes->size() - 1] =
                quint8((*nodes).last() | kNodeClosed);
        }
    }
    return !points->isEmpty();
}

} // namespace

bool exportFile(const Document &document, const QString &path, QString *error)
{
    // CMX writing is not ready yet: CorelDRAW rejects the generated files.
    // Keep the import path working and tell the user to use PDF/AI/EPS/SVG.
    Q_UNUSED(document);
    Q_UNUSED(path);
    if (error)
        *error = QStringLiteral(
            "Экспорт CMX пока находится в разработке: файл не создаётся. "
            "Для обмена с CorelDRAW используйте PDF, AI, EPS или SVG.");
    return false;
}

bool importFile(const QString &path, Document *document, QString *error)
{
    if (error)
        error->clear();
#if defined(VECTORA_HAVE_LIBCDR)
    try {
        librevenge::RVNGFileStream input(path.toUtf8().constData());
        if (!libcdr::CMXDocument::isSupported(&input)) {
            if (error)
                *error = QStringLiteral("Это не документ Corel CMX.");
            return false;
        }
        librevenge::RVNGStringVector output;
        librevenge::RVNGSVGDrawingGenerator painter(
            output, librevenge::RVNGString("svg"));
        if (!libcdr::CMXDocument::parse(&input, &painter)) {
            if (error)
                *error = QStringLiteral("Не удалось разобрать CMX.");
            return false;
        }
        QVector<QByteArray> pages;
        pages.reserve(int(output.size()));
        for (unsigned index = 0; index < output.size(); ++index)
            pages.append(QByteArray(output[index].cstr()));
        return CdrIO::importSvgPages(pages, document, error,
                                     QStringLiteral("Импорт CMX"));
    } catch (...) {
        if (error)
            *error = QStringLiteral("Не удалось разобрать CMX.");
        return false;
    }
#else
    if (error)
        *error = QStringLiteral(
            "Чтение CMX недоступно в этой сборке (нужен libcdr).");
    return false;
#endif
}

} // namespace CmxIO
