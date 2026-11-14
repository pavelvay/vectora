#pragma once

#include <QColor>
#include <QGraphicsView>
#include <QVector>

#include "Document.h"
#include "Tools.h"

class VectorItem;
class QTextEdit;
class QShortcut;

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    enum class BezierNodeMode { Cusp, Smooth, Symmetric };
    enum class BooleanOperation { Union, Difference, Intersection, Exclude, Divide, Trim };

    explicit CanvasView(QWidget *parent = nullptr);
    ~CanvasView() override;

    qreal zoom() const { return m_zoom; }
    QRectF pageRect() const;
    QSizeF pageSize() const { return m_pageSize; }
    bool showGrid() const { return m_grid; }

    QColor fillColor() const { return m_fill; }
    QColor strokeColor() const { return m_stroke; }
    qreal strokeWidth() const { return m_strokeWidth; }

    void setZoom(qreal zoom);
    void zoomAt(qreal zoom, const QPoint &viewportPos);
    void zoomStep(qreal factor);
    void fitPage();
    void actualSize();
    void zoomToSelection();
    void setShowGrid(bool on);
    void setShowPrintableArea(bool on);
    void setActiveTool(Tool tool);

    void setPageSize(const QSizeF &size);
    void setFillColor(const QColor &color);
    void setStrokeColor(const QColor &color);
    void setStrokeWidth(qreal width);

    QList<VectorItem *> vectorItems() const;
    QList<VectorItem *> selectedVectorItems() const;
    VectorItem *addVectorObject(const VectorObject &object, bool record = true,
                                bool select = true);
    void setDocument(const Document &document);
    QVector<PageInfo> pageList() const { return m_pages; }
    QVector<MasterPage> masterList() const { return m_masters; }
    int currentPage() const { return m_currentPage; }
    QString pageLabel(int index) const;
    void setCurrentPage(int index);
    void setPageList(const QVector<PageInfo> &pages,
                     const QVector<MasterPage> &masters, int currentPage);
    int addPage(const QString &name = QString());
    int duplicatePage(int index);
    void removePage(int index);
    void movePage(int from, int to);
    void renamePage(int index, const QString &name);
    void setPageInfo(int index, const PageInfo &info);
    int addMaster(const QString &name = QString());
    void renameMaster(int index, const QString &name);
    void setMasterInfo(int index, const MasterPage &master);
    void applyMasterToPage(int pageIndex, int masterIndex);
    int createMasterFromPage(int pageIndex, const QString &name);
    void applyPageVisibility();
    QImage renderPageImage(int index, const QSize &size) const;
    QVector<VectorObject> objects() const;

    void deleteSelectedObjects();
    void selectAllObjects();
    void editTextItem(VectorItem *item);

    void blendSelected(int steps = 7);
    QVector<Effect> selectionEffects() const;
    void setSelectionEffects(const QVector<Effect> &effects);
    void addEffectToSelection(const Effect &effect);
    void removeEffectFromSelection(int index);
    void moveEffectInSelection(int index, int delta);
    void applyMaskFromSelection(const QString &type);
    QVector<BlendRecord> blends() const { return m_blends; }
    QVector<SymbolDefinition> symbols() const { return m_symbols; }
    QVector<PatternDefinition> patterns() const { return m_patterns; }
    int createPatternFromSelection(const QString &name);
    int storePatternDefinition(const PatternDefinition &pattern);
    bool applyPatternToSelection(int patternId);
    bool applyPatternDefinitionToSelection(const PatternDefinition &pattern);
    void updatePatternDefinition(const PatternDefinition &pattern);
    bool updatePatternFromSelection(int patternId);
    int expandSelectedPatterns();
    void removePattern(int patternId);
    int selectedPatternId() const;
    PatternDefinition patternDefinition(int patternId) const;
    QVector<SymbolInstance> instances() const { return m_instances; }
    int createSymbolFromSelection(const QString &name);
    int insertSymbol(int symbolId, const QPointF &topLeft = QPointF());
    bool updateSymbolFromSelection(int symbolId = 0);
    void detachSelectedSymbolInstances();
    void renameSymbol(int symbolId, const QString &name);
    void deleteSymbol(int symbolId);
    void rebuildSymbolInstance(int instanceId);
    int selectedSymbolId() const;
    int selectedInstanceId() const;
    QImage renderSymbolImage(int symbolId, const QSize &size) const;
    QStringList symbolNames() const;
    QString selectionToSvg() const;
    int insertSvgAsset(const QString &svg, const QPointF &centre);
    VectorItem *addCodeObject(const VectorObject &object);
    void updateCodeItem(VectorItem *item, const VectorObject &object);
    void renameItem(VectorItem *item, const QString &name);
    void focusItem(VectorItem *item);
    void setBlends(const QVector<BlendRecord> &blends);
    void setBlendSteps(int blendId, int steps);
    void setBlendSpacing(int blendId, qreal spacing);
    void updateBlend(int blendId);
    void updateAllBlends();
    void removeBlend(int blendId);
    int blendIdFor(VectorItem *item) const;

    enum class DimensionMode { Horizontal, Vertical, Aligned, Angular, Radius, Diameter };
    void setDimensionMode(DimensionMode mode);
    DimensionMode dimensionMode() const { return m_dimensionMode; }
    void setDimensionUnits(int units);
    int dimensionUnits() const { return m_dimensionUnits; }
    void setDimensionPrecision(int digits);
    int dimensionPrecision() const { return m_dimensionPrecision; }
    void setDimensionOffset(qreal offset);
    qreal dimensionOffset() const { return m_dimensionOffset; }
    void setDimensionAssociative(bool on);
    bool dimensionAssociative() const { return m_dimensionAssociative; }
    void applyDimensionSettingsToSelection();
    void updateAllDimensions();
    QString measurementText(qreal pixels) const;

    // Exact construction commands.
    void constructLine(const QPointF &a, const QPointF &b);
    void constructLineFrom(const QPointF &start, qreal length, qreal angleDegrees);
    bool createParallelFromSelection(qreal offset);
    bool createPerpendicularFromSelection(qreal length);
    void constructCircle(const QPointF &center, qreal radius);
    void constructArc(const QPointF &center, qreal radius,
                      qreal startDegrees, qreal sweepDegrees);
    void constructPolygon(const QPointF &center, int sides, qreal radius);
    bool createTangentFromSelection(const QPointF &externalPoint);
    void scaleSelectionBy(qreal factor);

    void raiseSelection();
    void lowerSelection();

    QStringList layers() const { return m_layers; }
    QVector<VectorGroup> groups() const { return m_groups; }
    QVector<qreal> guidesX() const { return m_guidesX; }
    QVector<qreal> guidesY() const { return m_guidesY; }
    void beginGuideDrag(Qt::Orientation orientation);
    void updateGuideDrag(Qt::Orientation orientation, qreal scenePos);
    void endGuideDrag(Qt::Orientation orientation, qreal scenePos);
    void cancelGuideDrag();
    void clearGuides();
    void setGuidesVisible(bool visible);
    bool snapEnabled() const { return m_snapEnabled; }
    void setSnapEnabled(bool enabled);
    int gridStep() const { return m_gridStep; }
    void setGridStep(int step);
    qreal snapThreshold() const { return m_snapThreshold; }
    void setSnapThreshold(qreal threshold);
    int gridType() const { return m_gridType; }
    void setGridType(int type);
    qreal gridAngle() const { return m_gridAngle; }
    void setGridAngle(qreal degrees);
    QPointF gridOrigin() const { return m_gridOrigin; }
    void setGridOrigin(const QPointF &origin);
    bool snapOption(const QString &key) const;
    void setSnapOption(const QString &key, bool on);
    qreal drawingScale() const { return m_drawingScale; }
    void setDrawingScale(qreal scale);
    void setSmartGuides(const QVector<QLineF> &lines);
    QPointF snapNodePoint(const QPointF &scenePos, VectorItem *exclude) const;
    int activeLayer() const { return m_activeLayer; }
    void setActiveLayer(int index);
    int addLayer(const QString &name = QString());
    int addGroupRecord(int parentGroup);
    void removeLayer(int index);
    void renameLayer(int index, const QString &name);
    void moveLayer(int from, int to);
    void moveItemToLayer(VectorItem *item, int layer, int positionFromTop);
    void moveItemsToLayer(const QList<VectorItem *> &items, int layer);
    QList<VectorItem *> itemsInLayer(int layer) const;
    int layerOf(VectorItem *item) const;
    bool layerVisible(int index) const;
    bool layerLocked(int index) const;
    void setLayerVisible(int index, bool visible);
    void setLayerLocked(int index, bool locked);

    qreal cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(qreal radius) { m_cornerRadius = qBound(0.0, radius, 1000.0); }
    int polygonSides() const { return m_polygonSides; }
    void setPolygonSides(int sides) { m_polygonSides = qBound(3, sides, 64); }
    int starPoints() const { return m_starPoints; }
    void setStarPoints(int points) { m_starPoints = qBound(3, points, 50); }
    qreal starRatio() const { return m_starRatio; }
    void setStarRatio(qreal ratio) { m_starRatio = qBound(0.05, ratio, 0.95); }
    qreal spiralTurns() const { return m_spiralTurns; }
    void setSpiralTurns(qreal turns) { m_spiralTurns = qBound(0.5, turns, 20.0); }

    void bringToFront();
    void sendToBack();
    void bringForward();
    void sendBackward();
    void flipSelection(bool horizontal);
    void rotateSelection(qreal degrees);
    void setItemLocked(VectorItem *item, bool locked);
    void syncClones();
    void scaleSelectionTo(const QRectF &targetBounds, qreal opacity = -1.0);
    void applyBooleanOperation(BooleanOperation operation);
    void applyKnifeCut(const QLineF &cut);
    void applyEraserStroke(const QVector<QPointF> &points);
    void convertSelectionToCurves();
    void simplifySelectedPath();
    void convertStrokeToPath();
    void applyTextOnPath();
    void applyTextInShape();
    void detachTextSpecial();
    void applyContour(int steps, qreal distance, int direction,
                      const QColor &color, bool filled);
    void resetEnvelope();
    bool envelopeActive() const { return m_envelopeItem != nullptr; }
    bool hasSelectedNodes() const { return !m_selectedNodeElements.isEmpty(); }
    int selectedNodeElement() const { return m_nodeElement; }
    void addNodeAtSelectedSegment();
    void deleteSelectedNodes();
    void closeSelectedPath();
    void openSelectedPath();
    void joinSelectedEndpoints();
    void breakAtSelectedNode();
    void setSelectedNodeType(int type);
    void convertTextToCurves();
    void alignSelectedNodes(int mode);
    void duplicateSelection(int count, const QPointF &offset);
    void cloneSelection(const QPointF &offset = QPointF(24.0, 24.0));
    void skewSelection(qreal angleX, qreal angleY);

    void groupSelection();
    void ungroupSelection();
    void ungroupAll();
    int topGroupOf(int group) const;
    QList<VectorItem *> itemsInGroup(int topGroup) const;
    QList<int> childGroupsOf(int parentGroup) const;
    QList<VectorItem *> itemsDirectlyInGroup(int group) const;
    void selectGroupItems(int group);

    void finishBezier();
    void closeBezier();
    void deleteLastBezierNode();
    void setBezierNodeMode(BezierNodeMode mode);
    BezierNodeMode bezierNodeMode() const { return m_bezierNodeMode; }
    void setBezierAutoClose(bool on) { m_bezierAutoClose = on; }
    bool bezierAutoClose() const { return m_bezierAutoClose; }

