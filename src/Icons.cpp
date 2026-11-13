#include "Icons.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

QColor ink(const QColor &color)
{
    return color.isValid() ? color : QColor(0xd7, 0xdb, 0xe2);
}

QPen stroke(const QColor &color, qreal width = 1.55)
{
    QPen p(color);
    p.setWidthF(width);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

QPainterPath starPath(const QPointF &center, qreal outer, qreal inner, int points = 5)
{
    QPainterPath path;
    for (int i = 0; i < points * 2; ++i) {
        const qreal radius = (i % 2 == 0) ? outer : inner;
        const qreal angle = -kPi / 2.0 + i * kPi / points;
        const QPointF pt(center.x() + radius * std::cos(angle),
                         center.y() + radius * std::sin(angle));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

QPainterPath polygonPath(const QPointF &center, qreal radius, int sides)
{
    QPainterPath path;
    for (int i = 0; i < sides; ++i) {
        const qreal angle = -kPi / 2.0 + i * 2.0 * kPi / sides;
        const QPointF pt(center.x() + radius * std::cos(angle),
                         center.y() + radius * std::sin(angle));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

void drawArrowHead(QPainter &p, const QPointF &tip, const QPointF &from, qreal size = 4.2)
{
    const QLineF line(from, tip);
    const qreal angle = std::atan2(-line.dy(), line.dx());
    QPolygonF head;
    head << tip
         << tip + QPointF(std::sin(angle - kPi / 3.0) * size, std::cos(angle - kPi / 3.0) * size)
         << tip + QPointF(std::sin(angle - kPi + kPi / 3.0) * size,
                          std::cos(angle - kPi + kPi / 3.0) * size);
    p.setBrush(p.pen().color());
    p.drawPolygon(head);
}

void paintTool(QPainter &p, const QString &id, const QColor &c)
{
    if (id == QLatin1String("select")) {
        QPainterPath arrow;
        arrow.moveTo(6.2, 3.2);
        arrow.lineTo(18.4, 12.6);
        arrow.lineTo(12.9, 13.7);
        arrow.lineTo(15.6, 19.4);
        arrow.lineTo(13.0, 20.7);
        arrow.lineTo(10.3, 15.0);
        arrow.lineTo(6.5, 18.4);
        arrow.closeSubpath();
        p.setPen(stroke(c, 1.1));
        p.setBrush(c);
        p.drawPath(arrow);
    } else if (id == QLatin1String("node")) {
        QPainterPath curve;
        curve.moveTo(4.0, 18.0);
        curve.cubicTo(7.5, 6.5, 16.0, 19.5, 20.0, 7.0);
        p.drawPath(curve);
        p.setBrush(QColor(0x1b, 0x1d, 0x21));
        p.setPen(stroke(c, 1.4));
        p.drawRect(QRectF(2.6, 16.4, 3.2, 3.2));
        p.drawRect(QRectF(18.2, 5.4, 3.2, 3.2));
        p.setBrush(c);
        p.drawEllipse(QPointF(12.0, 12.9), 1.25, 1.25);
    } else if (id == QLatin1String("crop")) {
        p.drawLine(QPointF(6.5, 3.0), QPointF(6.5, 15.0));
        p.drawLine(QPointF(6.5, 15.0), QPointF(18.5, 15.0));
        p.drawLine(QPointF(3.0, 6.5), QPointF(15.0, 6.5));
        p.drawLine(QPointF(15.0, 6.5), QPointF(15.0, 18.5));
    } else if (id == QLatin1String("rect")) {
        p.drawRoundedRect(QRectF(3.8, 6.0, 16.4, 12.0), 1.6, 1.6);
    } else if (id == QLatin1String("ellipse")) {
        p.drawEllipse(QRectF(3.4, 6.0, 17.2, 12.0));
    } else if (id == QLatin1String("polygon")) {
        p.drawPath(polygonPath(QPointF(12, 12), 9.0, 6));
    } else if (id == QLatin1String("star")) {
        p.drawPath(starPath(QPointF(12, 12.4), 9.0, 3.9));
    } else if (id == QLatin1String("spiral")) {
        QPainterPath spiral;
        for (int i = 0; i <= 160; ++i) {
            const qreal t = i / 160.0;
            const qreal angle = t * 4.7 * kPi;
            const qreal radius = 0.8 + t * 8.6;
            const QPointF pt(12.0 + radius * std::cos(angle), 12.0 + radius * std::sin(angle));
            if (i == 0)
                spiral.moveTo(pt);
            else
                spiral.lineTo(pt);
        }
        p.drawPath(spiral);
    } else if (id == QLatin1String("line")) {
        p.drawLine(QPointF(4.5, 19.0), QPointF(19.0, 5.0));
        p.setBrush(c);
        p.drawEllipse(QPointF(4.5, 19.0), 1.4, 1.4);
        p.drawEllipse(QPointF(19.0, 5.0), 1.4, 1.4);
    } else if (id == QLatin1String("bezier")) {
        QPainterPath curve;
        curve.moveTo(4.0, 18.5);
        curve.cubicTo(8.0, 5.5, 16.0, 19.0, 20.0, 6.0);
        p.drawPath(curve);
        p.setPen(stroke(c, 1.0));
        p.drawLine(QPointF(4.0, 18.5), QPointF(9.0, 9.0));
        p.setBrush(QColor(0x1b, 0x1d, 0x21));
        p.drawRect(QRectF(2.6, 17.1, 2.8, 2.8));
        p.drawRect(QRectF(18.6, 4.6, 2.8, 2.8));
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(9.0, 9.0), 1.2, 1.2);
    } else if (id == QLatin1String("freehand")) {
        QPainterPath path;
        path.moveTo(4.0, 15.5);
        path.cubicTo(6.5, 5.0, 9.5, 20.0, 12.5, 11.0);
        path.cubicTo(15.0, 4.0, 18.0, 16.5, 20.0, 9.0);
        p.drawPath(path);
    } else if (id == QLatin1String("artistic")) {
        QPainterPath brush;
        brush.moveTo(4.5, 17.5);
        brush.cubicTo(9.0, 4.0, 13.5, 20.5, 19.5, 7.5);
        p.setPen(stroke(c, 3.6));
        p.drawPath(brush);
        p.setPen(stroke(c, 1.2));
        p.drawLine(QPointF(4.5, 20.0), QPointF(6.5, 16.0));
    } else if (id == QLatin1String("text")) {
        QFont f(QStringLiteral("Adwaita Sans"));
        f.setPixelSize(17);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(2.0, 3.0, 20.0, 18.0), Qt::AlignCenter, QStringLiteral("A"));
        p.setPen(stroke(c, 1.2));
        p.drawLine(QPointF(4.0, 20.5), QPointF(20.0, 20.5));
    } else if (id == QLatin1String("eyedropper")) {
        p.save();
        p.translate(12.0, 11.0);
        p.rotate(45.0);
        p.drawRoundedRect(QRectF(-2.7, -9.2, 5.4, 12.0), 2.4, 2.4);
        p.drawLine(QPointF(0.0, 2.8), QPointF(0.0, 7.2));
        p.restore();
        QPainterPath tip;
        tip.moveTo(6.3, 17.7);
        tip.lineTo(9.8, 17.0);
        tip.lineTo(7.0, 14.2);
        tip.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(tip);
    } else if (id == QLatin1String("fill")) {
        QPainterPath bucket;
        bucket.moveTo(5.0, 11.2);
        bucket.lineTo(11.0, 5.2);
        bucket.lineTo(17.4, 11.6);
        bucket.lineTo(11.4, 17.6);
        bucket.closeSubpath();
        p.drawPath(bucket);
        p.drawLine(QPointF(8.0, 8.2), QPointF(14.6, 14.8));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(18.4, 17.8), 2.1, 2.1);
    } else if (id == QLatin1String("outline")) {
        QPainterPath nib;
        nib.moveTo(8.8, 3.6);
        nib.lineTo(15.2, 3.6);
        nib.lineTo(14.4, 13.8);
        nib.lineTo(12.0, 20.2);
        nib.lineTo(9.6, 13.8);
        nib.closeSubpath();
        p.drawPath(nib);
        p.drawLine(QPointF(12.0, 4.6), QPointF(12.0, 12.6));
        p.setBrush(c);
        p.drawEllipse(QPointF(12.0, 14.0), 1.0, 1.0);
    } else if (id == QLatin1String("transparency")) {
        p.setPen(Qt::NoPen);
        QColor strong = c;
        strong.setAlpha(210);
        QColor soft = c;
        soft.setAlpha(60);
        p.setBrush(strong);
        p.drawRect(QRectF(3.5, 3.5, 8.5, 8.5));
        p.drawRect(QRectF(12.0, 12.0, 8.5, 8.5));
        p.setBrush(soft);
        p.drawRect(QRectF(12.0, 3.5, 8.5, 8.5));
        p.drawRect(QRectF(3.5, 12.0, 8.5, 8.5));
    } else if (id == QLatin1String("shadow")) {
        QColor soft = c;
        soft.setAlpha(70);
        p.setPen(Qt::NoPen);
        p.setBrush(soft);
        p.drawRoundedRect(QRectF(7.0, 7.0, 14.0, 14.0), 2.0, 2.0);
        p.setPen(stroke(c, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3.0, 3.0, 14.0, 14.0), 2.0, 2.0);
    } else if (id == QLatin1String("blend")) {
        p.drawEllipse(QPointF(5.5, 12.0), 2.8, 2.8);
        p.setBrush(c);
        p.drawEllipse(QPointF(18.5, 12.0), 2.8, 2.8);
        p.setPen(Qt::NoPen);
        QColor mid = c;
        for (int i = 0; i < 3; ++i) {
            mid.setAlpha(90 + i * 60);
            p.setBrush(mid);
            p.drawEllipse(QPointF(9.6 + i * 2.4, 12.0), 1.1, 1.1);
        }
    } else if (id == QLatin1String("zoom") || id == QLatin1String("zoom-in")
               || id == QLatin1String("zoom-out")) {
        p.drawEllipse(QRectF(3.6, 3.4, 12.2, 12.2));
        p.drawLine(QPointF(15.0, 15.0), QPointF(20.2, 20.2));
        p.setPen(stroke(c, 1.4));
        p.drawLine(QPointF(6.6, 9.5), QPointF(12.8, 9.5));
        if (id != QLatin1String("zoom-out"))
            p.drawLine(QPointF(9.7, 6.4), QPointF(9.7, 12.6));
    } else if (id == QLatin1String("pan")) {
        p.drawLine(QPointF(12.0, 4.0), QPointF(12.0, 20.0));
        p.drawLine(QPointF(4.0, 12.0), QPointF(20.0, 12.0));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        QPolygonF up;
        up << QPointF(12.0, 2.6) << QPointF(9.3, 6.4) << QPointF(14.7, 6.4);
        QPolygonF down;
        down << QPointF(12.0, 21.4) << QPointF(9.3, 17.6) << QPointF(14.7, 17.6);
        QPolygonF left;
        left << QPointF(2.6, 12.0) << QPointF(6.4, 9.3) << QPointF(6.4, 14.7);
        QPolygonF right;
        right << QPointF(21.4, 12.0) << QPointF(17.6, 9.3) << QPointF(17.6, 14.7);
        p.drawPolygon(up);
        p.drawPolygon(down);
        p.drawPolygon(left);
        p.drawPolygon(right);
    } else if (id == QLatin1String("swatch")) {
        p.setPen(stroke(QColor(0x00, 0x00, 0x00, 0x90), 1.2));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(3.5, 3.5, 17.0, 17.0), 3.0, 3.0);
    } else if (id == QLatin1String("none")) {
        p.setPen(stroke(QColor(0xe0, 0x46, 0x6b), 1.8));
        p.drawRoundedRect(QRectF(3.8, 3.8, 16.4, 16.4), 3.0, 3.0);
        p.drawLine(QPointF(5.5, 18.5), QPointF(18.5, 5.5));
    }
}

void paintAction(QPainter &p, const QString &id, const QColor &c)
{
    if (id == QLatin1String("new")) {
        QPainterPath page;
        page.moveTo(5.0, 2.6);
        page.lineTo(14.2, 2.6);
        page.lineTo(19.2, 7.6);
        page.lineTo(19.2, 21.4);
        page.lineTo(5.0, 21.4);
        page.closeSubpath();
        p.drawPath(page);
        p.drawLine(QPointF(14.2, 2.6), QPointF(14.2, 7.6));
        p.drawLine(QPointF(14.2, 7.6), QPointF(19.2, 7.6));
    } else if (id == QLatin1String("open")) {
        QPainterPath folder;
        folder.moveTo(2.8, 6.4);
        folder.lineTo(9.2, 6.4);
        folder.lineTo(11.2, 8.6);
        folder.lineTo(21.2, 8.6);
        folder.lineTo(21.2, 19.4);
        folder.lineTo(2.8, 19.4);
        folder.closeSubpath();
        p.drawPath(folder);
        p.drawLine(QPointF(2.8, 11.2), QPointF(21.2, 11.2));
    } else if (id == QLatin1String("save")) {
        p.drawRoundedRect(QRectF(3.6, 3.6, 16.8, 16.8), 2.2, 2.2);
        p.drawRect(QRectF(7.4, 3.8, 9.2, 5.6));
        p.drawRect(QRectF(7.0, 12.6, 10.0, 7.6));
        p.setPen(stroke(c, 1.2));
        p.drawLine(QPointF(9.0, 15.0), QPointF(15.0, 15.0));
    } else if (id == QLatin1String("save-as")) {
        p.drawRoundedRect(QRectF(2.8, 4.4, 14.0, 15.4), 2.0, 2.0);
        p.drawLine(QPointF(15.5, 12.0), QPointF(21.2, 12.0));
        drawArrowHead(p, QPointF(21.4, 12.0), QPointF(17.0, 12.0), 3.6);
    } else if (id == QLatin1String("import")) {
        p.drawRoundedRect(QRectF(4.0, 11.0, 16.0, 9.6), 1.6, 1.6);
        p.drawLine(QPointF(12.0, 3.0), QPointF(12.0, 12.6));
        drawArrowHead(p, QPointF(12.0, 13.4), QPointF(12.0, 8.0), 3.8);
    } else if (id == QLatin1String("export")) {
        p.drawRoundedRect(QRectF(4.0, 11.0, 16.0, 9.6), 1.6, 1.6);
        p.drawLine(QPointF(12.0, 12.8), QPointF(12.0, 3.4));
        drawArrowHead(p, QPointF(12.0, 2.6), QPointF(12.0, 8.0), 3.8);
    } else if (id == QLatin1String("pdf")) {
        QPainterPath page;
        page.moveTo(5.0, 2.8);
        page.lineTo(14.6, 2.8);
        page.lineTo(19.4, 7.6);
        page.lineTo(19.4, 21.2);
        page.lineTo(5.0, 21.2);
        page.closeSubpath();
        p.drawPath(page);
        QFont f(QStringLiteral("Adwaita Sans"));
        f.setPixelSize(6);
        f.setBold(true);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(4.0, 12.6, 16.0, 8.0), Qt::AlignCenter, QStringLiteral("PDF"));
    } else if (id == QLatin1String("print")) {
        p.drawRoundedRect(QRectF(3.8, 10.0, 16.4, 8.8), 1.6, 1.6);
        p.drawRect(QRectF(6.6, 3.4, 10.8, 6.6));
        p.drawRect(QRectF(6.6, 15.4, 10.8, 5.2));
        p.setBrush(c);
        p.drawEllipse(QPointF(17.2, 13.2), 0.9, 0.9);
    } else if (id == QLatin1String("undo") || id == QLatin1String("redo")) {
        const bool flip = id == QLatin1String("redo");
        p.save();
        if (flip) {
            p.translate(24.0, 0.0);
            p.scale(-1.0, 1.0);
        }
        QPainterPath arc;
        arc.moveTo(19.0, 19.4);
        arc.cubicTo(20.0, 8.6, 9.4, 4.6, 5.2, 11.6);
        p.drawPath(arc);
        QPainterPath head;
        head.moveTo(5.2, 11.8);
        head.lineTo(3.0, 6.6);
        head.lineTo(9.8, 7.4);
        head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(head);
        p.restore();
    } else if (id == QLatin1String("cut")) {
        p.drawEllipse(QRectF(4.2, 15.6, 4.4, 4.4));
        p.drawEllipse(QRectF(11.2, 17.4, 4.4, 4.4));
        p.drawLine(QPointF(7.6, 15.6), QPointF(17.4, 3.6));
        p.drawLine(QPointF(13.6, 17.2), QPointF(9.4, 10.6));
    } else if (id == QLatin1String("copy")) {
        p.drawRoundedRect(QRectF(3.6, 3.6, 11.6, 13.6), 1.4, 1.4);
        p.drawRoundedRect(QRectF(8.8, 7.4, 11.6, 13.6), 1.4, 1.4);
    } else if (id == QLatin1String("paste")) {
        p.drawRoundedRect(QRectF(4.6, 4.8, 14.8, 16.4), 1.8, 1.8);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(8.6, 2.8, 6.8, 4.0), 1.4, 1.4);
    } else if (id == QLatin1String("delete")) {
        p.drawLine(QPointF(4.4, 6.2), QPointF(19.6, 6.2));
        p.drawLine(QPointF(9.6, 3.4), QPointF(14.4, 3.4));
        p.drawRoundedRect(QRectF(6.0, 6.8, 12.0, 14.0), 1.8, 1.8);
        p.drawLine(QPointF(10.0, 10.0), QPointF(10.0, 17.4));
        p.drawLine(QPointF(14.0, 10.0), QPointF(14.0, 17.4));
    } else if (id == QLatin1String("select-all")) {
        QPen dashed = stroke(c, 1.3);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawRect(QRectF(3.8, 3.8, 16.4, 16.4));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRectF(2.4, 2.4, 3.0, 3.0));
        p.drawRect(QRectF(18.6, 2.4, 3.0, 3.0));
        p.drawRect(QRectF(2.4, 18.6, 3.0, 3.0));
        p.drawRect(QRectF(18.6, 18.6, 3.0, 3.0));
    } else if (id == QLatin1String("grid") || id == QLatin1String("guides")) {
        p.setPen(stroke(c, 1.2));
        for (int i = 1; i <= 3; ++i) {
            const qreal pos = 4.0 + i * 4.0;
            p.drawLine(QPointF(pos, 3.6), QPointF(pos, 20.4));
            p.drawLine(QPointF(3.6, pos), QPointF(20.4, pos));
        }
    } else if (id == QLatin1String("snap")) {
        QPainterPath magnet;
        magnet.moveTo(5.6, 4.0);
        magnet.lineTo(5.6, 12.4);
        magnet.cubicTo(5.6, 19.6, 18.4, 19.6, 18.4, 12.4);
        magnet.lineTo(18.4, 4.0);
        p.setPen(stroke(c, 2.6));
        p.drawPath(magnet);
        p.setPen(stroke(c, 1.0));
        p.drawLine(QPointF(5.6, 9.2), QPointF(9.4, 9.2));
        p.drawLine(QPointF(14.6, 9.2), QPointF(18.4, 9.2));
    } else if (id == QLatin1String("fit")) {
        p.setPen(stroke(c, 1.4));
        p.drawLine(QPointF(4.0, 9.0), QPointF(4.0, 4.0));
        p.drawLine(QPointF(4.0, 4.0), QPointF(9.0, 4.0));
        p.drawLine(QPointF(15.0, 4.0), QPointF(20.0, 4.0));
        p.drawLine(QPointF(20.0, 4.0), QPointF(20.0, 9.0));
        p.drawLine(QPointF(20.0, 15.0), QPointF(20.0, 20.0));
        p.drawLine(QPointF(20.0, 20.0), QPointF(15.0, 20.0));
        p.drawLine(QPointF(9.0, 20.0), QPointF(4.0, 20.0));
        p.drawLine(QPointF(4.0, 20.0), QPointF(4.0, 15.0));
        p.drawRect(QRectF(9.4, 9.4, 5.2, 5.2));
    } else if (id == QLatin1String("actual")) {
        p.drawRect(QRectF(4.0, 6.4, 16.0, 11.2));
        p.setPen(stroke(c, 1.2));
        p.drawLine(QPointF(8.0, 10.0), QPointF(16.0, 10.0));
        p.drawLine(QPointF(8.0, 14.0), QPointF(13.0, 14.0));
    } else if (id.startsWith(QLatin1String("align-"))) {
        const QString mode = id.mid(6);
        p.setPen(stroke(c, 2.0));
        if (mode == QLatin1String("left")) {
            p.drawLine(QPointF(4.2, 3.4), QPointF(4.2, 20.6));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(6.4, 6.0, 12.0, 4.0));
            p.drawRect(QRectF(6.4, 13.6, 7.2, 4.0));
        } else if (mode == QLatin1String("right")) {
            p.drawLine(QPointF(19.8, 3.4), QPointF(19.8, 20.6));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(5.6, 6.0, 12.0, 4.0));
            p.drawRect(QRectF(10.4, 13.6, 7.2, 4.0));
        } else if (mode == QLatin1String("hcenter")) {
            p.drawLine(QPointF(12.0, 3.4), QPointF(12.0, 20.6));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(5.0, 6.0, 14.0, 4.0));
            p.drawRect(QRectF(7.6, 13.6, 8.8, 4.0));
        } else if (mode == QLatin1String("top")) {
            p.drawLine(QPointF(3.4, 4.2), QPointF(20.6, 4.2));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(6.0, 6.4, 4.0, 12.0));
            p.drawRect(QRectF(13.6, 6.4, 4.0, 7.2));
        } else if (mode == QLatin1String("bottom")) {
            p.drawLine(QPointF(3.4, 19.8), QPointF(20.6, 19.8));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(6.0, 5.6, 4.0, 12.0));
            p.drawRect(QRectF(13.6, 10.4, 4.0, 7.2));
        } else {
            p.drawLine(QPointF(3.4, 12.0), QPointF(20.6, 12.0));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(6.0, 5.0, 4.0, 14.0));
            p.drawRect(QRectF(13.6, 7.6, 4.0, 8.8));
        }
    } else if (id == QLatin1String("group") || id == QLatin1String("ungroup")) {
        QPen dashed = stroke(c, 1.1);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(3.2, 3.2, 11.6, 11.6));
        p.setPen(stroke(c, 1.5));
        p.setBrush(c);
        p.drawRect(QRectF(9.2, 9.2, 11.6, 11.6));
    } else if (id == QLatin1String("flip-h") || id == QLatin1String("flip-v")) {
        p.save();
        if (id == QLatin1String("flip-v")) {
            p.translate(0.0, 24.0);
            p.scale(1.0, -1.0);
        }
        QPen dashed = stroke(c, 1.1);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(QPointF(12.0, 2.6), QPointF(12.0, 21.4));
        QPainterPath tri;
        tri.moveTo(9.6, 5.0);
        tri.lineTo(9.6, 18.0);
        tri.lineTo(3.2, 11.5);
        tri.closeSubpath();
        p.setPen(stroke(c, 1.5));
        p.drawPath(tri);
        p.restore();
    } else if (id == QLatin1String("rotate")) {
        p.drawArc(QRectF(4.0, 4.0, 16.0, 16.0), 40 * 16, 280 * 16);
        QPainterPath head;
        head.moveTo(19.4, 4.4);
        head.lineTo(14.2, 5.0);
        head.lineTo(17.6, 9.4);
        head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(head);
    } else if (id == QLatin1String("layers")) {
        QPolygonF top;
        top << QPointF(12, 3.0) << QPointF(21, 7.0) << QPointF(12, 11.0) << QPointF(3, 7.0);
        p.drawPolygon(top);
        p.drawLine(QPointF(3, 11.4), QPointF(12, 15.4));
        p.drawLine(QPointF(12, 15.4), QPointF(21, 11.4));
        p.drawLine(QPointF(3, 15.4), QPointF(12, 19.4));
        p.drawLine(QPointF(12, 19.4), QPointF(21, 15.4));
    } else if (id == QLatin1String("layer-add")) {
        p.drawLine(QPointF(3.4, 6.4), QPointF(13.4, 6.4));
        p.drawLine(QPointF(3.4, 10.4), QPointF(10.4, 10.4));
        p.setPen(stroke(c, 2.0));
        p.drawLine(QPointF(17.0, 12.4), QPointF(17.0, 20.4));
        p.drawLine(QPointF(13.0, 16.4), QPointF(21.0, 16.4));
    } else if (id == QLatin1String("fx")) {
        QPainterPath spark;
        spark.moveTo(12.0, 3.0);
        spark.lineTo(14.2, 9.8);
        spark.lineTo(21.0, 12.0);
        spark.lineTo(14.2, 14.2);
        spark.lineTo(12.0, 21.0);
        spark.lineTo(9.8, 14.2);
        spark.lineTo(3.0, 12.0);
        spark.lineTo(9.8, 9.8);
        spark.closeSubpath();
        p.drawPath(spark);
    } else if (id == QLatin1String("help")) {
        p.drawEllipse(QRectF(3.4, 3.4, 17.2, 17.2));
        QFont f(QStringLiteral("Adwaita Sans"));
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(3.0, 2.6, 18.0, 18.0), Qt::AlignCenter, QStringLiteral("?"));
    } else if (id == QLatin1String("settings")) {
        p.drawEllipse(QRectF(8.4, 8.4, 7.2, 7.2));
        p.drawEllipse(QRectF(3.4, 3.4, 17.2, 17.2));
    }
}

} // namespace

QIcon Icons::appIcon()
{
    constexpr int size = 64;
    QPixmap pixmap(size * 2, size * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0.0, 0.0, size, size);
    gradient.setColorAt(0.0, QColor("#5b8def"));
    gradient.setColorAt(1.0, QColor("#8a5cf6"));

    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawRoundedRect(QRectF(2.0, 2.0, 60.0, 60.0), 15.0, 15.0);

    p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(4.0, 4.0, 56.0, 56.0), 13.0, 13.0);

    QFont font(QStringLiteral("Adwaita Sans"));
    font.setPixelSize(36);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRectF(0.0, -2.0, size, size), Qt::AlignCenter, QStringLiteral("V"));

    p.end();
    return QIcon(pixmap);
}

QIcon Icons::icon(const QString &id, const QColor &color)
{
    constexpr qreal scale = 2.0;
    QPixmap pixmap(int(24 * scale), int(24 * scale));
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(scale);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor c = ink(color);
    p.setPen(stroke(c));
    p.setBrush(Qt::NoBrush);

    paintTool(p, id, c);
    paintAction(p, id, c);

    p.end();
    return QIcon(pixmap);
}
