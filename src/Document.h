#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Effects.h"

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
    Image,
    Dimension,
    Code
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

// A blend relationship between two objects. Intermediate objects are generated
// copies tagged with blendId and regenerated whenever an endpoint changes.
struct BlendRecord {
    int id = 0;
    int fromId = 0;
    int toId = 0;
    int steps = 7;
    qreal spacing = 0.0;   // > 0 overrides steps with a fixed distance
    bool active = true;
};

// One drawable shape inside a pattern tile. Kept separate from VectorObject so
// VectorObject can own a list of them without a recursive type.
struct PatternShape {
    QPainterPath path;
    QColor fill = QColor("#4f7cc0");
    QColor stroke = QColor("#1d2127");
    qreal strokeWidth = 0.0;
    bool hasFill = true;
    bool hasStroke = false;
    qreal opacity = 1.0;
};

// Repetition parameters of a pattern fill.
struct PatternSettings {
    int type = 0;             // 0 grid, 1 brick, 2 hex, 3 mirror,
                              // 4 radial, 5 rotation, 6 path
    QSizeF tile = QSizeF(64.0, 64.0);
    qreal spacingX = 0.0;
    qreal spacingY = 0.0;
    qreal offsetX = 0.0;
    qreal offsetY = 0.0;
    qreal angle = 0.0;        // tile rotation / start angle
    qreal scale = 1.0;
    int count = 8;            // radial / rotation / path copies
    bool mirrorX = false;
    bool mirrorY = false;
    qreal pathStep = 80.0;    // distance between copies along a path
    QPainterPath path;        // path repeat guide
};

struct VectorObject {
    int page = -9999;            // -9999 = current page, >=0 page, <0 master
    ShapeType type = ShapeType::Rectangle;
    int layer = -1;              // -1 = active layer when added to the canvas
    int group = 0;               // 0 = ungrouped, otherwise immediate group id
    QString name;                // optional user name, used by search/dockers
    int id = 0;                  // stable object id, used by clones
    int masterId = 0;            // 0 = ordinary object, otherwise clone source
    int blendId = 0;             // 0 = ordinary object, otherwise blend result id
    int symbolId = 0;            // > 0 when the object belongs to a symbol instance
    int instanceId = 0;          // > 0 when the object belongs to a placed instance
    int patternId = 0;           // > 0 when the object is filled with a pattern
    PatternSettings pattern;     // baked repeat settings for that fill
    QVector<PatternShape> patternShapes; // baked tile artwork
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

    QVector<Effect> effects;    // non-destructive effect stack, applied in order

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

    // Dimension objects. Points are kept in page coordinates and can be
    // re-resolved from the referenced objects whenever their geometry changes.
    int dimensionType = 0;   // 0 horizontal, 1 vertical, 2 aligned, 3 angular,
                             // 4 radius, 5 diameter
    QPointF dimA;            // first measured point / angular vertex
    QPointF dimB;            // second measured point / first ray point
    QPointF dimC;            // second ray point for angular dimensions
    qreal dimOffset = 24.0;  // distance of the dimension line from the geometry
    int dimUnits = 0;        // 0 mm, 1 cm, 2 inches, 3 px
    int dimPrecision = 1;    // digits after the decimal separator
    qreal dimScale = 1.0;    // drawing scale factor (1:N stores N)
    bool dimAssociative = true;
    int dimRefA = 0;
    int dimRefB = 0;
    int dimRefC = 0;
    QPointF dimAnchorA = QPointF(-1.0, -1.0);
    QPointF dimAnchorB = QPointF(-1.0, -1.0);
    QPointF dimAnchorC = QPointF(-1.0, -1.0);

    // Machine-readable code object (QR, linear barcode or Data Matrix).
    int codeKind = 0;             // CodeKind index
    QString codeData;
    int codeEcc = 1;              // QR error correction (0 L .. 3 H)
    int codeMargin = 4;           // quiet zone, modules
    QColor codeColor = QColor(0, 0, 0);
    QColor codeBackground = QColor(255, 255, 255);
    bool codeShowText = true;
    int codeModulesX = 0;
    int codeModulesY = 0;
    QVector<bool> codeCells;
    QString codeHumanText;
    QImage codeLogo;              // optional image in the centre of a QR code
    qreal codeLogoScale = 0.22;   // logo side as a fraction of the QR side

    QRectF localBounds() const;
};

// Bounds in local coordinates including the area occupied by the effect stack.
QRectF effectBounds(const VectorObject &object);

// Clickable outline of a dimension (lines and leader), so the object does not
// block picking underneath its large selection frame.
QPainterPath dimensionOutline(const VectorObject &object);

struct VectorGroup {
    int id = 0;
    int parent = 0;

    bool operator==(const VectorGroup &other) const
    {
        return id == other.id && parent == other.parent;
    }
};

// A reusable symbol stored inside the document. Instances are ordinary objects
// tagged with symbolId/instanceId, so they keep every existing editing tool.
struct SymbolDefinition {
    int id = 0;
    QString name;
    QRectF bounds;                 // coordinate box the objects were captured in
    QVector<VectorObject> objects; // artwork in that box
};

// A pattern stored in the document. Objects are captured in `bounds`; the
// baked tile artwork is generated into each filled object when applied.
struct PatternDefinition {
    int id = 0;
    QString name;
    QRectF bounds;
    QVector<VectorObject> objects;
    PatternSettings settings;
};

// A placed copy of a symbol. `bounds` is the instance box in document
// coordinates; instances scale independently of the definition.
struct SymbolInstance {
    int id = 0;
    int symbolId = 0;
    QRectF bounds;
    qreal rotation = 0.0;
    int page = -9999;
    int group = 0;
};

class Document
{
public:
    QSizeF pageSize = QSizeF(794.0, 1123.0); // current page, A4 at 96 dpi
    qreal drawingScale = 1.0;               // 1:N engineering drawing scale
    QVector<PageInfo> pages;
    QVector<MasterPage> masters;
    int currentPage = 0;
    QStringList layers = {QStringLiteral("Слой 1")};
    QVector<bool> layerVisible = {true};
    QVector<bool> layerLocked = {false};
    QVector<VectorGroup> groups;
    QVector<BlendRecord> blends;
    QVector<SymbolDefinition> symbols;
    QVector<SymbolInstance> instances;
    QVector<PatternDefinition> patterns;
    QVector<qreal> guidesX;
    QVector<qreal> guidesY;
    QVector<VectorObject> objects;

    void clear();

    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &json, QString *error);

    bool save(const QString &path, QString *error) const;
    bool load(const QString &path, QString *error);

    QString toSvg(bool withBackground = true) const;
    void render(QPainter *painter) const;

    static void paintObject(QPainter *painter, const VectorObject &object);
    static Document sample();
};

QColor interpolateColor(const QColor &from, const QColor &to, qreal t);

// Serialisation helpers shared with the pattern library and the asset modules.
QJsonObject vectorObjectToJson(const VectorObject &object);
VectorObject vectorObjectFromJson(const QJsonObject &entry);
QJsonArray encodePathData(const QPainterPath &path);
QPainterPath decodePathData(const QJsonArray &array);

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