signals:

signals:
    void scenePosChanged(const QPointF &pos);
    void zoomChanged(qreal zoom);
    void statusHint(const QString &text);
    void beginEdit();
    void pageChanged(int index);
    void endEdit();
    void selectionChanged(int count);
    void colorPicked(const QColor &color);
    void contextMenuRequested(const QPoint &globalPos, VectorItem *item);
    void symbolsChanged();
    void codeEditRequested(VectorItem *item);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class DrawMode {
        None,
        Rectangle,
        Ellipse,
        Polygon,
        Star,
        Spiral,
        Line,
        Freehand,
        Artistic,
        Crop,
        Transparency
    };

    struct BezierNode {
        QPointF anchor;
        QPointF in;
        QPointF out;
        BezierNodeMode mode = BezierNodeMode::Symmetric;
    };

    void updateCursor();
    void cycleSelection();
    void finishTextEditing(bool cancel);
    void normalizeZOrder();
    void updateSnapSettings();
    void applyLayerState(int layer);
    void applyLayerLayout(const QVector<QList<VectorItem *>> &perLayer);
    VectorItem *itemAtScene(const QPointF &scenePos) const;
    void finishDrawing(const QPointF &scenePos);
    void startDrawing(DrawMode mode, const QPointF &scenePos);

    void createTextAt(const QPointF &scenePos);
    void applyFillAt(const QPointF &scenePos);
    void applyOutlineAt(const QPointF &scenePos);
    void toggleShadowAt(const QPointF &scenePos);
    bool shadowPress(const QPointF &scenePos);
    void shadowMove(const QPointF &scenePos);
    void shadowRelease();
    void cropTo(const QRectF &rect);

    // Interactive perspective / extrusion tools.
    bool perspectivePress(const QPointF &scenePos);
    void perspectiveMove(const QPointF &scenePos);
    void perspectiveRelease();
    void drawPerspectiveOverlay(QPainter *painter);
    bool extrudePress(const QPointF &scenePos);
    void extrudeMove(const QPointF &scenePos);
    void extrudeRelease();
    QPolygonF perspectiveQuad(const VectorObject &object) const;
    QPointF perspectiveCornerScene(VectorItem *item, int corner) const;
    int perspectiveEffectIndex(VectorItem *item) const;

    void addBezierNode(const QPointF &scenePos);
    void cancelBezier();
    QPainterPath buildBezierPath() const;
    QPainterPath buildFreehandPath(const QVector<QPointF> &points) const;

    void updateBlendsFor(VectorItem *item);
    void updateDimensionsFor(VectorItem *item);
    bool resolveDimension(VectorObject &object) const;
    void dimensionPress(const QPointF &scenePos);
    void drawDimensionPreview(QPainter *painter);
    void drawSnapHint(QPainter *painter);
    QPointF gridSnapPoint(const QPointF &scenePos) const;
    QPointF anchorScenePoint(VectorItem *item, const QPointF &anchor) const;
    VectorItem *addConstructedPath(const QPainterPath &path, bool closed);
    bool nodePress(const QPointF &scenePos);
    void insertNodeAt(const QPointF &scenePos, bool nearestSegment);
    void drawNodeOverlay(QPainter *painter);
    void drawPreview(QPainter *painter);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:

    qreal m_zoom = 1.0;
    QSizeF m_pageSize = QSizeF(794.0, 1123.0);
    QVector<PageInfo> m_pages;
    QVector<MasterPage> m_masters;
    int m_currentPage = 0;
    bool m_grid = false;
    bool m_showPrintableArea = false;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_panAnchor;
    Tool m_tool = Tool::Select;

    QColor m_fill = QColor("#4f7cc0");
    QColor m_stroke = QColor("#1d2127");
    qreal m_strokeWidth = 2.0;

    DrawMode m_drawMode = DrawMode::None;
    bool m_drawing = false;
    QPointF m_drawStart;
    QPointF m_drawCurrent;
    QVector<QPointF> m_freehandPoints;

    bool m_bezierActive = false;
    bool m_bezierDragging = false;
    bool m_bezierDragMoved = false;
    bool m_bezierClosed = false;
    bool m_bezierAutoClose = false;
    bool m_bezierHasCursor = false;
    BezierNodeMode m_bezierNodeMode = BezierNodeMode::Symmetric;
    QPointF m_bezierCursor;
    QVector<BezierNode> m_bezierNodes;

    bool m_nodeDragging = false;
    VectorItem *m_nodeItem = nullptr;
    int m_nodeElement = -1;
    QSet<int> m_selectedNodeElements;
    QPointF m_nodeLastScene;

    bool m_transparencyDragging = false;
    VectorItem *m_transparencyItem = nullptr;
    qreal m_transparencyStartValue = 1.0;
    qreal m_transparencyStartX = 0.0;

    VectorItem *m_perspectiveItem = nullptr;
    int m_perspectiveCorner = -1;
    QPointF m_perspectiveLastScene;

    bool m_shadowDragging = false;
    VectorItem *m_shadowItem = nullptr;
    QPointF m_shadowStart;
    QPointF m_shadowStartOffset;

    bool m_extrudeDragging = false;
    VectorItem *m_extrudeItem = nullptr;
    int m_extrudeEffect = -1;
    QPointF m_extrudeStart;

    qreal m_nextZ = 1.0;
    QTextEdit *m_textEditor = nullptr;
    VectorItem *m_textItem = nullptr;
    QString m_textOriginal;
    QShortcut *m_tabShortcut = nullptr;

    QStringList m_layers = {QStringLiteral("Слой 1")};
    QVector<bool> m_layerVisible = {true};
    QVector<bool> m_layerLocked = {false};
    int m_activeLayer = 0;

    qreal m_cornerRadius = 0.0;
    int m_polygonSides = 6;
    int m_starPoints = 5;
    qreal m_starRatio = 0.45;
    qreal m_spiralTurns = 3.0;

    QVector<VectorGroup> m_groups;
    int m_nextGroupId = 1;
    int m_nextObjectId = 1;
    bool m_syncingSelection = false;
    QPointF m_groupPivot;
    bool m_groupPivotValid = false;
    bool m_groupPivotCustom = false;
    bool m_groupPivotDragging = false;
    bool m_insideGroup = false;
    int m_lastSelectionCount = 0;

    QVector<qreal> m_guidesX;
    QVector<qreal> m_guidesY;
    bool m_snapEnabled = true;
    int m_gridStep = 20;
    qreal m_snapThreshold = 6.0;
    QVector<QLineF> m_smartGuides;
    int m_gridType = 0;              // 0 cartesian, 1 polar, 2 isometric
    qreal m_gridAngle = 15.0;        // polar angular step, degrees
    QPointF m_gridOrigin;
    qreal m_drawingScale = 1.0;      // 1:N drawing scale
    bool m_snapGrid = true;
    bool m_snapGuides = true;
    bool m_snapObjects = true;
    bool m_snapNodes = true;
    bool m_snapCenters = true;
    bool m_snapMidpoints = true;
    bool m_snapIntersections = true;
    bool m_snapEdges = true;
    bool m_snapTangents = true;
    QPointF m_snapPoint;
    QString m_snapKind;
    bool m_snapValid = false;

    DimensionMode m_dimensionMode = DimensionMode::Horizontal;
    QPointF m_dimensionCursor;
    bool m_dimensionHasCursor = false;
    QVector<QPointF> m_dimensionPoints;
    QVector<int> m_dimensionRefs;
    QVector<QPointF> m_dimensionAnchors;
    int m_dimensionUnits = 0;
    int m_dimensionPrecision = 1;
    qreal m_dimensionOffset = 24.0;
    bool m_dimensionAssociative = true;
    bool m_updatingDimensions = false;
    QVector<QPainterPath> regionize(const QVector<QPainterPath> &sources) const;
    bool shapeBuilderPress(const QPointF &scenePos);
    void shapeBuilderMove(const QPointF &scenePos);
    void shapeBuilderRelease(const QPointF &scenePos, bool deleteRegions);
    void drawShapeBuilderOverlay(QPainter *painter);

    bool envelopePress(const QPointF &scenePos);
    void envelopeMove(const QPointF &scenePos);
    void remapEnvelope();
    void drawEnvelopeOverlay(QPainter *painter);

    bool m_shapeDragging = false;
    bool m_shapeHasRegions = false;
    QPointF m_shapeStart;
    QPointF m_shapeCurrent;
    QVector<QPainterPath> m_shapeRegions;
    QVector<VectorObject> m_shapeStyles;
    QList<VectorItem *> m_shapeSources;

    VectorItem *m_envelopeItem = nullptr;
    QVector<QPainterPath::Element> m_envelopeOriginal;
    QVector<QPointF> m_envelopeGrid;
    QRectF m_envelopeBounds;
    int m_envelopeDragIndex = -1;
    bool m_envelopeEditing = false;

    bool m_knifeActive = false;
    QPointF m_knifeStart;
    QPointF m_knifeCurrent;
    bool m_eraserActive = false;
    QVector<QPointF> m_eraserPoints;

    QVector<BlendRecord> m_blends;
    int m_nextBlendId = 1;

    QVector<SymbolDefinition> m_symbols;
    QVector<SymbolInstance> m_instances;
    int m_nextSymbolId = 1;
    int m_nextInstanceId = 1;

    QVector<PatternDefinition> m_patterns;
    int m_nextPatternId = 1;
    bool m_updatingBlends = false;

    bool m_guidePreview = false;
    bool m_guidesVisible = true;
    Qt::Orientation m_guideOrientation = Qt::Horizontal;
    qreal m_guidePosition = 0.0;
};
