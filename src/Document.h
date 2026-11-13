#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

enum class ShapeType {
    Rectangle,
    Ellipse,
    Polygon,
    Star,
    Spiral,
    Line,
    Bezier,
    Freehand,
    Artistic,
    Text,
    Image
};

QString shapeTypeId(ShapeType type);
ShapeType shapeTypeFromId(const QString &id);

struct VectorObject {
    ShapeType type = ShapeType::Rectangle;
    int layer = -1;              // -1 = active layer when added to the canvas
    QPainterPath path;          // geometry in local coordinates
    QPointF pos;                // placement on the page
    qreal rotation = 0.0;       // degrees, around the local bounds centre

    QColor fill = QColor("#4f7cc0");
    QColor stroke = QColor("#1d2127");
    qreal strokeWidth = 2.0;
    bool hasFill = true;
    bool hasStroke = true;
    qreal opacity = 1.0;
    bool shadow = false;
    QPointF shadowOffset = QPointF(8.0, 8.0);
    qreal shadowOpacity = 0.28;
    QColor shadowColor = QColor(0, 0, 0);

    QString text;
    QFont font;
    QRectF textBox;             // text / image frame in local coordinates
    int textAlign = 0;          // 0 = left, 1 = centre, 2 = right
    QImage image;

    QRectF localBounds() const;
};

class Document
{
public:
    QSizeF pageSize = QSizeF(794.0, 1123.0); // A4 at 96 dpi
    QStringList layers = {QStringLiteral("Слой 1")};
    QVector<VectorObject> objects;

    void clear();

    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &json, QString *error);

    bool save(const QString &path, QString *error) const;
    bool load(const QString &path, QString *error);

    QString toSvg() const;
    void render(QPainter *painter) const;

    static void paintObject(QPainter *painter, const VectorObject &object);
    static Document sample();
};

QColor interpolateColor(const QColor &from, const QColor &to, qreal t);
