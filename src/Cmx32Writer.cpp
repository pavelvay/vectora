#include "CmxIO.h"

#include <QBuffer>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTransform>
#include <QtEndian>
#include <QtMath>

#include <cstring>

#include "Document.h"

// CorelDRAW 2020 writes a 32-bit CMX 1 container (major 2, millimetre/inch
// scale 1/254000, tagged instructions). This writer mirrors that layout, but
// CorelDRAW still rejects the result, so exportFile() keeps the feature
// disabled until the remaining differences are understood.
namespace CmxIO {
namespace {

constexpr qreal kScale = 254000.0 / 96.0; // pixels -> CMX 32-bit units

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

void putTag(QByteArray &out, quint8 id, const QByteArray &payload)
{
    out.append(char(id));
    put16(out, 3 + payload.size());
    out.append(payload);
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

QColor flat(const QColor &color, qreal opacity)
{
    const qreal alpha = qBound(0.0, color.alphaF() * opacity, 1.0);
    return QColor(qRound(color.red() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.green() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.blue() * alpha + 255.0 * (1.0 - alpha)));
}

QColor fillColor(const VectorObject &object)
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
    const qreal n = object.gradientStops.size();
    return QColor::fromRgbF(qBound(0.0, r / n, 1.0),
                            qBound(0.0, g / n, 1.0),
                            qBound(0.0, b / n, 1.0));
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

struct DrawItem {
    QPainterPath path;
    QRectF bounds;
    const VectorObject *object = nullptr;
    bool hasFill = false;
    bool hasStroke = false;
    int fillRef = 0;
    int outlineRef = 0; // 0 means no outline
};

struct OutlineDef {
    int colorRef = 1;
    int width = 1;
    int lineStyleRef = 1;
};

struct Rect32 {
    int left = 0;
    int bottom = 0;
    int right = 0;
    int top = 0;
};

int coord(qreal px)
{
    return qRound(px * kScale);
}

Rect32 rect32(const QRectF &rect)
{
    Rect32 result;
    result.left = coord(rect.left());
    result.bottom = coord(rect.bottom());
    result.right = coord(rect.right());
    result.top = coord(rect.top());
    return result;
}

bool pathPoints(const QPainterPath &path, QVector<QPoint> *points,
                QVector<quint8> *nodes)
{
    int index = 0;
    while (index < path.elementCount()) {
        if (path.elementAt(index).type != QPainterPath::MoveToElement) {
            ++index;
            continue;
        }
        const int start = points->size();
        const QPoint first(coord(path.elementAt(index).x),
                           coord(path.elementAt(index).y));
        points->append(first);
        nodes->append(0x04); // move + user
        QPoint last = first;
        ++index;
        while (index < path.elementCount()
               && path.elementAt(index).type != QPainterPath::MoveToElement) {
            const QPainterPath::Element element = path.elementAt(index);
            if (element.type == QPainterPath::LineToElement) {
                last = QPoint(coord(element.x), coord(element.y));
                points->append(last);
                nodes->append(0x44); // line + user
                ++index;
            } else if (element.type == QPainterPath::CurveToElement
                       && index + 2 < path.elementCount()) {
                const QPainterPath::Element c2 = path.elementAt(index + 1);
                const QPainterPath::Element end = path.elementAt(index + 2);
                points->append(QPoint(coord(element.x), coord(element.y)));
                nodes->append(0xC0);
                points->append(QPoint(coord(c2.x), coord(c2.y)));
                nodes->append(0xC0);
                last = QPoint(coord(end.x), coord(end.y));
                points->append(last);
                nodes->append(0x84); // curve + user
                index += 3;
            } else {
                ++index;
            }
        }
        if (points->size() - start > 1 && last == first)
            (*nodes)[nodes->size() - 1] = quint8((*nodes).last() | 0x08);
    }
    return !points->isEmpty();
}

QByteArray colorTable(const QVector<QColor> &colors)
{
    QByteArray body;
    put16(body, colors.size());
    for (const QColor &color : colors) {
        QByteArray base;
        base.append(char(5)); // RGB
        base.append(char(5)); // user palette
        putTag(body, 1, base);
        QByteArray values;
        values.append(char(color.red()));
        values.append(char(color.green()));
        values.append(char(color.blue()));
        putTag(body, 2, values);
        body.append(char(0xff));
    }
    return leafChunk("rclr", body);
}

int colorRefFor(QVector<QColor> &colors, const QColor &color)
{
    for (int i = 0; i < colors.size(); ++i) {
        if (colors.at(i) == color)
            return i + 1;
    }
    colors.append(color);
    return colors.size();
}

int packLineStyle(const VectorObject &object)
{
    int cap = 0; // butt
    if (object.capStyle == 2)
        cap = 1; // round
    else if (object.capStyle == 1)
        cap = 2; // square
    int join = 0; // miter
    if (object.joinStyle == 2)
        join = 1; // round
    else if (object.joinStyle == 1)
        join = 2; // bevel
    return cap | (join << 4);
}

QByteArray outlineTable(const QVector<OutlineDef> &defs)
{
    QByteArray body;
    put16(body, defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        const OutlineDef &def = defs.at(i);
        QByteArray spec;
        put16(spec, def.lineStyleRef);
        put16(spec, 1); // screen
        put16(spec, def.colorRef);
        put16(spec, 0); // arrow heads
        put16(spec, i + 1); // matching pen record
        put16(spec, 0); // dash array
        putTag(body, 1, spec);
        body.append(char(0xff));
    }
    return leafChunk("rotl", body);
}

QByteArray penTable(const QVector<OutlineDef> &defs)
{
    QByteArray body;
    put16(body, defs.size());
    for (const OutlineDef &def : defs) {
        QByteArray spec;
        put32(spec, quint32(qint32(def.width)));
        put16(spec, 100); // aspect 1.0
        put32(spec, 0); // rotation angle
        put16(spec, 1); // identity matrix
        putTag(body, 1, spec);
        body.append(char(0xff));
    }
    return leafChunk("rpen", body);
}

QByteArray lineStyleTable(const QVector<int> &styles)
{
    QByteArray body;
    put16(body, styles.size());
    for (int style : styles) {
        QByteArray spec;
        spec.append(char(2)); // solid line
        spec.append(char(style & 0xff)); // caps (low nibble) and joins (high nibble)
        putTag(body, 1, spec);
        body.append(char(0xff));
    }
    return leafChunk("rott", body);
}

QByteArray polyCurve(const DrawItem &item)
{
    QVector<QPoint> points;
    QVector<quint8> nodes;
    if (!pathPoints(item.path, &points, &nodes))
        return QByteArray();

    QByteArray payload;

    QByteArray render;
    render.append(char((item.hasFill ? 1 : 0) | (item.hasStroke ? 2 : 0)));
    if (item.hasFill) {
        QByteArray uniform;
        put16(uniform, item.fillRef);
        put16(uniform, 1); // screen
        QByteArray fill;
        put16(fill, 1); // uniform fill
        putTag(fill, 1, uniform);
        fill.append(char(0xff));
        putTag(render, 1, fill);
        render.append(char(0xff)); // end of fill block
    }
    if (item.hasStroke) {
        QByteArray outline;
        put16(outline, item.outlineRef);
        putTag(render, 1, outline);
        render.append(char(0xff)); // end of outline block
    }
    putTag(payload, 1, render);

    QByteArray pointList;
    put16(pointList, points.size());
    for (const QPoint &point : points) {
        put32(pointList, quint32(qint32(point.x())));
        put32(pointList, quint32(qint32(point.y())));
    }
    for (quint8 node : nodes)
        pointList.append(char(node));
    putTag(payload, 2, pointList);

    const Rect32 box = rect32(item.bounds);
    QByteArray bbox;
    put32(bbox, quint32(qint32(box.left)));
    put32(bbox, quint32(qint32(box.bottom)));
    put32(bbox, quint32(qint32(box.right)));
    put32(bbox, quint32(qint32(box.top)));
    putTag(payload, 3, bbox);
    payload.append(char(0xff));
    return instruction(67, payload);
}

QByteArray infoChunk(const char *identifier, const QString &text)
{
    QByteArray body = text.toUtf8();
    const int padding = (body.size() / 32 + 1) * 32 - body.size();
    body.append(QByteArray(padding, '\0'));
    return leafChunk(identifier, body);
}

} // namespace

bool exportFile32(const Document &document, const QString &path, QString *error)
{
    const int sourcePage =
        qBound(0, document.currentPage, qMax(0, document.pages.size() - 1));
    Document pageDocument = document;
    pageDocument.currentPage = sourcePage;
    if (sourcePage < document.pages.size())
        pageDocument.pageSize = document.pages.at(sourcePage).size;

    QVector<DrawItem> items;
    for (const VectorObject &object : document.objects) {
        if (!visibleOnPage(pageDocument, object))
            continue;
        if (object.opacity <= 0.001)
            continue;
        const bool hasFill = object.hasFill && object.fillOpacity > 0.001;
        const bool hasStroke = object.hasStroke && object.strokeOpacity > 0.001
                               && object.strokeWidth > 0.001;
        if (!hasFill && !hasStroke)
            continue;
        QPainterPath geometry;
        if (object.type == ShapeType::Text && !object.text.isEmpty())
            geometry = buildTextPath(object);
        else
            geometry = object.path;
        if (geometry.isEmpty())
            continue;
        const QRectF bounds = object.localBounds();
        QTransform transform;
        transform.translate(object.pos.x(), object.pos.y());
        if (!qFuzzyIsNull(object.rotation)) {
            transform.translate(bounds.center().x(), bounds.center().y());
            transform.rotate(object.rotation);
            transform.translate(-bounds.center().x(), -bounds.center().y());
        }
        geometry = transform.map(geometry);
        QTransform flip;
        flip.translate(0.0, pageDocument.pageSize.height());
        flip.scale(1.0, -1.0);
        geometry = flip.map(geometry);
        if (geometry.isEmpty())
            continue;
        DrawItem item;
        item.path = geometry;
        item.bounds = geometry.boundingRect();
        item.object = &object;
        item.hasFill = hasFill;
        item.hasStroke = hasStroke;
        items.append(item);
    }

    QRectF pageBox;
    if (pageDocument.pageSize.width() > 0.5 && pageDocument.pageSize.height() > 0.5) {
        pageBox = QRectF(QPointF(0, 0), pageDocument.pageSize);
    } else {
        for (const DrawItem &item : items)
            pageBox = pageBox.isNull() ? item.bounds : pageBox.united(item.bounds);
        if (pageBox.isNull() || pageBox.width() < 0.5 || pageBox.height() < 0.5)
            pageBox = QRectF(0, 0, 100, 100);
    }

    QVector<QColor> colors;
    QVector<OutlineDef> outlines;
    QVector<int> lineStyles;
    for (DrawItem &item : items) {
        const VectorObject &object = *item.object;
        if (item.hasFill) {
            item.fillRef = colorRefFor(
                colors, flat(fillColor(object), object.opacity * object.fillOpacity));
        }
        if (item.hasStroke) {
            const QColor stroke = flat(object.stroke,
                                       object.opacity * object.strokeOpacity);
            const int colorRef = colorRefFor(colors, stroke);
            const qreal width = object.strokeWidth > 0.01 ? object.strokeWidth : 1.0;
            const int widthRef = qMax(1, qRound(width * kScale));
            const int packed = packLineStyle(object);
            int lineStyleRef = 0;
            for (int i = 0; i < lineStyles.size(); ++i) {
                if (lineStyles.at(i) == packed) {
                    lineStyleRef = i + 1;
                    break;
                }
            }
            if (lineStyleRef == 0) {
                lineStyles.append(packed);
                lineStyleRef = lineStyles.size();
            }
            int outlineRef = 0;
            for (int i = 0; i < outlines.size(); ++i) {
                const OutlineDef &def = outlines.at(i);
                if (def.colorRef == colorRef && def.width == widthRef
                    && def.lineStyleRef == lineStyleRef) {
                    outlineRef = i + 1;
                    break;
                }
            }
            if (outlineRef == 0) {
                OutlineDef def;
                def.colorRef = colorRef;
                def.width = widthRef;
                def.lineStyleRef = lineStyleRef;
                outlines.append(def);
                outlineRef = outlines.size();
            }
            item.outlineRef = outlineRef;
        }
    }

    const Rect32 pageRect = rect32(pageBox);
    const QByteArray beginPage = [&]() {
        QByteArray payload;
        QByteArray spec;
        put16(spec, 1);
        put32(spec, 0);
        put32(spec, quint32(qint32(pageRect.left)));
        put32(spec, quint32(qint32(pageRect.bottom)));
        put32(spec, quint32(qint32(pageRect.right)));
        put32(spec, quint32(qint32(pageRect.top)));
        spec.append(QByteArray(10, '\0'));
        putTag(payload, 1, spec);
        QByteArray matrix;
        put16(matrix, 1);
        putTag(payload, 2, matrix);
        QByteArray mapping;
        mapping.append('\0');
        putTag(payload, 3, mapping);
        putTag(payload, 4, QByteArray(8, '\0'));
        payload.append(char(0xff));
        return instruction(9, payload);
    }();

    const QByteArray beginLayer = [&]() {
        QByteArray payload;
        const QByteArray name = QByteArrayLiteral("Layer 1");
        QByteArray spec;
        put16(spec, 1);
        put16(spec, 1);
        put32(spec, 0);
        put32(spec, items.size() + 1);
        put16(spec, name.size());
        spec.append(name);
        putTag(payload, 1, spec);
        QByteArray matrix;
        put16(matrix, 1);
        putTag(payload, 2, matrix);
        QByteArray mapping;
        mapping.append('\0');
        putTag(payload, 3, mapping);
        QByteArray wideName;
        const QString text = QString::fromLatin1(name);
        put16(wideName, text.size());
        for (const QChar c : text) {
            put16(wideName, c.unicode());
        }
        putTag(payload, 4, wideName);
        payload.append(char(0xff));
        return instruction(11, payload);
    }();

    QByteArray pageBody;
    pageBody.append(beginPage);
    pageBody.append(beginLayer);
    int curveCount = 0;
    for (int i = 0; i < items.size(); ++i) {
        const QByteArray curve = polyCurve(items.at(i));
        if (!curve.isEmpty()) {
            pageBody.append(curve);
            ++curveCount;
        }
    }
    pageBody.append(instruction(12, QByteArray()));
    pageBody.append(instruction(10, QByteArray()));

    const QByteArray pageChunk = leafChunk("page", pageBody);
    const QByteArray rlst = []() {
        QByteArray body;
        put16(body, 1);
        put16(body, 2);
        put16(body, 9);
        put16(body, 1);
        return leafChunk("rlst", body);
    }();
    const QByteArray rclr = colorTable(colors);

    QByteArray rscrBody;
    put16(rscrBody, 1);
    QByteArray screen;
    screen.append(QByteArray::fromHex("00003c000000000040a5ae0200"));
    putTag(rscrBody, 1, screen);
    rscrBody.append(char(0xff));
    const QByteArray rscr = leafChunk("rscr", rscrBody);

    QByteArray rotaBody;
    put16(rotaBody, 1);
    rotaBody.append(QByteArray(4, '\0'));
    const QByteArray rota = leafChunk("rota", rotaBody);

    QByteArray emptyBody;
    put16(emptyBody, 0);
    const QByteArray rotl = outlineTable(outlines);
    const QByteArray rott = lineStyleTable(lineStyles);
    const QByteArray rpen = penTable(outlines);
    const QByteArray rdot = leafChunk("rdot", emptyBody);

    QByteArray dispChunk;
    {
        const int width = 160;
        const int height = qMax(1, qRound(width * pageDocument.pageSize.height()
                                          / qMax<qreal>(1.0, pageDocument.pageSize.width())));
        QImage preview(width, height, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::white);
        {
            QPainter painter(&preview);
            painter.scale(width / qMax<qreal>(1.0, pageDocument.pageSize.width()),
                          height / qMax<qreal>(1.0, pageDocument.pageSize.height()));
            pageDocument.render(&painter);
        }
        QByteArray bmp;
        QBuffer buffer(&bmp);
        buffer.open(QIODevice::WriteOnly);
        if (preview.save(&buffer, "BMP") && bmp.size() > 14) {
            QByteArray body;
            body.append(QByteArray::fromHex("08000000"));
            body.append(bmp.mid(14));
            dispChunk = leafChunk("DISP", body);
        }
    }

    const int contSize = 188;
    const int rootHeader = 12;
    const int dispOffset = rootHeader + contSize;
    const int pageOffset = dispOffset + dispChunk.size();
    const int rlstOffset = pageOffset + pageChunk.size();
    const int rotlOffset = rlstOffset + rlst.size();
    const int rottOffset = rotlOffset + rotl.size();
    const int rpenOffset = rottOffset + rott.size();
    const int rdotOffset = rpenOffset + rpen.size();
    const int rotaOffset = rdotOffset + rdot.size();
    const int rclrOffset = rotaOffset + rota.size();
    const int rscrOffset = rclrOffset + rclr.size();
    const int indxOffset = rscrOffset + rscr.size();

    const int layerNameSize = QByteArrayLiteral("Layer 1").size();
    int ixlrSize = 8 + 2 + 2 + (4 + 2 + layerNameSize + 4);
    if (ixlrSize % 2 != 0)
        ++ixlrSize;
    const int ixtlSize = 8 + 2 + 2 + 2 + 4;
    const int ixpgSize = 8 + 2 + 2 + 16;
    const int ixmrSize = 8 + 4 + 2 + 10 * 6;
    const int indxSize = 12 + ixlrSize + ixtlSize + ixpgSize + ixmrSize;
    const int ixlrOffset = indxOffset + 12;
    const int ixtlOffset = ixlrOffset + ixlrSize;
    const int ixpgOffset = ixtlOffset + ixtlSize;
    const int ixmrOffset = ixpgOffset + ixpgSize;
    const int infoOffset = indxOffset + indxSize;

    QByteArray contBody;
    QByteArray fileId = QByteArrayLiteral("{Corel Binary Meta File}");
    fileId.append(QByteArray(32 - fileId.size(), '\0'));
    contBody.append(fileId);
    QByteArray osType = QByteArrayLiteral("Windows 3.1");
    osType.append(QByteArray(16 - osType.size(), '\0'));
    contBody.append(osType);
    contBody.append(QByteArray::fromHex("32000000"));
    contBody.append(QByteArray::fromHex("3400")); // 32-bit coordinates
    contBody.append(QByteArray::fromHex("32000000")); // major 2
    contBody.append(QByteArray::fromHex("30000000"));
    contBody.append(QByteArray::fromHex("2300")); // 1/254000 inch units
    putDouble(contBody, 1.0e-7);
    contBody.append(QByteArray(12, '\0'));
    put32(contBody, quint32(ixmrOffset));
    put32(contBody, quint32(infoOffset));
    put32(contBody, dispChunk.isEmpty() ? 0xffffffffu : quint32(dispOffset));
    put32(contBody, quint32(qint32(pageRect.left)));
    put32(contBody, quint32(qint32(pageRect.bottom)));
    put32(contBody, quint32(qint32(pageRect.right)));
    put32(contBody, quint32(qint32(pageRect.top)));
    put32(contBody, quint32(curveCount + 4));
    contBody.append(QByteArray(64, '\0'));
    const QByteArray cont = leafChunk("cont", contBody);

    QByteArray ixlrBody;
    put16(ixlrBody, 1);
    put16(ixlrBody, 1);
    put32(ixlrBody, quint32(pageOffset + 8 + beginPage.size()));
    const QByteArray name = QByteArrayLiteral("Layer 1");
    put16(ixlrBody, name.size());
    ixlrBody.append(name);
    ixlrBody.append(QByteArray::fromHex("ffffffff"));
    const QByteArray ixlr = leafChunk("ixlr", ixlrBody);

    QByteArray ixtlBody;
    put16(ixtlBody, 1);
    put16(ixtlBody, 4);
    put16(ixtlBody, 3);
    put32(ixtlBody, quint32(ixlrOffset));
    const QByteArray ixtl = leafChunk("ixtl", ixtlBody);

    QByteArray ixpgBody;
    put16(ixpgBody, 1);
    put16(ixpgBody, 16);
    put32(ixpgBody, quint32(pageOffset));
    put32(ixpgBody, quint32(ixlrOffset));
    put32(ixpgBody, 0xffffffffu);
    put32(ixpgBody, quint32(rlstOffset));
    const QByteArray ixpg = leafChunk("ixpg", ixpgBody);

    QByteArray ixmrBody;
    ixmrBody.append(QByteArray::fromHex("01001900"));
    put16(ixmrBody, 10);
    auto section = [&ixmrBody](quint16 id, quint32 address) {
        put16(ixmrBody, id);
        put32(ixmrBody, address);
    };
    section(1, quint32(ixmrOffset));
    section(2, quint32(ixpgOffset));
    section(3, quint32(ixtlOffset));
    section(15, quint32(rotlOffset));
    section(16, quint32(rottOffset));
    section(17, quint32(rotaOffset));
    section(18, quint32(rscrOffset));
    section(19, quint32(rpenOffset));
    section(20, quint32(rdotOffset));
    section(21, quint32(rclrOffset));
    const QByteArray ixmr = leafChunk("ixmr", ixmrBody);

    const QByteArray indx = listChunk("indx", ixlr + ixtl + ixpg + ixmr);
    const QByteArray info = listChunk(
        "INFO", infoChunk("IKEY", QString())
                    + infoChunk("ICMT", QStringLiteral("Created by Vectora")));

    QByteArray rootBody = cont + dispChunk + pageChunk + rlst + rotl + rott
                          + rpen + rdot + rota + rclr + rscr + indx + info;
    QByteArray file("RIFF", 4);
    put32(file, quint32(4 + rootBody.size()));
    file.append("CMX1", 4);
    file.append(rootBody);

    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл для записи: %1")
                         .arg(save.errorString());
        return false;
    }
    if (save.write(file) != file.size() || !save.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить CMX: %1")
                         .arg(save.errorString());
        return false;
    }
    Q_UNUSED(QFileInfo(path));
    return true;
}

} // namespace CmxIO
