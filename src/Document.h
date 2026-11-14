#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QMap>
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

struct GradientStop {
    qreal position = 0.0;        // 0..1 along the gradient
    QColor color = QColor(Qt::white);
    qreal opacity = 1.0;         // per-stop transparency
};

struct PageInfo {
    QString name;
    QSizeF size = QSizeF(794.0, 1123.0);
    QColor background = QColor(Qt::white);
    qreal margin = 37.8;      // px, ~10 mm
    qreal bleed = 11.34;      // px, ~3 mm
    int master = -1;          // index into Document::masters
};

struct MasterPage {
    QString name;
    QString header;
    QString footer;
    bool pageNumbers = false;
};

struct VectorObject {
    int page = -9999;            // -9999 = current page, >=0 page, <0 master
    ShapeType type = ShapeType::Rectangle;
    int layer = -1;              // -1 = active layer when added to the canvas
    int group = 0;               // 0 = ungrouped, otherwise immediate group id
    int id = 0;                  // stable object id, used by clones
    int masterId = 0;            // 0 = ordinary object, otherwise clone source
    QPainterPath path;          // geometry in local coordinates
    QPointF pos;                // placement on the page
    qreal rotation = 0.0;       // degrees, around the local bounds centre

    QColor fill = QColor("#4f7cc0");
    QColor fill2 = QColor("#ffffff");
    // 0 solid, 1 linear, 2 radial, 3 conic, 4 multi-stop, 5 free, 6 mesh
    int fillStyle = 0;
    qreal gradientAngle = 0.0;   // degrees, for linear/conic gradients
    QVector<GradientStop> gradientStops; // custom stops; empty = fill -> fill2
    QPointF gradientCenter = QPointF(0.5, 0.5); // normalised gradient centre
    qreal gradientRadius = 0.5;  // normalised radius for radial/free gradients
    QColor meshColor3 = QColor("#ffd166"); // mesh bottom-left
    QColor meshColor4 = QColor("#06d6a0"); // mesh bottom-right
    QPointF pivot = QPointF(0.5, 0.5); // normalised rotation centre inside bounds
    QColor stroke = QColor("#1d2127");
    qreal strokeWidth = 2.0;
    int capStyle = 2;            // 0 butt, 1 square, 2 round
    int joinStyle = 2;           // 0 miter, 1 bevel, 2 round
    qreal miterLimit = 2.0;
    QVector<qreal> dashPattern;  // empty = solid; values are pen-width units
    bool hasFill = true;
    bool hasStroke = true;
    qreal opacity = 1.0;
    qreal fillOpacity = 1.0;     // independent fill transparency
    qreal strokeOpacity = 1.0;   // independent outline transparency
    int blendMode = 0;           // QPainter::CompositionMode index
    bool shadow = false;
    bool locked = false;
    QPointF shadowOffset = QPointF(8.0, 8.0);
    qreal shadowOpacity = 0.28;
    QColor shadowColor = QColor(0, 0, 0);

    // Shape parameters, kept so the property bar can rebuild the geometry.
    qreal cornerRadius = 0.0;
    int sides = 6;
    int starPoints = 5;
    qreal starRatio = 0.45;
    qreal spiralTurns = 3.0;

    QString text;
    QFont font;
    qreal letterSpacing = 0.0;   // pixels
    qreal lineSpacing = 1.0;     // multiplier
    QRectF textBox;             // text / image frame in local coordinates
    int textAlign = 0;          // 0 = left, 1 = centre, 2 = right
    bool underline = false;
    bool strikeOut = false;
    int script = 0;            // 0 normal, 1 superscript, 2 subscript
    qreal wordSpacing = 0.0;   // pixels between words
    qreal horizontalScale = 1.0;
    qreal verticalScale = 1.0;
    qreal baselineShift = 0.0; // pixels, additional to script offset
    qreal paragraphSpacing = 0.0;
    qreal firstLineIndent = 0.0;
    bool textOnPath = false;
    QPainterPath textPathCurve;
    qreal textPathOffset = 0.0;   // 0..1 start position along the curve
    bool textPathReverse = false;
    bool textInShape = false;
    QPainterPath textShape;
    QStringList fontFeatures;        // OpenType tags: liga, dlig, smcp, tnum, ...
    QMap<QString, qreal> variableAxes; // axis tag -> value
    QImage image;

    QRectF localBounds() const;
};

struct VectorGroup {
    int id = 0;
    int parent = 0;

    bool operator==(const VectorGroup &other) const
    {
        return id == other.id && parent == other.parent;
    }
};

class Document
{
public:
    QSizeF pageSize = QSizeF(794.0, 1123.0); // current page, A4 at 96 dpi
    QVector<PageInfo> pages;
    QVector<MasterPage> masters;
    int currentPage = 0;
    QStringList layers = {QStringLiteral("Слой 1")};
    QVector<bool> layerVisible = {true};
    QVector<bool> layerLocked = {false};
    QVector<VectorGroup> groups;
    QVector<qreal> guidesX;
    QVector<qreal> guidesY;
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

// Colour-space helpers used by the colour picker and palettes.
QString colorToHex(const QColor &color, bool alpha = false);
QColor colorFromHex(const QString &text, const QColor &fallback = Qt::black);
void colorToHsl(const QColor &color, qreal *h, qreal *s, qreal *l);
QColor colorFromHsl(qreal h, qreal s, qreal l, int alpha = 255);
void colorToCmyk(const QColor &color, qreal *c, qreal *m, qreal *y, qreal *k);
QColor colorFromCmyk(qreal c, qreal m, qreal y, qreal k, int alpha = 255);
void colorToLab(const QColor &color, qreal *l, qreal *a, qreal *b);
QColor colorFromLab(qreal l, qreal a, qreal b, int alpha = 255);

QPainterPath buildRectanglePath(const QRectF &bounds, qreal cornerRadius);
QPainterPath buildPolygonPath(const QRectF &bounds, int sides);
QPainterPath buildStarPath(const QRectF &bounds, int points, qreal ratio);
QPainterPath buildSpiralPath(const QRectF &bounds, qreal turns);
QPainterPath buildTextPath(const VectorObject &object);
