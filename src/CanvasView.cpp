#include "CanvasView.h"

#include <QColorDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>
#include <QContextMenuEvent>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QShortcut>
#include <QTextCursor>
#include <QTextEdit>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QtMath>
#include <QSet>

#include <algorithm>

#include "Patterns.h"
#include "SvgImporter.h"
#include "VectorItem.h"

namespace {
constexpr qreal kMargin = 400.0;
constexpr qreal kMinZoom = 0.02;
constexpr qreal kMaxZoom = 32.0;
constexpr qreal kPxPerMm = 3.7795275591;
constexpr qreal kPi = 3.14159265358979323846;

bool isCurveEndpoint(const QVector<QPainterPath::Element> &elements, int index)
{
    return index >= 2
           && elements.at(index).type == QPainterPath::CurveToDataElement
           && elements.at(index - 2).type == QPainterPath::CurveToElement;
}

bool isAnchorElement(const QVector<QPainterPath::Element> &elements, int index)
{
    if (index < 0 || index >= elements.size())
        return false;
    const auto type = elements.at(index).type;
    return type == QPainterPath::MoveToElement || type == QPainterPath::LineToElement
           || isCurveEndpoint(elements, index);
}

int anchorForElement(const QVector<QPainterPath::Element> &elements, int index)
{
    if (index < 0 || index >= elements.size())
        return -1;
    if (isAnchorElement(elements, index))
        return index;
    if (elements.at(index).type == QPainterPath::CurveToElement)
        return index + 2 < elements.size() ? index + 2 : -1;
    if (elements.at(index).type == QPainterPath::CurveToDataElement) {
        if (isCurveEndpoint(elements, index))
            return index;
        if (index + 1 < elements.size() && isCurveEndpoint(elements, index + 1))
            return index + 1;
    }
    return -1;
}

QPointF bezierPoint(const QPointF &p0, const QPointF &c1, const QPointF &c2,
                    const QPointF &p3, qreal t)
{
    const qreal u = 1.0 - t;
    return p0 * (u * u * u) + c1 * (3.0 * u * u * t)
           + c2 * (3.0 * u * t * t) + p3 * (t * t * t);
}

QPen previewPen()
{
    QPen pen(QColor(0x7f, 0xae, 0xf7));
    pen.setWidthF(1.4);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    return pen;
}
} // namespace

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
{
    setAcceptDrops(true);
    if (viewport())
        viewport()->setAcceptDrops(true);
    auto *graphicsScene = new QGraphicsScene(this);
    setScene(graphicsScene);
    setSceneRect(-kMargin, -kMargin, m_pageSize.width() + 2 * kMargin,
                 m_pageSize.height() + 2 * kMargin);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::RubberBandDrag);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(0x17, 0x18, 0x1b));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Dimensions follow the geometry they were attached to; update them before
    // the history snapshot is taken so undo/redo stores the final state.
    connect(this, &CanvasView::endEdit, this, [this]() { updateAllDimensions(); });

    connect(graphicsScene, &QGraphicsScene::selectionChanged, this, [this]() {
        if (selectedVectorItems().isEmpty()) {
            m_insideGroup = false;
            m_selectedNodeElements.clear();
            m_nodeItem = nullptr;
            m_nodeElement = -1;
        }
        if (!m_syncingSelection && !m_insideGroup) {
            QSet<int> roots;
            for (VectorItem *item : selectedVectorItems()) {
                if (item->groupIndex() > 0)
                    roots.insert(topGroupOf(item->groupIndex()));
            }
            if (!roots.isEmpty()) {
                m_syncingSelection = true;
                for (VectorItem *item : vectorItems()) {
                    if (item->groupIndex() > 0
                        && roots.contains(topGroupOf(item->groupIndex())))
                        item->setSelected(true);
                }
                m_syncingSelection = false;
            }
        }
        const QList<VectorItem *> selected = selectedVectorItems();
        const int count = selected.size();
        if (count >= 2) {
            if (count != m_lastSelectionCount || !m_groupPivotValid) {
                QRectF bounds;
                for (VectorItem *item : selected) {
                    const QRectF visual = item->mapRectToScene(item->localBounds());
                    bounds = bounds.isNull() ? visual : bounds.united(visual);
                }
                if (!bounds.isNull()) {
                    m_groupPivot = bounds.center();
                    m_groupPivotValid = true;
                    m_groupPivotCustom = false;
                }
            }
        } else {
            m_groupPivotValid = false;
            m_groupPivotCustom = false;
        }
        m_lastSelectionCount = count;
        emit selectionChanged(count);
    });

    m_tabShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    m_tabShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_tabShortcut, &QShortcut::activated, this, &CanvasView::cycleSelection);

    updateCursor();
}

CanvasView::~CanvasView()
{
    if (QGraphicsScene *current = scene())
        disconnect(current, nullptr, this, nullptr);
}

QRectF CanvasView::pageRect() const
{
    return QRectF(0.0, 0.0, m_pageSize.width(), m_pageSize.height());
}

void CanvasView::setPageSize(const QSizeF &size)
{
    if (!size.isValid() || size.isEmpty())
        return;
    m_pageSize = size;
    setSceneRect(-kMargin, -kMargin, size.width() + 2 * kMargin,
                 size.height() + 2 * kMargin);
    if (scene())
        scene()->setProperty("pageRect", QRectF(0, 0, size.width(), size.height()));
    viewport()->update();
}

void CanvasView::setFillColor(const QColor &color)
{
    m_fill = color;
}

void CanvasView::setStrokeColor(const QColor &color)
{
    m_stroke = color;
}

void CanvasView::setStrokeWidth(qreal width)
{
    m_strokeWidth = qBound(0.0, width, 100.0);
}

void CanvasView::setZoom(qreal zoom)
{
    const QPoint center = viewport()->rect().center();
    zoomAt(zoom, center);
}

void CanvasView::zoomAt(qreal zoom, const QPoint &viewportPos)
{
    zoom = qBound(kMinZoom, zoom, kMaxZoom);
    if (qFuzzyCompare(zoom, m_zoom))
        return;

    const QPointF before = mapToScene(viewportPos);
    m_zoom = zoom;
    setTransform(QTransform::fromScale(m_zoom, m_zoom));
    const QPointF after = mapToScene(viewportPos);
    const QPointF delta = after - before;

    // Move the scrollbars so the scene point under the cursor stays in place.
    // The sign is opposite to the raw scene delta; this is what makes Ctrl+wheel
    // zoom towards the mouse instead of away from it.
    horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                    - qRound(delta.x() * m_zoom));
    verticalScrollBar()->setValue(verticalScrollBar()->value()
                                  - qRound(delta.y() * m_zoom));

    emit zoomChanged(m_zoom);
    viewport()->update();
}

void CanvasView::zoomStep(qreal factor)
{
    setZoom(m_zoom * factor);
}

void CanvasView::fitPage()
{
    const QRectF page = pageRect().adjusted(-50, -50, 50, 50);
    const QSize viewportSize = viewport()->size();
    if (page.isEmpty() || viewportSize.isEmpty())
        return;

    const qreal scale = qMin(viewportSize.width() / page.width(),
                             viewportSize.height() / page.height());
    setZoom(scale);
    centerOn(page.center());
}

void CanvasView::actualSize()
{
    setZoom(1.0);
    centerOn(pageRect().center());
}

void CanvasView::zoomToSelection()
{
    const QList<VectorItem *> items = selectedVectorItems();
    if (items.isEmpty()) {
        emit statusHint(QStringLiteral("Сначала выделите объекты."));
        return;
    }
    QRectF bounds;
    for (VectorItem *item : items)
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    bounds = bounds.adjusted(-40, -40, 40, 40);
    const qreal scale = qMin(viewport()->width() / bounds.width(),
                             viewport()->height() / bounds.height());
    setZoom(scale);
    centerOn(bounds.center());
}

void CanvasView::setShowGrid(bool on)
{
    m_grid = on;
    viewport()->update();
}

void CanvasView::setShowPrintableArea(bool on)
{
    m_showPrintableArea = on;
    viewport()->update();
}

void CanvasView::setActiveTool(Tool tool)
{
    if (tool != Tool::Node) {
        m_selectedNodeElements.clear();
        m_nodeItem = nullptr;
        m_nodeElement = -1;
    }
    if (m_bezierActive && tool != Tool::Bezier)
        finishBezier();
    m_drawing = false;
    m_drawMode = DrawMode::None;
    m_freehandPoints.clear();
    m_knifeActive = false;
    m_eraserActive = false;
    m_eraserPoints.clear();
    m_shapeDragging = false;
    m_shapeHasRegions = false;
    m_shapeRegions.clear();
    m_shapeStyles.clear();
    m_shapeSources.clear();
    if (tool != Tool::Envelope) {
        m_envelopeItem = nullptr;
        m_envelopeGrid.clear();
        m_envelopeOriginal.clear();
        m_envelopeDragIndex = -1;
        m_envelopeEditing = false;
    }
    if (tool != Tool::Perspective) {
        m_perspectiveItem = nullptr;
        m_perspectiveCorner = -1;
    }
    if (tool != Tool::Extrude) {
        m_extrudeDragging = false;
        m_extrudeItem = nullptr;
        m_extrudeEffect = -1;
    }
    if (tool != Tool::Shadow) {
        m_shadowDragging = false;
        m_shadowItem = nullptr;
    }
    if (tool != Tool::Dimension) {
        m_dimensionPoints.clear();
        m_dimensionRefs.clear();
        m_dimensionAnchors.clear();
        m_dimensionHasCursor = false;
    }

    m_tool = tool;
    setDragMode(tool == Tool::Select ? QGraphicsView::RubberBandDrag
                                     : QGraphicsView::NoDrag);
    updateCursor();
    viewport()->update();
}

void CanvasView::cycleSelection()
{
    QList<VectorItem *> items;
    for (VectorItem *item : vectorItems()) {
        if (item->isVisible()
            && (item->flags() & QGraphicsItem::ItemIsSelectable))
            items.append(item);
    }
    if (items.isEmpty()) {
        emit statusHint(QStringLiteral("На холсте нет доступных объектов."));
        return;
    }

    std::reverse(items.begin(), items.end());
    const QList<VectorItem *> selected = selectedVectorItems();
    VectorItem *current = selected.isEmpty() ? nullptr : selected.last();
    const int index = items.indexOf(current);
    VectorItem *next = items.value((index + 1) % items.size());

    scene()->clearSelection();
    if (next)
        next->setSelected(true);
    emit statusHint(QStringLiteral("Tab: следующий объект (%1 из %2)")
                        .arg((index + 1) % items.size() + 1)
                        .arg(items.size()));
}

void CanvasView::updateCursor()
{
    if (m_panning || m_spaceHeld) {
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }

    switch (m_tool) {
    case Tool::Pan:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;
    case Tool::Text:
        viewport()->setCursor(Qt::IBeamCursor);
        break;
    case Tool::Select:
    case Tool::Node:
        viewport()->setCursor(Qt::ArrowCursor);
        break;
    default:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    }
}

void CanvasView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(rect, QColor(0x17, 0x18, 0x1b));

    const QRectF page = pageRect();

    for (int i = 7; i >= 1; --i) {
        const QRectF shadow = page.adjusted(-i * 1.5, -i * 1.5 + 4.0, i * 1.5,
                                            i * 1.5 + 4.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 7));
        painter->drawRoundedRect(shadow, 3.0, 3.0);
    }

    painter->setPen(Qt::NoPen);
    const QColor pageColor =
        (m_currentPage >= 0 && m_currentPage < m_pages.size())
            ? m_pages.at(m_currentPage).background
            : QColor(Qt::white);
    painter->setBrush(pageColor);
    painter->drawRect(page);

    // Master page artwork is drawn behind the editable objects of the page.
    if (m_currentPage >= 0 && m_currentPage < m_pages.size()) {
        const int master = m_pages.at(m_currentPage).master;
        if (master >= 0 && master < m_masters.size()) {
            for (VectorItem *item : vectorItems()) {
                const VectorObject object = item->object();
                if (object.page != -1 - master)
                    continue;
                const QRectF bounds = object.localBounds();
                painter->save();
                painter->translate(object.pos);
                painter->translate(bounds.center());
                painter->rotate(object.rotation);
                painter->translate(-bounds.center());
                Document::paintObject(painter, object);
                painter->restore();
            }
        }
    }

    if (m_currentPage >= 0 && m_currentPage < m_pages.size()
        && (m_pages.at(m_currentPage).bleed > 0.0
            || m_pages.at(m_currentPage).margin > 0.0)) {
        const PageInfo &info = m_pages.at(m_currentPage);
        if (info.bleed > 0.0) {
            QPen bleedPen(QColor(0xff, 0x66, 0x99, 90), 1.0);
            bleedPen.setStyle(Qt::DashLine);
            painter->setPen(bleedPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(page.adjusted(-info.bleed, -info.bleed,
                                            info.bleed, info.bleed));
        }
        if (info.margin > 0.0) {
            QPen marginPen(QColor(0x4f, 0x7c, 0xc0, 90), 1.0);
            marginPen.setStyle(Qt::DotLine);
            painter->setPen(marginPen);
            painter->drawRect(page.adjusted(info.margin, info.margin,
                                            -info.margin, -info.margin));
        }
    }

    if (m_grid) {
        painter->save();
        painter->setClipRect(page);
        painter->setPen(QPen(QColor(0x4f, 0x7c, 0xc0, 26), 1.0));
        const qreal step = qMax<qreal>(2.0, m_gridStep);

        if (m_gridType == 1) {
            // Polar grid: concentric circles and rays from the origin.
            const QPointF center = page.center() + m_gridOrigin;
            const qreal maxRadius = std::hypot(page.width(), page.height());
            for (qreal radius = step; radius <= maxRadius; radius += step)
                painter->drawEllipse(center, radius, radius);
            const qreal angleStep = qMax<qreal>(1.0, m_gridAngle);
            for (qreal angle = 0.0; angle < 360.0; angle += angleStep) {
                const qreal radians = angle * kPi / 180.0;
                painter->drawLine(center,
                                  center + QPointF(std::cos(radians), std::sin(radians))
                                               * maxRadius);
            }
        } else if (m_gridType == 2) {
            // Isometric grid: two families of lines at 30 degrees.
            const QPointF center = page.center() + m_gridOrigin;
            const qreal radians = 30.0 * kPi / 180.0;
            const QPointF u(std::cos(radians), std::sin(radians));
            const QPointF v(-std::cos(radians), std::sin(radians));
            const qreal diagonal = std::hypot(page.width(), page.height());
            const int range = int(diagonal / step) + 2;
            for (int n = -range; n <= range; ++n) {
                const QPointF first = center + v * (n * step);
                painter->drawLine(first - u * diagonal, first + u * diagonal);
                const QPointF second = center + u * (n * step);
                painter->drawLine(second - v * diagonal, second + v * diagonal);
            }
        } else {
            const QPointF base = page.topLeft() + m_gridOrigin;
            const qreal startX = base.x()
                                 + std::floor((page.left() - base.x()) / step) * step;
            const qreal startY = base.y()
                                 + std::floor((page.top() - base.y()) / step) * step;
            for (qreal x = startX; x <= page.right(); x += step)
                painter->drawLine(QPointF(x, page.top()), QPointF(x, page.bottom()));
            for (qreal y = startY; y <= page.bottom(); y += step)
                painter->drawLine(QPointF(page.left(), y), QPointF(page.right(), y));
        }
        painter->restore();
    }

    if (m_showPrintableArea) {
        constexpr qreal margin = kPxPerMm * 10.0;
        QPen marginPen(QColor(0x4f, 0x7c, 0xc0, 70), 1.0);
        marginPen.setStyle(Qt::DashLine);
        painter->setPen(marginPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(page.adjusted(margin, margin, -margin, -margin));
    }

    painter->setPen(QPen(QColor(0xc8, 0xce, 0xd8), 1.0));
    painter->drawRect(page);
    painter->restore();
}

void CanvasView::drawForeground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect)
    painter->save();
    QPen guidePen(QColor(0x00, 0xb8, 0xd9), 0.0);
    guidePen.setStyle(Qt::DashLine);
    painter->setPen(guidePen);
    const QRectF area = sceneRect();
    if (m_guidesVisible) {
        for (qreal x : m_guidesX)
            painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        for (qreal y : m_guidesY)
            painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    if (m_guidePreview) {
        QPen preview(QColor(0xff, 0xb3, 0x47), 0.0);
        preview.setStyle(Qt::DashLine);
        painter->setPen(preview);
        if (m_guideOrientation == Qt::Vertical)
            painter->drawLine(QPointF(m_guidePosition, area.top()),
                              QPointF(m_guidePosition, area.bottom()));
        else
            painter->drawLine(QPointF(area.left(), m_guidePosition),
                              QPointF(area.right(), m_guidePosition));
    }
    if (!m_smartGuides.isEmpty()) {
        QPen smart(QColor(0xff, 0x66, 0x99), 0.0);
        smart.setStyle(Qt::DashLine);
        painter->setPen(smart);
        for (const QLineF &line : m_smartGuides)
            painter->drawLine(line);
    }
    painter->restore();

    drawPreview(painter);
    drawNodeOverlay(painter);

    if (m_tool == Tool::Select && m_groupPivotValid
        && selectedVectorItems().size() > 1) {
        const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(0xff, 0xb3, 0x47), 1.4));
        painter->setBrush(QColor(0x1b, 0x1d, 0x21));
        painter->drawEllipse(m_groupPivot, 6.0 / s, 6.0 / s);
        painter->drawLine(m_groupPivot + QPointF(-10.0 / s, 0),
                          m_groupPivot + QPointF(10.0 / s, 0));
        painter->drawLine(m_groupPivot + QPointF(0, -10.0 / s),
                          m_groupPivot + QPointF(0, 10.0 / s));
        painter->restore();
    }
}

void CanvasView::drawPreview(QPainter *painter)
{
    if (m_tool == Tool::Bezier && !m_bezierNodes.isEmpty()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
        const qreal size = 7.0 / s;
        const qreal handleRadius = 3.0 / s;

        painter->setPen(previewPen());
        painter->setBrush(Qt::NoBrush);
        if (m_bezierNodes.size() >= 2)
            painter->drawPath(buildBezierPath());

        // Rubber band from the last node to the cursor.
        if (m_bezierHasCursor && !m_bezierDragging) {
            QPen rubber = previewPen();
            rubber.setColor(QColor(0x7f, 0xae, 0xf7, 150));
            painter->setPen(rubber);
            painter->drawLine(m_bezierNodes.last().anchor, m_bezierCursor);
        }

        const BezierNode &last = m_bezierNodes.last();
        if (last.in != last.anchor || last.out != last.anchor) {
            painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7, 170), 1.0));
            painter->drawLine(last.anchor, last.in);
            painter->drawLine(last.anchor, last.out);
            painter->setBrush(QColor(0x9d, 0xc0, 0xf2));
            painter->setPen(QPen(QColor(0x4f, 0x7c, 0xc0), 1.0));
            painter->drawEllipse(last.in, handleRadius, handleRadius);
            painter->drawEllipse(last.out, handleRadius, handleRadius);
        }

        const qreal closeDistance = 10.0 / s;
        const bool nearFirst = m_bezierNodes.size() >= 2 && m_bezierHasCursor
                               && QLineF(m_bezierCursor, m_bezierNodes.first().anchor)
                                          .length()
                                      <= closeDistance;

        for (int i = 0; i < m_bezierNodes.size(); ++i) {
            const BezierNode &node = m_bezierNodes.at(i);
            painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7), 1.2));
            painter->setBrush(i == 0 ? QColor(0x4f, 0x7c, 0xc0)
                                     : QColor(0xffffff));
            painter->drawRect(QRectF(node.anchor - QPointF(size / 2, size / 2),
                                     QSizeF(size, size)));
        }

        if (nearFirst) {
            painter->setPen(QPen(QColor(0xff, 0xb3, 0x47), 2.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(m_bezierNodes.first().anchor, closeDistance,
                                 closeDistance);
        }

        painter->restore();
        return;
    }

    if (m_knifeActive) {
        painter->save();
        painter->setPen(previewPen());
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(m_knifeStart, m_knifeCurrent);
        painter->setBrush(QColor(0xff, 0xb3, 0x47));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(m_knifeStart, 3, 3);
        painter->drawEllipse(m_knifeCurrent, 3, 3);
        painter->restore();
    }

    if (m_eraserActive && m_eraserPoints.size() >= 2) {
        painter->save();
        QPen eraser(QColor(0xff, 0x66, 0x99, 170), 16.0);
        eraser.setCapStyle(Qt::RoundCap);
        eraser.setJoinStyle(Qt::RoundJoin);
        painter->setPen(eraser);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(buildFreehandPath(m_eraserPoints));
        painter->restore();
    }

    drawShapeBuilderOverlay(painter);
    drawEnvelopeOverlay(painter);
    drawPerspectiveOverlay(painter);
    drawDimensionPreview(painter);
    drawSnapHint(painter);

    if (!m_drawing)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(previewPen());
    painter->setBrush(Qt::NoBrush);

    const QRectF raw(m_drawStart, m_drawCurrent);
    QRectF rect = raw.normalized();
    const bool shift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;

    switch (m_drawMode) {
    case DrawMode::Rectangle:
        if (shift) {
            const qreal side = qMax(rect.width(), rect.height());
            rect.setSize(QSizeF(side, side));
        }
        painter->drawRect(rect);
        break;
    case DrawMode::Ellipse:
        if (shift) {
            const qreal side = qMax(rect.width(), rect.height());
            rect.setSize(QSizeF(side, side));
        }
        painter->drawEllipse(rect);
        break;
    case DrawMode::Polygon:
    case DrawMode::Star:
    case DrawMode::Spiral: {
        const qreal radius = QLineF(m_drawStart, m_drawCurrent).length();
        if (radius > 1.0) {
            const QRectF bounds(m_drawStart.x() - radius,
                                m_drawStart.y() - radius,
                                radius * 2.0, radius * 2.0);
            QPainterPath path;
            if (m_drawMode == DrawMode::Polygon)
                path = buildPolygonPath(bounds, m_polygonSides);
            else if (m_drawMode == DrawMode::Star)
                path = buildStarPath(bounds, m_starPoints, m_starRatio);
            else
                path = buildSpiralPath(bounds, m_spiralTurns);
            painter->drawPath(path);
        }
        break;
    }
    case DrawMode::Line:
        painter->drawLine(m_drawStart, m_drawCurrent);
        break;
    case DrawMode::Freehand:
    case DrawMode::Artistic:
        if (m_freehandPoints.size() >= 2)
            painter->drawPath(buildFreehandPath(m_freehandPoints));
        break;
    case DrawMode::Crop:
        painter->drawRect(rect);
        painter->setBrush(QColor(0, 0, 0, 40));
        painter->setPen(Qt::NoPen);
        painter->drawRect(QRectF(pageRect().topLeft(), QPointF(pageRect().right(), rect.top())));
        painter->drawRect(QRectF(QPointF(pageRect().left(), rect.bottom()),
                                 pageRect().bottomRight()));
        break;
    default:
        break;
    }

    painter->restore();
}

void CanvasView::drawNodeOverlay(QPainter *painter)
{
    if (m_tool != Tool::Node)
        return;

    const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
    const qreal size = 7.0 / s;
    const qreal controlSize = 4.5 / s;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    for (VectorItem *item : selectedVectorItems()) {
        if (!item->hasPath())
            continue;
        const QVector<QPainterPath::Element> elements = item->pathElements();
        int i = 0;
        while (i < elements.size()) {
            const QPainterPath::Element e = elements.at(i);
            if (e.type == QPainterPath::MoveToElement
                || e.type == QPainterPath::LineToElement) {
                const QPointF p = item->mapToScene(QPointF(e.x, e.y));
                painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7), 1.2));
                painter->setBrush(m_selectedNodeElements.contains(i)
                                      ? QColor(0xff, 0xb3, 0x47)
                                      : QColor(0xffffff));
                painter->drawRect(QRectF(p - QPointF(size / 2, size / 2),
                                         QSizeF(size, size)));
                ++i;
            } else if (e.type == QPainterPath::CurveToElement
                       && i + 2 < elements.size()) {
                const QPointF anchor = item->mapToScene(
                    QPointF(elements.at(i + 2).x, elements.at(i + 2).y));
                const QPointF c1 = item->mapToScene(QPointF(e.x, e.y));
                const QPointF c2 = item->mapToScene(
                    QPointF(elements.at(i + 1).x, elements.at(i + 1).y));

                painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7, 130), 1.0));
                painter->drawLine(anchor, c2);

                painter->setBrush(QColor(0x9d, 0xc0, 0xf2));
                painter->setPen(QPen(QColor(0x4f, 0x7c, 0xc0), 1.0));
                painter->drawEllipse(c1, controlSize, controlSize);
                painter->drawEllipse(c2, controlSize, controlSize);

                painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7), 1.2));
                painter->setBrush(m_selectedNodeElements.contains(i + 2)
                                      ? QColor(0xff, 0xb3, 0x47)
                                      : QColor(0xffffff));
                painter->drawRect(QRectF(anchor - QPointF(size / 2, size / 2),
                                         QSizeF(size, size)));
                i += 3;
            } else {
                ++i;
            }
        }
    }

    painter->restore();
}

void CanvasView::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_tool == Tool::Zoom) {
        event->ignore();
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());
    VectorItem *item = itemAtScene(scenePos);
    if (item && !item->isLocked()) {
        if (!item->isSelected()) {
            if (item->groupIndex() > 0)
                selectGroupItems(item->groupIndex());
            else {
                scene()->clearSelection();
                item->setSelected(true);
                emit selectionChanged(1);
            }
        }
    } else if (!item) {
        scene()->clearSelection();
    }

    emit contextMenuRequested(event->globalPos(), item);
    event->accept();
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        zoomAt(m_zoom * factor, event->position().toPoint());
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

QList<VectorItem *> CanvasView::vectorItems() const
{
    QList<VectorItem *> result;
    const QList<QGraphicsItem *> all = scene()->items();
    for (QGraphicsItem *item : all) {
        if (item->type() == VectorItem::ItemType)
            result.append(static_cast<VectorItem *>(item));
    }
    std::sort(result.begin(), result.end(), [](VectorItem *a, VectorItem *b) {
        return a->zValue() < b->zValue();
    });
    return result;
}

QList<VectorItem *> CanvasView::selectedVectorItems() const
{
    QList<VectorItem *> result;
    const QList<QGraphicsItem *> selected = scene()->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (item->type() == VectorItem::ItemType)
            result.append(static_cast<VectorItem *>(item));
    }
    std::sort(result.begin(), result.end(), [](VectorItem *a, VectorItem *b) {
        return a->zValue() < b->zValue();
    });
    return result;
}

VectorItem *CanvasView::itemAtScene(const QPointF &scenePos) const
{
    const QList<QGraphicsItem *> items = scene()->items(scenePos);
    for (QGraphicsItem *item : items) {
        if (item->type() == VectorItem::ItemType)
            return static_cast<VectorItem *>(item);
    }
    return nullptr;
}

VectorItem *CanvasView::addVectorObject(const VectorObject &object, bool record, bool select)
{
    if (m_layers.isEmpty()) {
        m_layers.append(QStringLiteral("Слой 1"));
        m_activeLayer = 0;
    }

    VectorObject copy = object;
    if (copy.page == -9999)
        copy.page = m_currentPage;
    if (copy.layer < 0 || copy.layer >= m_layers.size())
        copy.layer = m_activeLayer;
    if (copy.id <= 0)
        copy.id = m_nextObjectId++;
    else
        m_nextObjectId = qMax(m_nextObjectId, copy.id + 1);
    if (copy.masterId == copy.id)
        copy.masterId = 0;

    if (record && copy.layer >= 0 && copy.layer < m_layers.size()
        && (m_layerLocked.value(copy.layer, false)
            || !m_layerVisible.value(copy.layer, true))) {
        emit statusHint(QStringLiteral("Слой «%1» заблокирован или скрыт.")
                            .arg(m_layers.at(copy.layer)));
        return nullptr;
    }

    if (record)
        emit beginEdit();

    auto *item = new VectorItem(copy);
    scene()->addItem(item);
    item->setZValue(m_nextZ++);
    connect(item, &VectorItem::editStarted, this, &CanvasView::beginEdit);
    connect(item, &VectorItem::editFinished, this, &CanvasView::endEdit);
    connect(item, &VectorItem::editFinished, this, [this, item]() {
        updateBlendsFor(item);
        updateDimensionsFor(item);
    });
    connect(item, &VectorItem::textEditRequested, this, &CanvasView::editTextItem);

    if (select) {
        scene()->clearSelection();
        item->setSelected(true);
    }

    normalizeZOrder();
    applyLayerState(copy.layer);
    updateSnapSettings();

    if (record)
        emit endEdit();

    emit selectionChanged(selectedVectorItems().size());
    return item;
}


QString CanvasView::pageLabel(int index) const
{
    if (index < 0) {
        const int master = -1 - index;
        if (master >= 0 && master < m_masters.size())
            return QStringLiteral("Мастер: %1").arg(m_masters.at(master).name);
        return QStringLiteral("Мастер");
    }
    if (index < m_pages.size())
        return m_pages.at(index).name;
    return QStringLiteral("Страница");
}

void CanvasView::applyPageVisibility()
{
    for (VectorItem *item : vectorItems()) {
        const VectorObject object = item->object();
        const bool layerOk =
            object.layer < 0 || object.layer >= m_layerVisible.size()
            || m_layerVisible.at(object.layer);
        item->setVisible(layerOk && object.page == m_currentPage);
    }
    viewport()->update();
}

void CanvasView::setCurrentPage(int index)
{
    if (!m_pages.isEmpty() && m_currentPage >= 0 && m_currentPage < m_pages.size())
        m_pages[m_currentPage].size = m_pageSize;

    const int minimum = -m_masters.size();
    const int maximum = m_pages.size() - 1;
    index = qBound(minimum, index, maximum);
    if (index == m_currentPage && !m_pages.isEmpty()) {
        applyPageVisibility();
        return;
    }

    m_currentPage = index;
    if (m_currentPage >= 0 && m_currentPage < m_pages.size()) {
        m_pageSize = m_pages.at(m_currentPage).size;
        for (VectorItem *item : vectorItems())
            item->setVisible(false);
    }
    scene()->clearSelection();
    applyPageVisibility();
    emit selectionChanged(0);
    emit pageChanged(m_currentPage);
    viewport()->update();
    emit statusHint(QStringLiteral("Открыто: %1").arg(pageLabel(m_currentPage)));
}

void CanvasView::setPageList(const QVector<PageInfo> &pages,
                             const QVector<MasterPage> &masters, int currentPage)
{
    m_pages = pages;
    if (m_pages.isEmpty())
        m_pages.append(PageInfo{QStringLiteral("Страница 1"), m_pageSize,
                                QColor(Qt::white), 37.8, 11.34, -1});
    m_masters = masters;
    m_currentPage = qBound(-m_masters.size(), currentPage, m_pages.size() - 1);
    if (m_currentPage >= 0)
        m_pageSize = m_pages.at(m_currentPage).size;
    applyPageVisibility();
    emit pageChanged(m_currentPage);
}

int CanvasView::addPage(const QString &name)
{
    if (m_currentPage >= 0 && m_currentPage < m_pages.size())
        m_pages[m_currentPage].size = m_pageSize;
    PageInfo info;
    info.name = name.isEmpty()
                    ? QStringLiteral("Страница %1").arg(m_pages.size() + 1)
                    : name;
    info.size = m_currentPage >= 0 && m_currentPage < m_pages.size()
                    ? m_pages.at(m_currentPage).size
                    : QSizeF(794.0, 1123.0);
    if (m_currentPage >= 0 && m_currentPage < m_pages.size()) {
        info.background = m_pages.at(m_currentPage).background;
        info.margin = m_pages.at(m_currentPage).margin;
        info.bleed = m_pages.at(m_currentPage).bleed;
    }
    emit beginEdit();
    m_pages.append(info);
    emit endEdit();
    setCurrentPage(m_pages.size() - 1);
    return m_pages.size() - 1;
}

int CanvasView::duplicatePage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return index;
    emit beginEdit();
    m_pages.insert(index + 1, m_pages.at(index));
    m_pages[index + 1].name = QStringLiteral("%1 (копия)").arg(m_pages.at(index).name);

    QVector<VectorItem *> items;
    for (VectorItem *item : vectorItems()) {
        if (item->object().page == index)
            items.append(item);
    }

    // Move the following pages down before inserting the copy, otherwise the
    // freshly created duplicates would be shifted as well.
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.page > index) {
            object.page += 1;
            item->setObject(object);
        }
    }

    // Duplicated symbol instances become independent copies on the new page.
    QMap<int, int> instanceMap;
    QVector<SymbolInstance> newInstances;
    for (const SymbolInstance &instance : m_instances) {
        if (instance.page != index)
            continue;
        SymbolInstance copy = instance;
        copy.id = m_nextInstanceId++;
        copy.page = index + 1;
        copy.group = m_nextGroupId++;
        instanceMap.insert(instance.id, copy.id);
        m_groups.append({copy.group, 0});
        newInstances.append(copy);
    }

    scene()->clearSelection();
    for (VectorItem *item : items) {
        VectorObject copy = item->object();
        copy.page = index + 1;
        copy.id = m_nextObjectId++;
        copy.masterId = 0;
        copy.group = 0;
        if (copy.instanceId > 0 && instanceMap.contains(copy.instanceId)) {
            for (const SymbolInstance &instance : newInstances) {
                if (instance.symbolId == copy.symbolId
                    && instanceMap.value(item->object().instanceId) == instance.id)
                    copy.group = instance.group;
            }
            copy.instanceId = instanceMap.value(copy.instanceId);
        }
        addVectorObject(copy, false, false);
    }
    m_instances.append(newInstances);
    emit endEdit();
    setCurrentPage(index + 1);
    return index + 1;
}

void CanvasView::removePage(int index)
{
    if (index < 0 || index >= m_pages.size() || m_pages.size() <= 1)
        return;
    emit beginEdit();
    const int removedPage = index;
    for (VectorItem *item : vectorItems()) {
        if (item->object().page == removedPage) {
            scene()->removeItem(item);
            delete item;
        }
    }
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.page > removedPage) {
            object.page -= 1;
            item->setObject(object);
        }
    }
    for (int i = m_instances.size() - 1; i >= 0; --i) {
        if (m_instances.at(i).page == removedPage) {
            m_instances.removeAt(i);
        } else if (m_instances.at(i).page > removedPage) {
            m_instances[i].page -= 1;
        }
    }
    m_pages.removeAt(index);
    for (PageInfo &page : m_pages) {
        if (page.master >= 0)
            page.master = page.master;
    }
    emit endEdit();
    setCurrentPage(qMin(m_currentPage, m_pages.size() - 1));
    emit pageChanged(m_currentPage);
}

void CanvasView::movePage(int from, int to)
{
    if (from < 0 || from >= m_pages.size() || to < 0 || to >= m_pages.size()
        || from == to)
        return;
    emit beginEdit();
    m_pages.move(from, to);
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.page < 0)
            continue;
        int page = object.page;
        if (page == from)
            page = to;
        else if (from < to && page > from && page <= to)
            page -= 1;
        else if (from > to && page >= to && page < from)
            page += 1;
        if (page != object.page) {
            object.page = page;
            item->setObject(object);
        }
    }
    for (SymbolInstance &instance : m_instances) {
        if (instance.page < 0)
            continue;
        int page = instance.page;
        if (page == from)
            page = to;
        else if (from < to && page > from && page <= to)
            page -= 1;
        else if (from > to && page >= to && page < from)
            page += 1;
        instance.page = page;
    }
    emit endEdit();
    setCurrentPage(to);
    emit pageChanged(m_currentPage);
}

void CanvasView::renamePage(int index, const QString &name)
{
    if (index < 0 || index >= m_pages.size() || name.trimmed().isEmpty())
        return;
    m_pages[index].name = name.trimmed();
    emit beginEdit();
    emit endEdit();
    emit pageChanged(m_currentPage);
}

void CanvasView::setPageInfo(int index, const PageInfo &info)
{
    if (index < 0 || index >= m_pages.size())
        return;
    emit beginEdit();
    m_pages[index] = info;
    if (index == m_currentPage) {
        m_pageSize = info.size;
        setPageSize(info.size);
    }
    emit endEdit();
    emit pageChanged(m_currentPage);
    viewport()->update();
}

int CanvasView::addMaster(const QString &name)
{
    MasterPage master;
    master.name = name.isEmpty()
                      ? QStringLiteral("Мастер %1").arg(m_masters.size() + 1)
                      : name;
    m_masters.append(master);
    emit beginEdit();
    emit endEdit();
    emit pageChanged(m_currentPage);
    return m_masters.size() - 1;
}

void CanvasView::renameMaster(int index, const QString &name)
{
    if (index < 0 || index >= m_masters.size() || name.trimmed().isEmpty())
        return;
    m_masters[index].name = name.trimmed();
    emit pageChanged(m_currentPage);
}

void CanvasView::setMasterInfo(int index, const MasterPage &master)
{
    if (index < 0 || index >= m_masters.size())
        return;
    m_masters[index] = master;
    emit pageChanged(m_currentPage);
}

void CanvasView::applyMasterToPage(int pageIndex, int masterIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pages.size())
        return;
    m_pages[pageIndex].master = masterIndex;
    emit beginEdit();
    emit endEdit();
    emit pageChanged(m_currentPage);
    viewport()->update();
}

int CanvasView::createMasterFromPage(int pageIndex, const QString &name)
{
    if (pageIndex < 0 || pageIndex >= m_pages.size())
        return -1;
    if (m_currentPage >= 0 && m_currentPage < m_pages.size())
        m_pages[m_currentPage].size = m_pageSize;

    QVector<VectorObject> copies;
    for (VectorItem *item : vectorItems()) {
        if (item->object().page == pageIndex)
            copies.append(item->object());
    }

    emit beginEdit();
    const int master = addMaster(name);
    setCurrentPage(-1 - master);
    for (VectorObject copy : copies) {
        copy.page = -1 - master;
        copy.id = 0;
        copy.masterId = 0;
        copy.group = 0;
        addVectorObject(copy, false, false);
    }
    m_pages[pageIndex].master = master;
    emit endEdit();
    setCurrentPage(pageIndex);
    emit pageChanged(m_currentPage);
    return master;
}

QImage CanvasView::renderPageImage(int index, const QSize &size) const
{
    Document document;
    document.pages = m_pages;
    document.masters = m_masters;
    document.layers = m_layers;
    document.layerVisible = m_layerVisible;
    document.layerLocked = m_layerLocked;
    document.pageSize = index >= 0 && index < m_pages.size()
                            ? m_pages.at(index).size
                            : (m_pages.isEmpty() ? m_pageSize : m_pages.first().size);
    document.currentPage = index >= 0 ? index : 0;
    if (index < 0 && !document.pages.isEmpty()) {
        PageInfo info = document.pages.first();
        info.master = -1 - index;
        document.pages[0] = info;
    }
    for (const VectorObject &object : objects()) {
        if (object.page == index)
            document.objects.append(object);
    }
    if (index >= 0 && index < m_pages.size()) {
        const int master = m_pages.at(index).master;
        if (master >= 0) {
            for (const VectorObject &object : objects()) {
                if (object.page == -1 - master)
                    document.objects.prepend(object);
            }
        }
    }

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = qMin(size.width() / qMax<qreal>(1.0, document.pageSize.width()),
                             size.height() / qMax<qreal>(1.0, document.pageSize.height()));
    painter.scale(scale, scale);
    document.render(&painter);
    painter.end();
    return image;
}

void CanvasView::setDocument(const Document &document)
{
    if (m_textEditor)
        finishTextEditing(false);
    scene()->clear();
    m_nextZ = 1.0;
    m_bezierActive = false;
    m_bezierNodes.clear();

    m_nextObjectId = 1;
    m_guidesX = document.guidesX;
    m_guidesY = document.guidesY;
    m_groups = document.groups;
    m_nextGroupId = 1;
    for (const VectorGroup &group : m_groups)
        m_nextGroupId = qMax(m_nextGroupId, group.id + 1);

    m_layers = document.layers;
    if (m_layers.isEmpty())
        m_layers.append(QStringLiteral("Слой 1"));
    m_layerVisible = document.layerVisible;
    m_layerLocked = document.layerLocked;
    m_layerVisible.resize(m_layers.size());
    m_layerLocked.resize(m_layers.size());
    for (int i = 0; i < m_layers.size(); ++i) {
        if (document.layerVisible.size() > i)
            m_layerVisible[i] = document.layerVisible.at(i);
        if (document.layerLocked.size() > i)
            m_layerLocked[i] = document.layerLocked.at(i);
    }
    m_activeLayer = qBound(0, m_activeLayer, m_layers.size() - 1);

    m_pages = document.pages;
    if (m_pages.isEmpty()) {
        m_pages.append(PageInfo{QStringLiteral("Страница 1"), document.pageSize,
                                QColor(Qt::white), 37.8, 11.34, -1});
    }
    m_masters = document.masters;
    m_currentPage = document.currentPage;
    if (m_currentPage < 0)
        m_currentPage = 0;
    if (m_currentPage >= m_pages.size())
        m_currentPage = m_pages.size() - 1;

    setPageSize(m_pages.at(m_currentPage).size);
    m_drawingScale = document.drawingScale > 0.0 ? document.drawingScale : 1.0;
    setBlends(document.blends);
    m_symbols = document.symbols;
    m_instances = document.instances;
    m_nextSymbolId = 1;
    for (const SymbolDefinition &symbol : m_symbols)
        m_nextSymbolId = qMax(m_nextSymbolId, symbol.id + 1);
    m_nextInstanceId = 1;
    for (const SymbolInstance &instance : m_instances)
        m_nextInstanceId = qMax(m_nextInstanceId, instance.id + 1);
    m_patterns = document.patterns;
    m_nextPatternId = 1;
    for (const PatternDefinition &pattern : m_patterns)
        m_nextPatternId = qMax(m_nextPatternId, pattern.id + 1);
    for (const VectorObject &object : document.objects) {
        VectorObject copy = object;
        if (copy.page == -9999)
            copy.page = 0;
        addVectorObject(copy, false, false);
    }
    updateAllBlends();
    normalizeZOrder();
    for (int i = 0; i < m_layers.size(); ++i)
        applyLayerState(i);
    applyPageVisibility();
    updateSnapSettings();
    emit selectionChanged(0);
    emit pageChanged(m_currentPage);
    viewport()->update();
}

QVector<VectorObject> CanvasView::objects() const
{
    QVector<VectorObject> result;
    const QList<VectorItem *> items = vectorItems();
    result.reserve(items.size());
    for (VectorItem *item : items)
        result.append(item->object());
    return result;
}

void CanvasView::deleteSelectedObjects()
{
    if (m_textEditor)
        finishTextEditing(false);
    const QList<VectorItem *> items = selectedVectorItems();
    if (items.isEmpty()) {
        emit statusHint(QStringLiteral("Нет выделенных объектов."));
        return;
    }
    QSet<int> removedIds;
    for (VectorItem *item : items)
        removedIds.insert(item->object().id);

    emit beginEdit();
    for (VectorItem *item : items) {
        scene()->removeItem(item);
        delete item;
    }
    for (VectorItem *item : vectorItems()) {
        if (removedIds.contains(item->object().masterId)) {
            VectorObject object = item->object();
            object.masterId = 0;
            item->setObject(object);
        }
    }
    for (int i = m_blends.size() - 1; i >= 0; --i) {
        if (removedIds.contains(m_blends.at(i).fromId)
            || removedIds.contains(m_blends.at(i).toId)) {
            removeBlend(m_blends.at(i).id);
        }
    }
    for (int i = m_instances.size() - 1; i >= 0; --i) {
        bool alive = false;
        for (VectorItem *item : vectorItems()) {
            if (item->object().instanceId == m_instances.at(i).id) {
                alive = true;
                break;
            }
        }
        if (!alive)
            m_instances.removeAt(i);
    }
    normalizeZOrder();
    emit endEdit();
    emit selectionChanged(0);
    viewport()->update();
}

void CanvasView::selectAllObjects()
{
    m_insideGroup = false;
    const QList<VectorItem *> items = vectorItems();
    int selected = 0;
    for (VectorItem *item : items) {
        if (!item->isVisible()
            || !(item->flags() & QGraphicsItem::ItemIsSelectable))
            continue;
        item->setSelected(true);
        ++selected;
    }
    emit selectionChanged(selected);
}

void CanvasView::raiseSelection()
{
    if (vectorItems().isEmpty())
        return;

    emit beginEdit();
    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int layer = 0; layer < m_layers.size(); ++layer)
        perLayer[layer] = itemsInLayer(layer);

    for (int layer = 0; layer < perLayer.size(); ++layer) {
        QList<VectorItem *> &items = perLayer[layer];
        for (int i = items.size() - 2; i >= 0; --i) {
            if (items.at(i)->isSelected() && !items.at(i + 1)->isSelected())
                items.swapItemsAt(i, i + 1);
        }
    }
    applyLayerLayout(perLayer);
    emit endEdit();
}

void CanvasView::lowerSelection()
{
    if (vectorItems().isEmpty())
        return;

    emit beginEdit();
    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int layer = 0; layer < m_layers.size(); ++layer)
        perLayer[layer] = itemsInLayer(layer);

    for (int layer = 0; layer < perLayer.size(); ++layer) {
        QList<VectorItem *> &items = perLayer[layer];
        for (int i = 1; i < items.size(); ++i) {
            if (items.at(i)->isSelected() && !items.at(i - 1)->isSelected())
                items.swapItemsAt(i, i - 1);
        }
    }
    applyLayerLayout(perLayer);
    emit endEdit();
}

QList<VectorItem *> CanvasView::itemsInLayer(int layer) const
{
    QList<VectorItem *> result;
    for (VectorItem *item : vectorItems()) {
        if (item->object().layer == layer)
            result.append(item);
    }
    return result;
}

int CanvasView::layerOf(VectorItem *item) const
{
    return item ? item->object().layer : 0;
}

void CanvasView::setActiveLayer(int index)
{
    m_activeLayer = qBound(0, index, qMax(0, m_layers.size() - 1));
}

int CanvasView::addGroupRecord(int parentGroup)
{
    const int id = m_nextGroupId++;
    m_groups.append({id, parentGroup});
    return id;
}

int CanvasView::addLayer(const QString &name)
{
    emit beginEdit();

    QString layerName = name.trimmed();
    if (layerName.isEmpty()) {
        int number = m_layers.size() + 1;
        layerName = QStringLiteral("Слой %1").arg(number);
        while (m_layers.contains(layerName)) {
            ++number;
            layerName = QStringLiteral("Слой %1").arg(number);
        }
    }

    m_layers.append(layerName);
    m_layerVisible.append(true);
    m_layerLocked.append(false);
    m_activeLayer = m_layers.size() - 1;
    normalizeZOrder();

    emit endEdit();
    emit selectionChanged(selectedVectorItems().size());
    return m_activeLayer;
}

void CanvasView::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size())
        return;
    if (m_layers.size() <= 1) {
        emit statusHint(QStringLiteral("Нельзя удалить единственный слой."));
        return;
    }

    emit beginEdit();

    const QList<VectorItem *> doomed = itemsInLayer(index);
    for (VectorItem *item : doomed) {
        scene()->removeItem(item);
        delete item;
    }

    m_layers.removeAt(index);
    if (index < m_layerVisible.size())
        m_layerVisible.removeAt(index);
    if (index < m_layerLocked.size())
        m_layerLocked.removeAt(index);
    for (VectorItem *item : vectorItems()) {
        const int layer = item->object().layer;
        if (layer > index)
            item->setLayerIndex(layer - 1);
    }
    m_activeLayer = qBound(0, m_activeLayer > index ? m_activeLayer - 1 : m_activeLayer,
                           m_layers.size() - 1);
    normalizeZOrder();

    emit endEdit();
    emit statusHint(QStringLiteral("Слой удалён. Удалено объектов: %1").arg(doomed.size()));
}

void CanvasView::renameLayer(int index, const QString &name)
{
    if (index < 0 || index >= m_layers.size())
        return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    emit beginEdit();
    m_layers[index] = trimmed;
    emit endEdit();
}

void CanvasView::moveLayer(int from, int to)
{
    if (from < 0 || from >= m_layers.size() || to < 0 || to >= m_layers.size()
        || from == to) {
        return;
    }

    emit beginEdit();

    QVector<int> order;
    for (int i = 0; i < m_layers.size(); ++i)
        order.append(i);
    order.move(from, to);

    QStringList newNames;
    newNames.reserve(order.size());
    for (int oldIndex : order)
        newNames.append(m_layers.at(oldIndex));

    QVector<QList<VectorItem *>> perLayer(order.size());
    for (VectorItem *item : vectorItems()) {
        const int newIndex = order.indexOf(item->object().layer);
        perLayer[newIndex].append(item);
    }

    m_layers = newNames;
    QVector<bool> newVisible;
    QVector<bool> newLocked;
    newVisible.reserve(order.size());
    newLocked.reserve(order.size());
    for (int oldIndex : order) {
        newVisible.append(m_layerVisible.value(oldIndex, true));
        newLocked.append(m_layerLocked.value(oldIndex, false));
    }
    m_layerVisible = newVisible;
    m_layerLocked = newLocked;
    m_activeLayer = qMax(0, order.indexOf(m_activeLayer));
    applyLayerLayout(perLayer);

    emit endEdit();
}

void CanvasView::moveItemToLayer(VectorItem *item, int layer, int positionFromTop)
{
    if (!item || layer < 0 || layer >= m_layers.size())
        return;
    if (m_layerLocked.value(layer, false)) {
        emit statusHint(QStringLiteral("Целевой слой заблокирован."));
        return;
    }

    emit beginEdit();

    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int i = 0; i < m_layers.size(); ++i)
        perLayer[i] = itemsInLayer(i);

    const int oldLayer = qBound(0, item->object().layer, m_layers.size() - 1);
    perLayer[oldLayer].removeAll(item);

    QList<VectorItem *> &target = perLayer[layer];
    const int position = qBound(0, positionFromTop, target.size());
    target.insert(target.size() - position, item);
    item->setLayerIndex(layer);

    applyLayerLayout(perLayer);
    emit endEdit();
}

void CanvasView::moveItemsToLayer(const QList<VectorItem *> &items, int layer)
{
    if (items.isEmpty() || layer < 0 || layer >= m_layers.size())
        return;
    if (m_layerLocked.value(layer, false)) {
        emit statusHint(QStringLiteral("Целевой слой заблокирован."));
        return;
    }

    emit beginEdit();

    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int i = 0; i < m_layers.size(); ++i)
        perLayer[i] = itemsInLayer(i);

    QList<VectorItem *> ordered = items;
    std::sort(ordered.begin(), ordered.end(), [](VectorItem *a, VectorItem *b) {
        return a->zValue() < b->zValue();
    });

    for (VectorItem *item : ordered) {
        const int oldLayer = qBound(0, item->object().layer, m_layers.size() - 1);
        perLayer[oldLayer].removeAll(item);
        perLayer[layer].append(item);
        item->setLayerIndex(layer);
    }

    applyLayerLayout(perLayer);
    emit endEdit();
}

bool CanvasView::layerVisible(int index) const
{
    return m_layerVisible.value(index, true);
}

bool CanvasView::layerLocked(int index) const
{
    return m_layerLocked.value(index, false);
}

void CanvasView::setLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= m_layers.size())
        return;
    emit beginEdit();
    m_layerVisible[index] = visible;
    applyLayerState(index);
    emit endEdit();
}

void CanvasView::setLayerLocked(int index, bool locked)
{
    if (index < 0 || index >= m_layers.size())
        return;
    emit beginEdit();
    m_layerLocked[index] = locked;
    applyLayerState(index);
    emit endEdit();
}

void CanvasView::applyLayerState(int layer)
{
    if (layer < 0 || layer >= m_layers.size())
        return;

    const bool visible = m_layerVisible.value(layer, true);
    const bool locked = m_layerLocked.value(layer, false);
    for (VectorItem *item : itemsInLayer(layer)) {
        const bool itemLocked = item->isLocked();
        item->setVisible(visible && item->object().page == m_currentPage);
        item->setFlag(QGraphicsItem::ItemIsSelectable, !locked && !itemLocked);
        item->setFlag(QGraphicsItem::ItemIsMovable, !locked && !itemLocked);
        if (locked || itemLocked)
            item->setSelected(false);
    }
}

void CanvasView::applyLayerLayout(const QVector<QList<VectorItem *>> &perLayer)
{
    int z = 1;
    for (int layer = 0; layer < perLayer.size(); ++layer) {
        const bool visible = m_layerVisible.value(layer, true);
        const bool locked = m_layerLocked.value(layer, false);
        for (VectorItem *item : perLayer.at(layer)) {
            item->setLayerIndex(layer);
            item->setZValue(z++);
            const bool itemLocked = item->isLocked();
            item->setVisible(visible);
            item->setFlag(QGraphicsItem::ItemIsSelectable, !locked && !itemLocked);
            item->setFlag(QGraphicsItem::ItemIsMovable, !locked && !itemLocked);
        }
    }
    m_nextZ = z;
    viewport()->update();
}

void CanvasView::normalizeZOrder()
{
    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (VectorItem *item : vectorItems()) {
        int layer = item->object().layer;
        if (layer < 0 || layer >= m_layers.size())
            layer = 0;
        perLayer[layer].append(item);
    }
    applyLayerLayout(perLayer);
}

void CanvasView::startDrawing(DrawMode mode, const QPointF &scenePos)
{
    m_drawMode = mode;
    m_drawing = true;
    m_drawStart = scenePos;
    m_drawCurrent = scenePos;
    m_freehandPoints.clear();
    if (mode == DrawMode::Freehand || mode == DrawMode::Artistic)
        m_freehandPoints.append(scenePos);
    viewport()->update();
}

void CanvasView::finishDrawing(const QPointF &scenePos)
{
    if (!m_drawing)
        return;

    m_drawing = false;
    m_drawCurrent = scenePos;
    const QPointF start = m_drawStart;
    const QPointF end = scenePos;
    const bool tiny = QLineF(start, end).length() < 5.0;
    const bool shift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;

    const auto baseObject = [this](ShapeType type) {
        VectorObject object;
        object.type = type;
        object.fill = m_fill;
        object.hasFill = true;
        object.stroke = m_stroke;
        object.strokeWidth = m_strokeWidth;
        object.hasStroke = m_strokeWidth > 0.0;
        return object;
    };

    switch (m_drawMode) {
    case DrawMode::Rectangle:
    case DrawMode::Ellipse: {
        QRectF rect = QRectF(start, end).normalized();
        if (tiny)
            rect = QRectF(start.x() - 60.0, start.y() - 40.0, 120.0, 80.0);
        if (shift) {
            const qreal side = qMax(rect.width(), rect.height());
            rect.setSize(QSizeF(side, side));
        }
        if (rect.width() < 1.0 || rect.height() < 1.0)
            break;

        VectorObject object = baseObject(m_drawMode == DrawMode::Rectangle
                                             ? ShapeType::Rectangle
                                             : ShapeType::Ellipse);
        if (m_drawMode == DrawMode::Rectangle) {
            object.cornerRadius = m_cornerRadius;
            object.path = buildRectanglePath(QRectF(0.0, 0.0, rect.width(),
                                                    rect.height()),
                                             m_cornerRadius);
        } else {
            object.path.addEllipse(QRectF(0.0, 0.0, rect.width(), rect.height()));
        }
        object.pos = rect.topLeft();
        addVectorObject(object);
        break;
    }
    case DrawMode::Polygon:
    case DrawMode::Star:
    case DrawMode::Spiral: {
        const qreal radius = tiny ? 60.0 : QLineF(start, end).length();
        if (radius < 1.0)
            break;

        const QRectF bounds(start.x() - radius, start.y() - radius,
                            radius * 2.0, radius * 2.0);
        VectorObject object;
        if (m_drawMode == DrawMode::Polygon) {
            object = baseObject(ShapeType::Polygon);
            object.sides = m_polygonSides;
            object.path = buildPolygonPath(bounds, m_polygonSides);
        } else if (m_drawMode == DrawMode::Star) {
            object = baseObject(ShapeType::Star);
            object.starPoints = m_starPoints;
            object.starRatio = m_starRatio;
            object.path = buildStarPath(bounds, m_starPoints, m_starRatio);
        } else {
            object = baseObject(ShapeType::Spiral);
            object.hasFill = false;
            object.spiralTurns = m_spiralTurns;
            object.path = buildSpiralPath(bounds, m_spiralTurns);
            object.strokeWidth = qMax<qreal>(m_strokeWidth, 1.5);
        }
        object.pos = bounds.topLeft();
        addVectorObject(object);
        break;
    }
    case DrawMode::Line: {
        QPointF delta = end - start;
        if (tiny)
            delta = QPointF(140.0, 0.0);
        if (shift) {
            if (qAbs(delta.x()) > qAbs(delta.y()))
                delta.setY(0.0);
            else
                delta.setX(0.0);
        }
        VectorObject object = baseObject(ShapeType::Line);
        object.hasFill = false;
        object.path.moveTo(0.0, 0.0);
        object.path.lineTo(delta);
        object.pos = start;
        addVectorObject(object);
        break;
    }
    case DrawMode::Freehand:
    case DrawMode::Artistic: {
        if (m_freehandPoints.size() < 2) {
            m_freehandPoints.clear();
            break;
        }
        VectorObject object = baseObject(m_drawMode == DrawMode::Freehand
                                             ? ShapeType::Freehand
                                             : ShapeType::Artistic);
        object.path = buildFreehandPath(m_freehandPoints);
        object.hasFill = false;
        object.pos = QPointF(0.0, 0.0);
        object.stroke = m_drawMode == DrawMode::Freehand ? m_stroke : m_fill;
        object.strokeWidth = m_drawMode == DrawMode::Freehand
                                 ? qMax<qreal>(m_strokeWidth, 1.0)
                                 : qMax<qreal>(m_strokeWidth * 4.0, 8.0);
        addVectorObject(object);
        m_freehandPoints.clear();
        break;
    }
    case DrawMode::Crop:
        cropTo(QRectF(start, end).normalized());
        break;
    default:
        break;
    }

    m_drawMode = DrawMode::None;
    viewport()->update();
}

QPainterPath CanvasView::buildFreehandPath(const QVector<QPointF> &points) const
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    if (points.size() == 1)
        return path;

    for (int i = 0; i < points.size() - 1; ++i) {
        const QPointF p0 = points.at(qMax(0, i - 1));
        const QPointF p1 = points.at(i);
        const QPointF p2 = points.at(i + 1);
        const QPointF p3 = points.at(qMin(points.size() - 1, i + 2));
        const QPointF c1 = p1 + (p2 - p0) / 6.0;
        const QPointF c2 = p2 - (p3 - p1) / 6.0;
        path.cubicTo(c1, c2, p2);
    }
    return path;
}

QPainterPath CanvasView::buildBezierPath() const
{
    QPainterPath path;
    if (m_bezierNodes.isEmpty())
        return path;
    path.moveTo(m_bezierNodes.first().anchor);
    for (int i = 1; i < m_bezierNodes.size(); ++i)
        path.cubicTo(m_bezierNodes.at(i - 1).out, m_bezierNodes.at(i).in,
                     m_bezierNodes.at(i).anchor);
    return path;
}

void CanvasView::addBezierNode(const QPointF &scenePos)
{
    m_bezierActive = true;
    m_bezierDragging = true;
    m_bezierDragMoved = false;
    m_bezierClosed = false;
    m_bezierHasCursor = true;
    m_bezierCursor = scenePos;

    BezierNode node;
    node.anchor = scenePos;
    node.in = scenePos;
    node.out = scenePos;
    node.mode = m_bezierNodeMode;
    m_bezierNodes.append(node);
    viewport()->update();
}

void CanvasView::finishBezier()
{
    if (!m_bezierActive)
        return;

    const bool closed = m_bezierClosed || m_bezierAutoClose;
    const int nodeCount = m_bezierNodes.size();

    m_bezierActive = false;
    m_bezierDragging = false;
    m_bezierHasCursor = false;

    if (m_bezierNodes.size() >= 2) {
        VectorObject object;
        object.type = ShapeType::Bezier;
        object.path = buildBezierPath();
        if (closed && m_bezierNodes.size() >= 2) {
            object.path.cubicTo(m_bezierNodes.last().out,
                                m_bezierNodes.first().in,
                                m_bezierNodes.first().anchor);
            object.path.closeSubpath();
        }
        object.pos = QPointF(0.0, 0.0);
        object.fill = m_fill;
        object.hasFill = closed && m_fill.alpha() > 0;
        object.stroke = m_stroke;
        object.strokeWidth = qMax<qreal>(m_strokeWidth, 1.0);
        object.hasStroke = true;
        addVectorObject(object);
        emit statusHint(QStringLiteral("Кривая создана: %1 узлов%2")
                            .arg(nodeCount)
                            .arg(closed ? QStringLiteral(", замкнута") : QString()));
    }

    m_bezierClosed = false;
    m_bezierNodes.clear();
    viewport()->update();
}

void CanvasView::closeBezier()
{
    if (!m_bezierActive || m_bezierNodes.size() < 2) {
        emit statusHint(QStringLiteral("Для замыкания нужно минимум два узла."));
        return;
    }
    m_bezierClosed = true;
    finishBezier();
}

void CanvasView::deleteLastBezierNode()
{
    if (!m_bezierActive)
        return;
    if (!m_bezierNodes.isEmpty())
        m_bezierNodes.removeLast();
    if (m_bezierNodes.isEmpty()) {
        m_bezierActive = false;
        m_bezierDragging = false;
    }
    viewport()->update();
    emit statusHint(QStringLiteral("Узел удалён. Осталось: %1").arg(m_bezierNodes.size()));
}

void CanvasView::setBezierNodeMode(BezierNodeMode mode)
{
    m_bezierNodeMode = mode;
    if (!m_bezierNodes.isEmpty())
        m_bezierNodes.last().mode = mode;
}

void CanvasView::cancelBezier()
{
    m_bezierActive = false;
    m_bezierDragging = false;
    m_bezierClosed = false;
    m_bezierNodes.clear();
    viewport()->update();
}

void CanvasView::createTextAt(const QPointF &scenePos)
{
    VectorObject object;
    object.type = ShapeType::Text;
    object.pos = scenePos;
    object.text = QStringLiteral("Текст");
    object.fill = m_fill;
    object.hasFill = true;
    object.hasStroke = false;
    object.strokeWidth = 0.0;
    object.textBox = QRectF(0.0, 0.0, 300.0, 40.0);
    object.textAlign = 0;
    QFont font(QStringLiteral("Adwaita Sans"));
    font.setPixelSize(24);
    object.font = font;

    VectorItem *item = addVectorObject(object);
    editTextItem(item);
}

void CanvasView::editTextItem(VectorItem *item)
{
    if (!item || item->object().type != ShapeType::Text)
        return;

    if (m_textEditor)
        finishTextEditing(false);

    emit beginEdit();
    m_textItem = item;
    m_textOriginal = item->object().text;

    auto *editor = new QTextEdit(viewport());
    m_textEditor = editor;
    editor->setPlainText(m_textOriginal);
    editor->setFont(item->object().font);
    editor->setAlignment(item->object().textAlign == 1
                             ? Qt::AlignHCenter
                             : (item->object().textAlign == 2 ? Qt::AlignRight
                                                              : Qt::AlignLeft));
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setStyleSheet(QStringLiteral(
        "QTextEdit { background: rgba(27,29,33,235); color:#f2f5fa;"
        " border:1px solid #6f9ee8; border-radius:4px; padding:3px;"
        " selection-background-color:#35507d; }"));

    const QRectF sceneRect = item->mapRectToScene(item->localBounds());
    const QPoint topLeft = mapFromScene(sceneRect.topLeft());
    const QPoint bottomRight = mapFromScene(sceneRect.bottomRight());
    QRect geometry(topLeft, bottomRight);
    geometry.setWidth(qMax(240, geometry.width()));
    geometry.setHeight(qMax(80, geometry.height()));
    geometry.moveLeft(qBound(0, geometry.left(), viewport()->width() - 80));
    geometry.moveTop(qBound(0, geometry.top(), viewport()->height() - 40));
    editor->setGeometry(geometry);

    editor->installEventFilter(this);
    connect(editor, &QTextEdit::textChanged, this, [this, item, editor]() {
        if (m_textItem == item && m_textEditor == editor)
            item->setText(editor->toPlainText());
    });
    if (m_tabShortcut)
        m_tabShortcut->setEnabled(false);
    editor->show();
    editor->setFocus();
    editor->moveCursor(QTextCursor::End);
}

void CanvasView::finishTextEditing(bool cancel)
{
    if (!m_textEditor)
        return;

    QTextEdit *editor = m_textEditor;
    VectorItem *item = m_textItem;
    const QString original = m_textOriginal;
    const QString text = editor->toPlainText();
    m_textEditor = nullptr;
    m_textItem = nullptr;
    editor->removeEventFilter(this);
    editor->hide();
    editor->deleteLater();
    if (m_tabShortcut)
        m_tabShortcut->setEnabled(true);

    if (item) {
        if (cancel)
            item->setText(original);
        else
            item->setText(text);
    }
    emit endEdit();
    if (!cancel)
        emit statusHint(QStringLiteral("Текст обновлён."));
}

bool CanvasView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_textEditor) {
        if (event->type() == QEvent::FocusOut) {
            finishTextEditing(false);
            return false;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishTextEditing(true);
                return true;
            }
            if ((keyEvent->key() == Qt::Key_Return
                 || keyEvent->key() == Qt::Key_Enter)
                && (keyEvent->modifiers() & Qt::ControlModifier)) {
                finishTextEditing(false);
                return true;
            }
        }
    }
    return QGraphicsView::eventFilter(watched, event);
}

void CanvasView::convertTextToCurves()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите текст для перевода в кривые."));
        return;
    }

    emit beginEdit();
    int converted = 0;
    for (VectorItem *item : selected) {
        if (item->object().type != ShapeType::Text)
            continue;
        VectorObject object = item->object();
        const QPainterPath path = buildTextPath(object);
        if (path.isEmpty())
            continue;

        object.type = ShapeType::Bezier;
        object.path = path;
        object.text.clear();
        object.id = 0;
        object.masterId = 0;
        object.cornerRadius = 0;

        scene()->removeItem(item);
        delete item;
        VectorItem *curve = addVectorObject(object, false, true);
        if (curve)
            curve->setSelected(true);
        ++converted;
    }
    emit endEdit();
    emit statusHint(converted > 0
                        ? QStringLiteral("Текст переведён в кривые: %1").arg(converted)
                        : QStringLiteral("Среди выделенных нет текста."));
}

void CanvasView::applyFillAt(const QPointF &scenePos)
{
    VectorItem *item = itemAtScene(scenePos);
    if (!item) {
        emit statusHint(QStringLiteral("Щёлкните по объекту, чтобы залить его цветом %1.")
                            .arg(m_fill.name().toUpper()));
        return;
    }
    emit beginEdit();
    item->setFillColor(m_fill);
    emit endEdit();
    emit statusHint(QStringLiteral("Заливка применена: %1").arg(m_fill.name().toUpper()));
}

void CanvasView::applyOutlineAt(const QPointF &scenePos)
{
    VectorItem *item = itemAtScene(scenePos);
    if (!item) {
        const QList<VectorItem *> selected = selectedVectorItems();
        if (!selected.isEmpty())
            item = selected.first();
    }
    if (!item) {
        emit statusHint(QStringLiteral("Выберите объект для настройки обводки."));
        return;
    }

    const QColor color = QColorDialog::getColor(m_stroke, this,
                                                QStringLiteral("Цвет обводки"));
    if (!color.isValid())
        return;

    bool ok = false;
    const double width = QInputDialog::getDouble(this, QStringLiteral("Обводка"),
                                                 QStringLiteral("Толщина, px:"),
                                                 m_strokeWidth, 0.0, 100.0, 1, &ok);
    if (!ok)
        return;

    emit beginEdit();
    item->setStrokeColor(color);
    item->setStrokeWidth(width);
    emit endEdit();

    m_stroke = color;
    m_strokeWidth = width;
    emit statusHint(QStringLiteral("Обводка: %1, %2 px")
                        .arg(color.name().toUpper())
                        .arg(width));
}

void CanvasView::toggleShadowAt(const QPointF &scenePos)
{
    VectorItem *item = itemAtScene(scenePos);
    if (!item) {
        emit statusHint(QStringLiteral("Щёлкните по объекту, чтобы включить или выключить тень."));
        return;
    }
    const bool on = !item->object().shadow;
    emit beginEdit();
    item->setShadowEnabled(on);
    emit endEdit();
    emit statusHint(on ? QStringLiteral("Тень включена.") : QStringLiteral("Тень выключена."));
}

namespace {
QPolygonF identityQuad()
{
    return QPolygonF({QPointF(0.0, 0.0), QPointF(1.0, 0.0),
                      QPointF(1.0, 1.0), QPointF(0.0, 1.0)});
}
} // namespace

QPolygonF CanvasView::perspectiveQuad(const VectorObject &object) const
{
    for (const Effect &effect : object.effects) {
        if (effect.type == QLatin1String("perspective") && effect.quad.size() == 4)
            return effect.quad;
    }
    return identityQuad();
}

int CanvasView::perspectiveEffectIndex(VectorItem *item) const
{
    if (!item)
        return -1;
    const QVector<Effect> effects = item->effects();
    for (int i = 0; i < effects.size(); ++i) {
        if (effects.at(i).type == QLatin1String("perspective"))
            return i;
    }
    return -1;
}

QPointF CanvasView::perspectiveCornerScene(VectorItem *item, int corner) const
{
    if (!item)
        return QPointF();
    const QRectF bounds = item->object().localBounds();
    const QPolygonF quad = perspectiveQuad(item->object());
    const QPointF normalised = quad.value(corner, QPointF());
    const QPointF local(bounds.left() + normalised.x() * bounds.width(),
                        bounds.top() + normalised.y() * bounds.height());
    return item->mapToScene(local);
}

bool CanvasView::perspectivePress(const QPointF &scenePos)
{
    const qreal tolerance = 13.0 / (m_zoom > 0.0 ? m_zoom : 1.0);

    if (m_perspectiveItem) {
        for (int corner = 0; corner < 4; ++corner) {
            if (QLineF(scenePos, perspectiveCornerScene(m_perspectiveItem, corner))
                    .length() <= tolerance) {
                m_perspectiveCorner = corner;
                m_perspectiveLastScene = scenePos;
                emit beginEdit();
                viewport()->update();
                return true;
            }
        }
    }

    VectorItem *item = itemAtScene(scenePos);
    if (!item) {
        m_perspectiveItem = nullptr;
        m_perspectiveCorner = -1;
        viewport()->update();
        return false;
    }

    m_perspectiveItem = item;
    scene()->clearSelection();
    item->setSelected(true);
    emit selectionChanged(1);

    if (perspectiveEffectIndex(item) < 0) {
        QPolygonF quad;
        quad << QPointF(0.0, 0.0) << QPointF(1.0, 0.0)
             << QPointF(1.0, 1.0) << QPointF(0.0, 1.0);
        Effect effect = makeEffect(QStringLiteral("perspective"));
        effect.quad = quad;
        emit beginEdit();
        QVector<Effect> effects = item->effects();
        effects.append(effect);
        item->setEffects(effects);
        emit endEdit();
        emit statusHint(QStringLiteral(
            "Потяните угловые маркеры, чтобы построить перспективу. "
            "Shift+двойной клик сбрасывает искажение."));
    }

    m_perspectiveCorner = -1;
    viewport()->update();
    return true;
}

void CanvasView::perspectiveMove(const QPointF &scenePos)
{
    if (!m_perspectiveItem || m_perspectiveCorner < 0)
        return;
    const int index = perspectiveEffectIndex(m_perspectiveItem);
    if (index < 0)
        return;

    const QRectF bounds = m_perspectiveItem->object().localBounds();
    if (bounds.width() < 1.0 || bounds.height() < 1.0)
        return;

    QVector<Effect> effects = m_perspectiveItem->effects();
    if (index >= effects.size())
        return;

    QPolygonF quad = effects.at(index).quad;
    if (quad.size() != 4)
        quad = identityQuad();

    const QPointF local = m_perspectiveItem->mapFromScene(scenePos);
    const QPointF normalised((local.x() - bounds.left()) / bounds.width(),
                             (local.y() - bounds.top()) / bounds.height());
    quad[m_perspectiveCorner] = QPointF(qBound(-2.0, normalised.x(), 3.0),
                                        qBound(-2.0, normalised.y(), 3.0));
    effects[index].quad = quad;
    m_perspectiveItem->setEffects(effects);
    m_perspectiveLastScene = scenePos;
    viewport()->update();
}

void CanvasView::perspectiveRelease()
{
    if (m_perspectiveCorner < 0)
        return;
    m_perspectiveCorner = -1;
    emit endEdit();
    emit statusHint(QStringLiteral("Перспектива обновлена."));
    viewport()->update();
}

void CanvasView::drawPerspectiveOverlay(QPainter *painter)
{
    if (m_tool != Tool::Perspective)
        return;

    QList<VectorItem *> targets;
    if (m_perspectiveItem)
        targets.append(m_perspectiveItem);
    else
        targets = selectedVectorItems();

    const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
    const qreal handle = 8.0 / s;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (VectorItem *item : targets) {
        if (!item)
            continue;
        QPolygonF quad;
        for (int corner = 0; corner < 4; ++corner)
            quad << perspectiveCornerScene(item, corner);

        QPen outline(QColor(0xff, 0xb3, 0x47), 1.4 / s);
        outline.setStyle(Qt::DashLine);
        painter->setPen(outline);
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(quad);

        painter->setPen(QPen(QColor(0x1b, 0x1d, 0x21), 1.2 / s));
        for (int corner = 0; corner < 4; ++corner) {
            const bool active = item == m_perspectiveItem
                                && corner == m_perspectiveCorner;
            painter->setBrush(active ? QColor(0xff, 0xb3, 0x47)
                                     : QColor(0x7f, 0xae, 0xf7));
            painter->drawRect(QRectF(quad.at(corner) - QPointF(handle / 2, handle / 2),
                                     QSizeF(handle, handle)));
        }
    }
    painter->restore();
}

bool CanvasView::extrudePress(const QPointF &scenePos)
{
    VectorItem *item = itemAtScene(scenePos);
    if (!item)
        return false;

    scene()->clearSelection();
    item->setSelected(true);
    emit selectionChanged(1);

    QVector<Effect> effects = item->effects();
    int index = -1;
    for (int i = 0; i < effects.size(); ++i) {
        if (effects.at(i).type == QLatin1String("extrude")) {
            index = i;
            break;
        }
    }

    emit beginEdit();
    if (index < 0) {
        effects.append(makeEffect(QStringLiteral("extrude")));
        index = effects.size() - 1;
        item->setEffects(effects);
    }

    m_extrudeDragging = true;
    m_extrudeItem = item;
    m_extrudeEffect = index;
    m_extrudeStart = scenePos;
    emit statusHint(QStringLiteral(
        "Потяните от объекта, чтобы задать глубину и направление экструзии."));
    viewport()->update();
    return true;
}

void CanvasView::extrudeMove(const QPointF &scenePos)
{
    if (!m_extrudeDragging || !m_extrudeItem || m_extrudeEffect < 0)
        return;

    const QPointF delta = scenePos - m_extrudeStart;
    const qreal distance = std::hypot(delta.x(), delta.y());
    if (distance < 3.0)
        return;

    QVector<Effect> effects = m_extrudeItem->effects();
    if (m_extrudeEffect >= effects.size())
        return;

    Effect &effect = effects[m_extrudeEffect];
    effect.distance = qBound(1.0, distance, 800.0);
    effect.angle = std::atan2(delta.y(), delta.x()) * 180.0 / kPi;
    m_extrudeItem->setEffects(effects);
    emit scenePosChanged(scenePos);
    viewport()->update();
}

void CanvasView::extrudeRelease()
{
    if (!m_extrudeDragging)
        return;
    m_extrudeDragging = false;
    m_extrudeItem = nullptr;
    m_extrudeEffect = -1;
    emit endEdit();
    emit statusHint(QStringLiteral("Экструзия обновлена."));
    viewport()->update();
}

bool CanvasView::shadowPress(const QPointF &scenePos)
{
    VectorItem *item = itemAtScene(scenePos);
    if (!item) {
        emit statusHint(QStringLiteral("Щёлкните по объекту, чтобы задать тень."));
        return false;
    }
    scene()->clearSelection();
    item->setSelected(true);
    emit selectionChanged(1);

    VectorObject object = item->object();
    if (!object.shadow) {
        object.shadow = true;
        item->setObject(object);
    }
    emit beginEdit();
    m_shadowDragging = true;
    m_shadowItem = item;
    m_shadowStart = scenePos;
    m_shadowStartOffset = object.shadowOffset;
    emit statusHint(QStringLiteral(
        "Потяните от объекта, чтобы задать сдвиг тени; отпустите — готово."));
    viewport()->update();
    return true;
}

void CanvasView::shadowMove(const QPointF &scenePos)
{
    if (!m_shadowDragging || !m_shadowItem)
        return;
    const QPointF offset = m_shadowStartOffset + (scenePos - m_shadowStart);
    m_shadowItem->setShadowOffset(offset);
    emit scenePosChanged(scenePos);
    viewport()->update();
}

void CanvasView::shadowRelease()
{
    if (!m_shadowDragging)
        return;
    m_shadowDragging = false;
    m_shadowItem = nullptr;
    emit endEdit();
    emit statusHint(QStringLiteral("Тень обновлена."));
    viewport()->update();
}

void CanvasView::bringToFront()
{
    if (selectedVectorItems().isEmpty())
        return;
    emit beginEdit();
    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int layer = 0; layer < m_layers.size(); ++layer) {
        QList<VectorItem *> &items = perLayer[layer];
        items = itemsInLayer(layer);
        QList<VectorItem *> selected;
        for (VectorItem *item : items) {
            if (item->isSelected())
                selected.append(item);
        }
        for (VectorItem *item : selected)
            items.removeAll(item);
        for (VectorItem *item : selected)
            items.append(item);
    }
    applyLayerLayout(perLayer);
    emit endEdit();
}

void CanvasView::sendToBack()
{
    if (selectedVectorItems().isEmpty())
        return;
    emit beginEdit();
    QVector<QList<VectorItem *>> perLayer(m_layers.size());
    for (int layer = 0; layer < m_layers.size(); ++layer) {
        QList<VectorItem *> items = itemsInLayer(layer);
        QList<VectorItem *> selected;
        for (VectorItem *item : items) {
            if (item->isSelected())
                selected.append(item);
        }
        for (VectorItem *item : selected)
            items.removeAll(item);
        for (VectorItem *item : selected)
            items.prepend(item);
        perLayer[layer] = items;
    }
    applyLayerLayout(perLayer);
    emit endEdit();
}

void CanvasView::bringForward()
{
    raiseSelection();
}

void CanvasView::sendBackward()
{
    lowerSelection();
}

void CanvasView::flipSelection(bool horizontal)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;

    QRectF bounds;
    for (VectorItem *item : selected) {
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    }
    QPointF pivot = bounds.center();
    if (selected.size() > 1 && m_groupPivotValid)
        pivot = m_groupPivot;
    else if (selected.size() == 1)
        pivot = selected.first()->mapToScene(selected.first()->pivotPoint());

    emit beginEdit();
    int flipped = 0;
    for (VectorItem *item : selected) {
        const QPointF oldCenter = item->sceneBoundingRect().center();
        if (!item->flip(horizontal))
            continue;
        const QPointF newCenter = item->sceneBoundingRect().center();
        QPointF target = oldCenter;
        if (horizontal)
            target.setX(pivot.x() * 2.0 - oldCenter.x());
        else
            target.setY(pivot.y() * 2.0 - oldCenter.y());
        item->moveBy(target.x() - newCenter.x(), target.y() - newCenter.y());
        ++flipped;
    }
    emit endEdit();
    emit statusHint(flipped > 0
                        ? QStringLiteral("Отражено объектов: %1").arg(flipped)
                        : QStringLiteral("Текст не отражается на этом этапе."));
}

void CanvasView::rotateSelection(qreal degrees)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;

    QRectF bounds;
    for (VectorItem *item : selected) {
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    }
    QPointF pivot = bounds.center();
    if (selected.size() > 1 && m_groupPivotValid)
        pivot = m_groupPivot;
    else if (selected.size() == 1)
        pivot = selected.first()->mapToScene(selected.first()->pivotPoint());
    const qreal radians = degrees * M_PI / 180.0;

    emit beginEdit();
    for (VectorItem *item : selected) {
        const QPointF center = item->sceneBoundingRect().center();
        const QPointF shifted = center - pivot;
        const QPointF rotated(shifted.x() * std::cos(radians)
                                  - shifted.y() * std::sin(radians),
                              shifted.x() * std::sin(radians)
                                  + shifted.y() * std::cos(radians));
        const QPointF target = pivot + rotated;
        item->moveBy(target.x() - center.x(), target.y() - center.y());

        qreal angle = item->object().rotation + degrees;
        while (angle > 360.0)
            angle -= 360.0;
        while (angle <= -360.0)
            angle += 360.0;
        item->setRotationAngle(angle);
    }
    emit endEdit();
}

void CanvasView::beginGuideDrag(Qt::Orientation orientation)
{
    m_guidePreview = true;
    m_guideOrientation = orientation;
    m_guidePosition = 0.0;
}

void CanvasView::updateGuideDrag(Qt::Orientation orientation, qreal scenePos)
{
    m_guidePreview = true;
    m_guideOrientation = orientation;
    m_guidePosition = scenePos;
    viewport()->update();
}

void CanvasView::endGuideDrag(Qt::Orientation orientation, qreal scenePos)
{
    m_guidePreview = false;
    emit beginEdit();
    if (orientation == Qt::Vertical) {
        m_guidesX.append(scenePos);
        std::sort(m_guidesX.begin(), m_guidesX.end());
    } else {
        m_guidesY.append(scenePos);
        std::sort(m_guidesY.begin(), m_guidesY.end());
    }
    updateSnapSettings();
    emit endEdit();
    viewport()->update();
    emit statusHint(QStringLiteral("Направляющая добавлена."));
}

void CanvasView::setGuidesVisible(bool visible)
{
    m_guidesVisible = visible;
    viewport()->update();
}

void CanvasView::cancelGuideDrag()
{
    m_guidePreview = false;
    viewport()->update();
}

void CanvasView::clearGuides()
{
    if (m_guidesX.isEmpty() && m_guidesY.isEmpty())
        return;
    emit beginEdit();
    m_guidesX.clear();
    m_guidesY.clear();
    updateSnapSettings();
    emit endEdit();
    viewport()->update();
    emit statusHint(QStringLiteral("Направляющие удалены."));
}

void CanvasView::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
    updateSnapSettings();
}

void CanvasView::setGridStep(int step)
{
    m_gridStep = qBound(2, step, 400);
    updateSnapSettings();
    viewport()->update();
}

void CanvasView::setSnapThreshold(qreal threshold)
{
    m_snapThreshold = qBound(1.0, threshold, 60.0);
    updateSnapSettings();
}

void CanvasView::setSmartGuides(const QVector<QLineF> &lines)
{
    m_smartGuides = lines;
    viewport()->update();
}

namespace {

struct SnapCandidate {
    QPointF point;
    QString kind;
};

} // namespace

QPointF CanvasView::gridSnapPoint(const QPointF &scenePos) const
{
    const QRectF page = scene()->property("pageRect").toRectF();
    const QRectF area = page.isValid() ? page : QRectF(QPointF(0, 0), m_pageSize);
    const qreal step = qMax<qreal>(2.0, m_gridStep);

    if (m_gridType == 1) {
        const QPointF origin = area.center() + m_gridOrigin;
        const QPointF delta = scenePos - origin;
        const qreal radius = std::hypot(delta.x(), delta.y());
        const qreal snappedRadius = qRound(radius / step) * step;
        const qreal angleStep = qMax<qreal>(1.0, m_gridAngle);
        qreal degrees = std::atan2(delta.y(), delta.x()) * 180.0 / kPi;
        degrees = qRound(degrees / angleStep) * angleStep;
        const qreal radians = degrees * kPi / 180.0;
        return origin + QPointF(std::cos(radians) * snappedRadius,
                                std::sin(radians) * snappedRadius);
    }
    if (m_gridType == 2) {
        const QPointF origin = area.center() + m_gridOrigin;
        const qreal radians = 30.0 * kPi / 180.0;
        const QPointF u(std::cos(radians) * step, std::sin(radians) * step);
        const QPointF v(-std::cos(radians) * step, std::sin(radians) * step);
        const QPointF delta = scenePos - origin;
        // Solve delta = i*u + j*v.
        const qreal a = delta.x() / (step * std::cos(radians));
        const qreal b = delta.y() / (step * std::sin(radians));
        const qreal iIndex = qRound((a + b) / 2.0);
        const qreal jIndex = qRound((b - a) / 2.0);
        return origin + u * iIndex + v * jIndex;
    }

    const QPointF origin = area.topLeft() + m_gridOrigin;
    return QPointF(origin.x() + qRound((scenePos.x() - origin.x()) / step) * step,
                   origin.y() + qRound((scenePos.y() - origin.y()) / step) * step);
}

QPointF CanvasView::snapNodePoint(const QPointF &scenePos, VectorItem *exclude) const
{
    if (!m_snapEnabled)
        return scenePos;

    QVector<qreal> xs;
    QVector<qreal> ys;
    QVector<SnapCandidate> candidates;

    const QRectF page = scene()->property("pageRect").toRectF();
    if (page.isValid()) {
        xs << page.left() << page.center().x() << page.right();
        ys << page.top() << page.center().y() << page.bottom();
    }

    QVector<QRectF> nearbyBounds;
    const qreal nearbyDistance = 320.0;
    for (VectorItem *item : vectorItems()) {
        if (item == exclude || !item->isVisible())
            continue;
        const QRectF bounds = item->mapRectToScene(item->localBounds());
        if (!bounds.isValid() || bounds.width() < 0.2 || bounds.height() < 0.2)
            continue;

        const QPointF center = bounds.center();
        if (m_snapObjects) {
            xs << bounds.left() << center.x() << bounds.right();
            ys << bounds.top() << center.y() << bounds.bottom();
        }
        if ((center - scenePos).manhattanLength() > nearbyDistance * 2.0)
            continue;
        nearbyBounds.append(bounds);

        if (m_snapCenters)
            candidates.append({center, QStringLiteral("Центр")});
        if (m_snapMidpoints) {
            candidates.append({QPointF(bounds.center().x(), bounds.top()),
                               QStringLiteral("Середина")});
            candidates.append({QPointF(bounds.right(), bounds.center().y()),
                               QStringLiteral("Середина")});
            candidates.append({QPointF(bounds.center().x(), bounds.bottom()),
                               QStringLiteral("Середина")});
            candidates.append({QPointF(bounds.left(), bounds.center().y()),
                               QStringLiteral("Середина")});
        }
        if (m_snapEdges) {
            candidates.append({QPointF(qBound(bounds.left(), scenePos.x(), bounds.right()),
                                       bounds.top()), QStringLiteral("Край")});
            candidates.append({QPointF(qBound(bounds.left(), scenePos.x(), bounds.right()),
                                       bounds.bottom()), QStringLiteral("Край")});
            candidates.append({QPointF(bounds.left(),
                                       qBound(bounds.top(), scenePos.y(), bounds.bottom())),
                               QStringLiteral("Край")});
            candidates.append({QPointF(bounds.right(),
                                       qBound(bounds.top(), scenePos.y(), bounds.bottom())),
                               QStringLiteral("Край")});
        }
        if (m_snapTangents && item->object().type == ShapeType::Ellipse) {
            const qreal rx = bounds.width() / 2.0;
            const qreal ry = bounds.height() / 2.0;
            candidates.append({center + QPointF(rx, 0), QStringLiteral("Касание")});
            candidates.append({center + QPointF(-rx, 0), QStringLiteral("Касание")});
            candidates.append({center + QPointF(0, ry), QStringLiteral("Касание")});
            candidates.append({center + QPointF(0, -ry), QStringLiteral("Касание")});
        }
        if (m_snapNodes) {
            for (const QPainterPath::Element &element : item->pathElements()) {
                candidates.append({item->mapToScene(QPointF(element.x, element.y)),
                                   QStringLiteral("Узел")});
            }
        }
    }

    if (m_snapIntersections) {
        for (int a = 0; a < nearbyBounds.size(); ++a) {
            for (int b = a + 1; b < nearbyBounds.size(); ++b) {
                const QRectF first = nearbyBounds.at(a);
                const QRectF second = nearbyBounds.at(b);
                const auto addIntersection = [&candidates](const QPointF &point) {
                    candidates.append({point, QStringLiteral("Пересечение")});
                };
                for (qreal x : {first.left(), first.right()}) {
                    for (qreal y : {second.top(), second.bottom()}) {
                        if (x >= second.left() && x <= second.right()
                            && y >= first.top() && y <= first.bottom())
                            addIntersection(QPointF(x, y));
                    }
                }
                for (qreal x : {second.left(), second.right()}) {
                    for (qreal y : {first.top(), first.bottom()}) {
                        if (x >= first.left() && x <= first.right()
                            && y >= second.top() && y <= second.bottom())
                            addIntersection(QPointF(x, y));
                    }
                }
            }
        }
    }

    if (m_snapGuides) {
        xs << m_guidesX;
        ys << m_guidesY;
    }

    QString kind;
    QPointF snapPoint = scenePos;
    if (m_snapGrid) {
        const QPointF gridPoint = gridSnapPoint(scenePos);
        const QPointF delta = gridPoint - scenePos;
        if (std::hypot(delta.x(), delta.y()) <= m_snapThreshold) {
            snapPoint = gridPoint;
            kind = m_gridType == 1 ? QStringLiteral("Полярная сетка")
                                   : (m_gridType == 2 ? QStringLiteral("Изометрия")
                                                      : QStringLiteral("Сетка"));
        }
    }

    if (kind.isEmpty()) {
        qreal bestDistance = m_snapThreshold + 1.0;
        for (const SnapCandidate &candidate : candidates) {
            const qreal distance = QLineF(candidate.point, scenePos).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                snapPoint = candidate.point;
                kind = candidate.kind;
            }
        }
        if (bestDistance > m_snapThreshold)
            kind.clear();
    }

    if (kind.isEmpty()) {
        // Fall back to independent horizontal/vertical edge snapping.
        qreal bestX = scenePos.x();
        qreal bestY = scenePos.y();
        qreal bestDx = m_snapThreshold + 1.0;
        qreal bestDy = m_snapThreshold + 1.0;
        for (qreal target : xs) {
            if (qAbs(target - scenePos.x()) < bestDx) {
                bestDx = qAbs(target - scenePos.x());
                bestX = target;
            }
        }
        for (qreal target : ys) {
            if (qAbs(target - scenePos.y()) < bestDy) {
                bestDy = qAbs(target - scenePos.y());
                bestY = target;
            }
        }
        if (bestDx <= m_snapThreshold || bestDy <= m_snapThreshold) {
            snapPoint = QPointF(bestDx <= m_snapThreshold ? bestX : scenePos.x(),
                                bestDy <= m_snapThreshold ? bestY : scenePos.y());
            kind = QStringLiteral("Край");
        }
    }

    QVector<QLineF> lines;
    const QRectF area = sceneRect();
    if (!kind.isEmpty()) {
        lines.append(QLineF(snapPoint.x(), area.top(), snapPoint.x(), area.bottom()));
        lines.append(QLineF(area.left(), snapPoint.y(), area.right(), snapPoint.y()));
    }
    CanvasView *self = const_cast<CanvasView *>(this);
    self->setSmartGuides(lines);
    self->m_snapPoint = snapPoint;
    self->m_snapKind = kind;
    self->m_snapValid = !kind.isEmpty();

    return kind.isEmpty() ? scenePos : snapPoint;
}

void CanvasView::setGridType(int type)
{
    m_gridType = qBound(0, type, 2);
    updateSnapSettings();
    viewport()->update();
}

void CanvasView::setGridAngle(qreal degrees)
{
    m_gridAngle = qBound(1.0, degrees, 90.0);
    updateSnapSettings();
    viewport()->update();
}

void CanvasView::setGridOrigin(const QPointF &origin)
{
    m_gridOrigin = origin;
    updateSnapSettings();
    viewport()->update();
}

bool CanvasView::snapOption(const QString &key) const
{
    if (key == QLatin1String("grid")) return m_snapGrid;
    if (key == QLatin1String("guides")) return m_snapGuides;
    if (key == QLatin1String("objects")) return m_snapObjects;
    if (key == QLatin1String("nodes")) return m_snapNodes;
    if (key == QLatin1String("centers")) return m_snapCenters;
    if (key == QLatin1String("midpoints")) return m_snapMidpoints;
    if (key == QLatin1String("intersections")) return m_snapIntersections;
    if (key == QLatin1String("edges")) return m_snapEdges;
    if (key == QLatin1String("tangents")) return m_snapTangents;
    return false;
}

void CanvasView::setSnapOption(const QString &key, bool on)
{
    if (key == QLatin1String("grid")) m_snapGrid = on;
    else if (key == QLatin1String("guides")) m_snapGuides = on;
    else if (key == QLatin1String("objects")) m_snapObjects = on;
    else if (key == QLatin1String("nodes")) m_snapNodes = on;
    else if (key == QLatin1String("centers")) m_snapCenters = on;
    else if (key == QLatin1String("midpoints")) m_snapMidpoints = on;
    else if (key == QLatin1String("intersections")) m_snapIntersections = on;
    else if (key == QLatin1String("edges")) m_snapEdges = on;
    else if (key == QLatin1String("tangents")) m_snapTangents = on;
}

void CanvasView::setDrawingScale(qreal scale)
{
    m_drawingScale = qBound(0.01, scale, 1000.0);
    updateAllDimensions();
    viewport()->update();
    emit statusHint(QStringLiteral("Масштаб чертежа: 1:%1")
                        .arg(m_drawingScale, 0, 'g', 6));
}

void CanvasView::drawSnapHint(QPainter *painter)
{
    if (!m_snapValid || m_snapKind.isEmpty())
        return;
    if (m_tool != Tool::Node && m_tool != Tool::Dimension && !m_drawing)
        return;

    const qreal scale = transform().m11() > 0 ? transform().m11() : 1.0;
    const qreal size = 6.0 / scale;
    const qreal fontSize = 11.0 / scale;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(0xff, 0x66, 0x99), 1.3 / scale));
    painter->setBrush(QColor(0x1b, 0x1d, 0x21, 220));
    if (m_snapKind == QLatin1String("Центр"))
        painter->drawEllipse(m_snapPoint, size, size);
    else if (m_snapKind == QLatin1String("Середина"))
        painter->drawPolygon(QPolygonF({m_snapPoint + QPointF(0, -size),
                                        m_snapPoint + QPointF(size, size),
                                        m_snapPoint + QPointF(-size, size)}));
    else if (m_snapKind == QLatin1String("Пересечение"))
        painter->drawLine(m_snapPoint + QPointF(-size, -size),
                          m_snapPoint + QPointF(size, size)),
        painter->drawLine(m_snapPoint + QPointF(-size, size),
                          m_snapPoint + QPointF(size, -size));
    else if (m_snapKind == QLatin1String("Узел"))
        painter->drawRect(QRectF(m_snapPoint - QPointF(size, size) / 2.0,
                                 QSizeF(size, size)));
    else
        painter->drawEllipse(m_snapPoint, size * 0.8, size * 0.8);

    QFont font = painter->font();
    font.setPixelSize(qMax(6, qRound(fontSize)));
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const QRectF textRect = metrics.boundingRect(m_snapKind);
    const QRectF background(m_snapPoint.x() + 9.0 / scale,
                            m_snapPoint.y() - 24.0 / scale,
                            textRect.width() + 8.0 / scale,
                            textRect.height() + 4.0 / scale);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0x1b, 0x1d, 0x21, 225));
    painter->drawRoundedRect(background, 3.0 / scale, 3.0 / scale);
    painter->setPen(QColor(0xff, 0x9a, 0xc0));
    painter->drawText(background.adjusted(4.0 / scale, 2.0 / scale, 0, 0),
                      Qt::AlignLeft | Qt::AlignVCenter, m_snapKind);
    painter->restore();
}



void CanvasView::updateSnapSettings()
{
    for (VectorItem *item : vectorItems())
        item->setSnapSettings(m_snapEnabled, m_gridStep, m_guidesX, m_guidesY,
                              m_snapThreshold, m_gridType, m_gridOrigin,
                              m_gridAngle);
}

void CanvasView::setItemLocked(VectorItem *item, bool locked)
{
    if (!item)
        return;
    emit beginEdit();
    item->setLocked(locked);
    emit endEdit();
    emit statusHint(locked ? QStringLiteral("Объект заблокирован.")
                           : QStringLiteral("Объект разблокирован."));
}

int CanvasView::topGroupOf(int group) const
{
    int current = group;
    for (int guard = 0; guard < 100 && current > 0; ++guard) {
        int parent = 0;
        for (const VectorGroup &entry : m_groups) {
            if (entry.id == current) {
                parent = entry.parent;
                break;
            }
        }
        if (parent <= 0)
            return current;
        current = parent;
    }
    return current;
}

QList<VectorItem *> CanvasView::itemsInGroup(int topGroup) const
{
    QList<VectorItem *> result;
    for (VectorItem *item : vectorItems()) {
        if (item->groupIndex() > 0
            && topGroupOf(item->groupIndex()) == topGroup)
            result.append(item);
    }
    return result;
}

void CanvasView::syncClones()
{
    QHash<int, VectorItem *> byId;
    for (VectorItem *item : vectorItems())
        byId.insert(item->object().id, item);

    const QList<VectorItem *> items = vectorItems();
    for (VectorItem *clone : items) {
        const VectorObject cloneObject = clone->object();
        if (cloneObject.masterId <= 0)
            continue;
        VectorItem *master = byId.value(cloneObject.masterId, nullptr);
        if (!master)
            continue;

        VectorObject updated = master->object();
        updated.id = cloneObject.id;
        updated.masterId = cloneObject.masterId;
        updated.pos = cloneObject.pos;
        updated.layer = cloneObject.layer;
        updated.group = cloneObject.group;
        updated.locked = cloneObject.locked;
        clone->setObject(updated);
    }
}

void CanvasView::scaleSelectionTo(const QRectF &targetBounds, qreal opacity)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.size() < 2 || targetBounds.width() < 1.0
        || targetBounds.height() < 1.0) {
        return;
    }

    QRectF sourceBounds;
    for (VectorItem *item : selected) {
        const QRectF visual = item->mapRectToScene(item->localBounds());
        sourceBounds = sourceBounds.isNull() ? visual : sourceBounds.united(visual);
    }
    if (sourceBounds.width() < 0.5 || sourceBounds.height() < 0.5)
        return;

    const qreal sx = targetBounds.width() / sourceBounds.width();
    const qreal sy = targetBounds.height() / sourceBounds.height();

    emit beginEdit();
    for (VectorItem *item : selected) {
        const QRectF oldVisual = item->mapRectToScene(item->localBounds());
        const QPointF oldCenter = oldVisual.center();
        const QSizeF oldSize = item->object().localBounds().size();
        item->setFrameSize(QSizeF(qMax<qreal>(1.0, oldSize.width() * sx),
                                  qMax<qreal>(1.0, oldSize.height() * sy)));
        const QPointF newCenter = item->mapToScene(item->object().localBounds().center());
        const QPointF wanted(targetBounds.left()
                                 + (oldCenter.x() - sourceBounds.left()) * sx,
                             targetBounds.top()
                                 + (oldCenter.y() - sourceBounds.top()) * sy);
        item->moveBy(wanted.x() - newCenter.x(), wanted.y() - newCenter.y());
        if (opacity >= 0.0)
            item->setObjectOpacity(opacity);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Размер группы изменён."));
}

void CanvasView::duplicateSelection(int count, const QPointF &offset)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty() || count < 1)
        return;

    QVector<VectorObject> originals;
    for (VectorItem *item : selected)
        originals.append(item->object());

    emit beginEdit();
    scene()->clearSelection();
    for (int step = 1; step <= count; ++step) {
        for (VectorObject object : originals) {
            object.pos += offset * qreal(step);
            object.id = 0;
            object.masterId = 0;
            object.group = 0;
            object.locked = false;
            if (object.type == ShapeType::Dimension) {
                object.dimAssociative = false;
                object.dimRefA = 0;
                object.dimRefB = 0;
                object.dimRefC = 0;
            }
            VectorItem *copy = addVectorObject(object, false, false);
            if (copy)
                copy->setSelected(true);
        }
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Создано копий: %1").arg(count * originals.size()));
}

void CanvasView::cloneSelection(const QPointF &offset)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объект для клонирования."));
        return;
    }

    QVector<VectorObject> originals;
    for (VectorItem *item : selected)
        originals.append(item->object());

    emit beginEdit();
    scene()->clearSelection();
    for (VectorObject object : originals) {
        object.pos += offset;
        object.masterId = object.id;
        object.id = 0;
        object.group = 0;
        object.locked = false;
        VectorItem *clone = addVectorObject(object, false, false);
        if (clone)
            clone->setSelected(true);
    }
    emit endEdit();
    syncClones();
    emit statusHint(QStringLiteral("Создано клонов: %1").arg(originals.size()));
}

void CanvasView::skewSelection(qreal angleX, qreal angleY)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;

    QRectF bounds;
    for (VectorItem *item : selected) {
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    }
    const QPointF pivot = bounds.center();
    const qreal ax = std::tan(qBound(-75.0, angleX, 75.0) * M_PI / 180.0);
    const qreal ay = std::tan(qBound(-75.0, angleY, 75.0) * M_PI / 180.0);

    emit beginEdit();
    int skewed = 0;
    for (VectorItem *item : selected) {
        if (!item->applySkew(angleX, angleY))
            continue;
        ++skewed;
        if (selected.size() > 1) {
            const QPointF pos = item->pos();
            item->moveBy((pos.y() - pivot.y()) * ax, (pos.x() - pivot.x()) * ay);
        }
    }
    emit endEdit();
    emit statusHint(skewed > 0
                        ? QStringLiteral("Наклонено объектов: %1").arg(skewed)
                        : QStringLiteral("Текст пока не наклоняется."));
}

QList<int> CanvasView::childGroupsOf(int parentGroup) const
{
    QList<int> result;
    for (const VectorGroup &group : m_groups) {
        if (group.parent == parentGroup)
            result.append(group.id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

QList<VectorItem *> CanvasView::itemsDirectlyInGroup(int group) const
{
    QList<VectorItem *> result;
    for (VectorItem *item : vectorItems()) {
        if (item->groupIndex() == group)
            result.append(item);
    }
    return result;
}

void CanvasView::selectGroupItems(int group)
{
    if (group <= 0)
        return;
    const int top = topGroupOf(group);
    m_insideGroup = false;
    m_syncingSelection = true;
    scene()->clearSelection();
    for (VectorItem *item : itemsInGroup(top))
        item->setSelected(true);
    m_syncingSelection = false;
    emit selectionChanged(selectedVectorItems().size());
    emit statusHint(QStringLiteral("Выбрана группа."));
}

void CanvasView::applyBooleanOperation(BooleanOperation operation)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    struct Source {
        VectorItem *item = nullptr;
        QPainterPath path;
        VectorObject object;
    };
    QVector<Source> sources;
    for (VectorItem *item : selected) {
        const VectorObject object = item->object();
        QPainterPath path;
        if (object.type == ShapeType::Text) {
            path = item->mapToScene(buildTextPath(object));
        } else if (!object.path.isEmpty()) {
            path = item->mapToScene(object.path);
        } else {
            continue;
        }
        if (path.isEmpty())
            continue;
        sources.append({item, path, object});
    }

    if (sources.size() < 2) {
        emit statusHint(QStringLiteral("Для логической операции нужно минимум два контура."));
        return;
    }

    QVector<QPainterPath> results;
    switch (operation) {
    case BooleanOperation::Union: {
        QPainterPath result = sources.first().path;
        for (int i = 1; i < sources.size(); ++i)
            result = result.united(sources.at(i).path);
        results.append(result);
        break;
    }
    case BooleanOperation::Difference: {
        QPainterPath result = sources.first().path;
        for (int i = 1; i < sources.size(); ++i)
            result = result.subtracted(sources.at(i).path);
        results.append(result);
        break;
    }
    case BooleanOperation::Intersection: {
        QPainterPath result = sources.first().path;
        for (int i = 1; i < sources.size(); ++i)
            result = result.intersected(sources.at(i).path);
        results.append(result);
        break;
    }
    case BooleanOperation::Exclude: {
        QPainterPath result = sources.first().path;
        for (int i = 1; i < sources.size(); ++i) {
            const QPainterPath other = sources.at(i).path;
            const QPainterPath overlap = result.intersected(other);
            result = result.united(other).subtracted(overlap);
        }
        results.append(result);
        break;
    }
    case BooleanOperation::Divide: {
        QVector<QPainterPath> regions;
        for (const Source &source : sources) {
            if (regions.isEmpty()) {
                regions.append(source.path);
                continue;
            }
            QVector<QPainterPath> next;
            QPainterPath remaining = source.path;
            for (const QPainterPath &region : regions) {
                const QPainterPath outside = region.subtracted(source.path);
                const QPainterPath inside = region.intersected(source.path);
                if (!outside.isEmpty())
                    next.append(outside);
                if (!inside.isEmpty())
                    next.append(inside);
                remaining = remaining.subtracted(region);
            }
            if (!remaining.isEmpty())
                next.append(remaining);
            regions = next;
        }
        results = regions;
        break;
    }
    case BooleanOperation::Trim: {
        const QPainterPath top = sources.last().path;
        for (int i = 0; i < sources.size(); ++i) {
            QPainterPath result = sources.at(i).path;
            if (i != sources.size() - 1)
                result = result.subtracted(top);
            if (!result.isEmpty())
                results.append(result);
        }
        break;
    }
    }

    if (results.isEmpty()) {
        emit statusHint(QStringLiteral("Операция не дала результата."));
        return;
    }

    VectorObject baseObject = sources.first().object;
    const int layer = baseObject.layer;

    emit beginEdit();
    scene()->clearSelection();
    for (const Source &source : sources) {
        scene()->removeItem(source.item);
        delete source.item;
    }

    for (const QPainterPath &path : results) {
        if (path.isEmpty())
            continue;
        VectorObject object = baseObject;
        object.type = ShapeType::Bezier;
        object.path = path;
        object.pos = QPointF(0, 0);
        object.rotation = 0;
        object.text.clear();
        object.id = 0;
        object.masterId = 0;
        object.group = 0;
        object.locked = false;
        object.layer = layer;
        VectorItem *item = addVectorObject(object, false, true);
        if (item)
            item->setSelected(true);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Логическая операция выполнена: объектов %1")
                        .arg(results.size()));
}

void CanvasView::convertSelectionToCurves()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объекты для преобразования."));
        return;
    }

    emit beginEdit();
    int converted = 0;
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.type == ShapeType::Image)
            continue;

        if (object.type == ShapeType::Text) {
            const QPainterPath textPath = buildTextPath(object);
            if (textPath.isEmpty())
                continue;
            object.path = textPath;
            object.text.clear();
        } else if (!object.path.isEmpty()) {
            // Already a path, but strip shape parameters so it is a free curve.
            object.cornerRadius = 0;
        } else {
            continue;
        }

        object.type = ShapeType::Bezier;
        object.id = 0;
        object.masterId = 0;
        scene()->removeItem(item);
        delete item;
        VectorItem *curve = addVectorObject(object, false, true);
        if (curve)
            curve->setSelected(true);
        ++converted;
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Преобразовано в кривые: %1").arg(converted));
}

void CanvasView::simplifySelectedPath()
{
    if (!m_nodeItem || !m_nodeItem->hasPath()) {
        const QList<VectorItem *> selected = selectedVectorItems();
        for (VectorItem *item : selected) {
            if (item->hasPath()) {
                m_nodeItem = item;
                break;
            }
        }
    }
    if (!m_nodeItem)
        return;

    QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
    if (elements.size() < 3)
        return;

    QVector<QPainterPath::Element> simplified;
    for (int i = 0; i < elements.size(); ++i) {
        const auto &element = elements.at(i);
        if (!simplified.isEmpty()) {
            const auto &previous = simplified.last();
            const qreal distance = QLineF(previous.x, previous.y,
                                          element.x, element.y).length();
            if (distance < 0.05
                && element.type != QPainterPath::CurveToElement)
                continue;
        }
        simplified.append(element);
    }

    // Remove collinear line anchors.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i + 1 < simplified.size(); ++i) {
            if (simplified.at(i).type != QPainterPath::LineToElement
                || simplified.at(i + 1).type != QPainterPath::LineToElement)
                continue;
            const QPointF a(simplified.at(i - 1).x, simplified.at(i - 1).y);
            const QPointF b(simplified.at(i).x, simplified.at(i).y);
            const QPointF c(simplified.at(i + 1).x, simplified.at(i + 1).y);
            const QPointF ab = b - a;
            const QPointF ac = c - a;
            const qreal cross = qAbs(ab.x() * ac.y() - ab.y() * ac.x());
            if (cross < 0.5 * QLineF(a, c).length()) {
                simplified.removeAt(i);
                changed = true;
                break;
            }
        }
    }

    if (simplified.size() == elements.size()) {
        emit statusHint(QStringLiteral("Лишних узлов не найдено."));
        return;
    }

    emit beginEdit();
    m_nodeItem->setPathElements(simplified);
    m_selectedNodeElements.clear();
    emit endEdit();
    emit statusHint(QStringLiteral("Удалено узлов: %1")
                        .arg(elements.size() - simplified.size()));
}

void CanvasView::applyKnifeCut(const QLineF &cut)
{
    if (QLineF(cut.p1(), cut.p2()).length() < 2.0)
        return;

    QList<VectorItem *> targets = selectedVectorItems();
    if (targets.isEmpty())
        targets = vectorItems();

    const QPointF d = (cut.p2() - cut.p1()) / cut.length();
    const QPointF n(-d.y(), d.x());
    const qreal huge = qMax(pageSize().width(), pageSize().height()) * 8.0 + 2000.0;
    const QPointF a = cut.p1() - d * huge;
    const QPointF b = cut.p2() + d * huge;

    QPainterPath clipA;
    QPolygonF polyA;
    polyA << a + n * huge << b + n * huge << b << a;
    clipA.addPolygon(polyA);
    clipA.closeSubpath();

    QPainterPath clipB;
    QPolygonF polyB;
    polyB << a - n * huge << b - n * huge << b << a;
    clipB.addPolygon(polyB);
    clipB.closeSubpath();

    int cutCount = 0;
    emit beginEdit();
    for (VectorItem *item : targets) {
        const VectorObject object = item->object();
        if (object.type == ShapeType::Text || object.type == ShapeType::Image
            || object.path.isEmpty())
            continue;

        const QPainterPath scenePath = item->mapToScene(object.path);
        if (!scenePath.boundingRect().adjusted(-10, -10, 10, 10).intersects(
                QRectF(cut.p1(), cut.p2()).normalized().adjusted(-1, -1, 1, 1)))
            continue;

        const QPainterPath partA = scenePath.intersected(clipA);
        const QPainterPath partB = scenePath.intersected(clipB);
        if (partA.isEmpty() || partB.isEmpty())
            continue;

        VectorObject first = object;
        first.type = ShapeType::Bezier;
        first.path = partA;
        first.pos = QPointF(0, 0);
        first.rotation = 0;
        first.text.clear();
        first.id = 0;
        first.masterId = 0;
        first.group = 0;
        VectorObject second = first;
        second.path = partB;

        scene()->removeItem(item);
        delete item;
        addVectorObject(first, false, true);
        addVectorObject(second, false, true);
        ++cutCount;
    }
    emit endEdit();
    emit statusHint(cutCount > 0
                        ? QStringLiteral("Разрезано объектов: %1").arg(cutCount)
                        : QStringLiteral("Линия ножа не пересекла замкнутые контуры."));
}

void CanvasView::applyEraserStroke(const QVector<QPointF> &points)
{
    if (points.size() < 2)
        return;

    QPainterPath stroke = buildFreehandPath(points);
    QPainterPathStroker stroker;
    stroker.setWidth(18.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    const QPainterPath brush = stroker.createStroke(stroke);

    QList<VectorItem *> targets = selectedVectorItems();
    if (targets.isEmpty())
        targets = vectorItems();

    int erased = 0;
    emit beginEdit();
    for (VectorItem *item : targets) {
        const VectorObject object = item->object();
        if (object.type == ShapeType::Text || object.type == ShapeType::Image
            || object.path.isEmpty())
            continue;
        const QPainterPath scenePath = item->mapToScene(object.path);
        if (!scenePath.boundingRect().intersects(brush.boundingRect()))
            continue;

        const QPainterPath result = scenePath.subtracted(brush);
        scene()->removeItem(item);
        delete item;
        if (result.isEmpty())
            continue;

        VectorObject updated = object;
        updated.type = ShapeType::Bezier;
        updated.path = result;
        updated.pos = QPointF(0, 0);
        updated.rotation = 0;
        updated.text.clear();
        updated.id = 0;
        updated.masterId = 0;
        updated.group = 0;
        addVectorObject(updated, false, true);
        ++erased;
    }
    emit endEdit();
    emit statusHint(erased > 0
                        ? QStringLiteral("Стёрто объектов: %1").arg(erased)
                        : QStringLiteral("Под ластиком нет замкнутых контуров."));
}

void CanvasView::convertStrokeToPath()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объекты с обводкой."));
        return;
    }

    emit beginEdit();
    int converted = 0;
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.type == ShapeType::Image || object.strokeWidth <= 0.0
            || !object.hasStroke)
            continue;

        QPainterPath source = object.path;
        if (object.type == ShapeType::Text)
            source = buildTextPath(object);
        if (source.isEmpty())
            continue;

        QPainterPathStroker stroker;
        stroker.setWidth(object.strokeWidth);
        stroker.setCapStyle(object.capStyle == 0 ? Qt::FlatCap
                                                  : (object.capStyle == 1
                                                         ? Qt::SquareCap
                                                         : Qt::RoundCap));
        stroker.setJoinStyle(object.joinStyle == 0 ? Qt::MiterJoin
                                                    : (object.joinStyle == 1
                                                           ? Qt::BevelJoin
                                                           : Qt::RoundJoin));
        stroker.setMiterLimit(object.miterLimit);
        if (!object.dashPattern.isEmpty())
            stroker.setDashPattern(object.dashPattern);

        const QPainterPath outline = stroker.createStroke(source);
        if (outline.isEmpty())
            continue;

        object.type = ShapeType::Bezier;
        object.path = outline;
        object.fill = object.stroke;
        object.hasFill = true;
        object.fillStyle = 0;
        object.hasStroke = false;
        object.strokeWidth = 0;
        object.dashPattern.clear();
        object.text.clear();
        object.id = 0;
        object.masterId = 0;

        scene()->removeItem(item);
        delete item;
        VectorItem *convertedItem = addVectorObject(object, false, true);
        if (convertedItem)
            convertedItem->setSelected(true);
        ++converted;
    }
    emit endEdit();
    emit statusHint(converted > 0
                        ? QStringLiteral("Обводка преобразована: %1").arg(converted)
                        : QStringLiteral("Нет объектов с обводкой."));
}


QVector<QPainterPath> CanvasView::regionize(const QVector<QPainterPath> &sources) const
{
    QVector<QPainterPath> regions;
    for (const QPainterPath &source : sources) {
        if (regions.isEmpty()) {
            regions.append(source);
            continue;
        }
        QVector<QPainterPath> next;
        QPainterPath remaining = source;
        for (const QPainterPath &region : regions) {
            const QPainterPath outside = region.subtracted(source);
            const QPainterPath inside = region.intersected(source);
            if (!outside.isEmpty())
                next.append(outside);
            if (!inside.isEmpty())
                next.append(inside);
            remaining = remaining.subtracted(region);
        }
        if (!remaining.isEmpty())
            next.append(remaining);
        regions = next;
    }
    return regions;
}

bool CanvasView::shapeBuilderPress(const QPointF &scenePos)
{
    m_shapeSources.clear();
    m_shapeRegions.clear();
    m_shapeStyles.clear();

    QList<VectorItem *> candidates = selectedVectorItems();
    if (candidates.size() < 2)
        candidates = vectorItems();

    QVector<QPainterPath> paths;
    for (VectorItem *item : candidates) {
        if (!item->isVisible() || item->object().locked || layerLocked(layerOf(item)))
            continue;
        const VectorObject object = item->object();
        QPainterPath path;
        if (object.type == ShapeType::Text)
            path = item->mapToScene(buildTextPath(object));
        else if (!object.path.isEmpty())
            path = item->mapToScene(object.path);
        if (path.isEmpty())
            continue;
        paths.append(path);
        m_shapeSources.append(item);
    }

    if (paths.size() < 2) {
        emit statusHint(QStringLiteral("Создание фигур: нужно минимум два объекта."));
        return false;
    }

    m_shapeRegions = regionize(paths);
    for (const QPainterPath &region : m_shapeRegions) {
        VectorObject style = m_shapeSources.first()->object();
        for (int i = m_shapeSources.size() - 1; i >= 0; --i) {
            if (region.subtracted(paths.at(i)).isEmpty()) {
                style = m_shapeSources.at(i)->object();
                break;
            }
        }
        m_shapeStyles.append(style);
    }

    m_shapeDragging = true;
    m_shapeHasRegions = true;
    m_shapeStart = scenePos;
    m_shapeCurrent = scenePos;
    viewport()->update();
    return true;
}

void CanvasView::shapeBuilderMove(const QPointF &scenePos)
{
    if (!m_shapeDragging)
        return;
    m_shapeCurrent = scenePos;
    viewport()->update();
}

void CanvasView::shapeBuilderRelease(const QPointF &scenePos, bool deleteRegions)
{
    if (!m_shapeDragging)
        return;
    m_shapeCurrent = scenePos;
    m_shapeDragging = false;

    const bool click = QLineF(m_shapeStart, m_shapeCurrent).length() < 3.0;
    const QRectF rect = QRectF(m_shapeStart, m_shapeCurrent).normalized().adjusted(-0.5, -0.5, 0.5, 0.5);

    QVector<int> chosen;
    for (int i = 0; i < m_shapeRegions.size(); ++i) {
        const QPainterPath &region = m_shapeRegions.at(i);
        if (click ? region.contains(m_shapeStart) : region.intersects(rect))
            chosen.append(i);
    }

    if (chosen.isEmpty()) {
        m_shapeRegions.clear();
        m_shapeStyles.clear();
        m_shapeSources.clear();
        m_shapeHasRegions = false;
        viewport()->update();
        emit statusHint(QStringLiteral("Области не выбраны."));
        return;
    }

    QPainterPath merged;
    VectorObject mergedStyle = m_shapeStyles.value(chosen.first());
    for (int index : chosen) {
        if (merged.isEmpty())
            merged = m_shapeRegions.at(index);
        else
            merged = merged.united(m_shapeRegions.at(index));
    }

    emit beginEdit();
    for (VectorItem *item : m_shapeSources) {
        scene()->removeItem(item);
        delete item;
    }

    int created = 0;
    const int layer = mergedStyle.layer;
    if (!deleteRegions && !merged.isEmpty()) {
        VectorObject object = mergedStyle;
        object.type = ShapeType::Bezier;
        object.path = merged;
        object.pos = QPointF(0, 0);
        object.rotation = 0;
        object.text.clear();
        object.id = 0;
        object.masterId = 0;
        object.group = 0;
        object.layer = layer;
        if (addVectorObject(object, false, true))
            ++created;
    }

    for (int i = 0; i < m_shapeRegions.size(); ++i) {
        if (chosen.contains(i))
            continue;
        if (m_shapeRegions.at(i).isEmpty())
            continue;
        VectorObject object = m_shapeStyles.at(i);
        object.type = ShapeType::Bezier;
        object.path = m_shapeRegions.at(i);
        object.pos = QPointF(0, 0);
        object.rotation = 0;
        object.text.clear();
        object.id = 0;
        object.masterId = 0;
        object.group = 0;
        object.layer = layer;
        addVectorObject(object, false, false);
        ++created;
    }

    emit endEdit();
    m_shapeRegions.clear();
    m_shapeStyles.clear();
    m_shapeSources.clear();
    m_shapeHasRegions = false;
    viewport()->update();
    emit statusHint(deleteRegions
                        ? QStringLiteral("Области удалены: %1").arg(chosen.size())
                        : QStringLiteral("Области объединены: %1").arg(chosen.size()));
}

void CanvasView::drawShapeBuilderOverlay(QPainter *painter)
{
    if (!m_shapeHasRegions)
        return;
    const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
    const bool click = QLineF(m_shapeStart, m_shapeCurrent).length() < 3.0;
    const QRectF rect = QRectF(m_shapeStart, m_shapeCurrent).normalized().adjusted(-0.5, -0.5, 0.5, 0.5);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_shapeRegions.size(); ++i) {
        const QPainterPath &region = m_shapeRegions.at(i);
        const bool hovered = click ? region.contains(m_shapeStart) : region.intersects(rect);
        painter->setPen(QPen(hovered ? QColor(0xff, 0xb3, 0x47) : QColor(0x7f, 0xae, 0xf7, 160),
                             (hovered ? 1.6 : 0.9) / s));
        painter->setBrush(hovered ? QColor(0xff, 0xb3, 0x47, 90)
                                  : QColor(0x7f, 0xae, 0xf7, 24));
        painter->drawPath(region);
    }
    if (!click) {
        QPen marquee(QColor(0xff, 0xb3, 0x47), 0.0);
        marquee.setStyle(Qt::DashLine);
        painter->setPen(marquee);
        painter->setBrush(QColor(0xff, 0xb3, 0x47, 26));
        painter->drawRect(rect);
    }
    painter->restore();
}

void CanvasView::applyTextOnPath()
{
    VectorItem *textItem = nullptr;
    VectorItem *pathItem = nullptr;
    for (VectorItem *item : selectedVectorItems()) {
        if (item->object().type == ShapeType::Text && !textItem)
            textItem = item;
        else if (item->hasPath() && !pathItem)
            pathItem = item;
    }
    if (!textItem || !pathItem) {
        emit statusHint(QStringLiteral("Выберите текст и кривую (текст + контур)."));
        return;
    }

    const VectorObject pathObject = pathItem->object();
    const QPainterPath scenePath =
        pathItem->mapToScene(pathObject.type == ShapeType::Text
                                 ? buildTextPath(pathObject)
                                 : pathObject.path);
    if (scenePath.isEmpty()) {
        emit statusHint(QStringLiteral("Контур пуст."));
        return;
    }

    VectorObject textObject = textItem->object();
    const QPainterPath localPath = textItem->mapFromScene(scenePath);
    textObject.textOnPath = true;
    textObject.textInShape = false;
    textObject.textPathCurve = localPath;
    textObject.textPathOffset = 0.0;
    textObject.textPathReverse = false;
    textObject.textBox = localPath.boundingRect().adjusted(-10, -30, 10, 30);

    emit beginEdit();
    textItem->setObject(textObject);
    emit endEdit();
    emit statusHint(QStringLiteral("Текст размещён по контуру."));
}

void CanvasView::applyTextInShape()
{
    VectorItem *textItem = nullptr;
    VectorItem *shapeItem = nullptr;
    for (VectorItem *item : selectedVectorItems()) {
        if (item->object().type == ShapeType::Text && !textItem)
            textItem = item;
        else if (item->hasPath() && !shapeItem)
            shapeItem = item;
    }
    if (!textItem || !shapeItem) {
        emit statusHint(QStringLiteral("Выберите текст и фигуру-контейнер."));
        return;
    }

    const VectorObject shapeObject = shapeItem->object();
    const QPainterPath sceneShape = shapeItem->mapToScene(shapeObject.path);
    if (sceneShape.isEmpty()) {
        emit statusHint(QStringLiteral("Фигура пуста."));
        return;
    }

    VectorObject textObject = textItem->object();
    const QPainterPath localShape = textItem->mapFromScene(sceneShape);
    textObject.textInShape = true;
    textObject.textOnPath = false;
    textObject.textShape = localShape;
    const QRectF box = localShape.boundingRect().adjusted(6, 6, -6, -6);
    if (box.width() > 4.0 && box.height() > 4.0) {
        textObject.textBox = box;
    }
    emit beginEdit();
    textItem->setObject(textObject);
    emit endEdit();
    emit statusHint(QStringLiteral("Текст помещён в фигуру."));
}

void CanvasView::detachTextSpecial()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    int changed = 0;
    emit beginEdit();
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.type != ShapeType::Text)
            continue;
        if (!object.textOnPath && !object.textInShape)
            continue;
        object.textOnPath = false;
        object.textInShape = false;
        object.textPathCurve = QPainterPath();
        object.textShape = QPainterPath();
        item->setObject(object);
        ++changed;
    }
    emit endEdit();
    emit statusHint(changed > 0
                        ? QStringLiteral("Текст отсоединён от контура: %1").arg(changed)
                        : QStringLiteral("Нет текста по контуру или в фигуре."));
}

void CanvasView::applyContour(int steps, qreal distance, int direction,
                              const QColor &color, bool filled)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объект для контура."));
        return;
    }
    steps = qBound(1, steps, 40);
    distance = qBound(0.5, distance, 400.0);

    emit beginEdit();
    int created = 0;
    for (VectorItem *item : selected) {
        const VectorObject source = item->object();
        QPainterPath base = source.type == ShapeType::Text ? buildTextPath(source)
                                                           : source.path;
        if (base.isEmpty())
            continue;

        for (int i = 1; i <= steps; ++i) {
            const qreal width = 2.0 * distance * i;
            QPainterPathStroker stroker;
            stroker.setWidth(width);
            stroker.setJoinStyle(Qt::RoundJoin);
            stroker.setCapStyle(Qt::RoundCap);
            const QPainterPath ring = stroker.createStroke(base);

            QVector<QPainterPath> variants;
            if (direction == 0 || direction == 2)
                variants.append(base.united(ring));
            if (direction == 1 || direction == 2)
                variants.append(base.subtracted(ring));

            for (const QPainterPath &variant : variants) {
                if (variant.isEmpty())
                    continue;
                VectorObject object = source;
                object.type = ShapeType::Bezier;
                object.path = variant;
                object.text.clear();
                object.id = 0;
                object.masterId = 0;
                object.group = 0;
                if (filled) {
                    object.hasFill = true;
                    object.fillStyle = 0;
                    object.fill = color;
                    object.hasStroke = false;
                } else {
                    object.hasFill = false;
                    object.hasStroke = true;
                    object.stroke = color;
                    object.strokeWidth = qMax<qreal>(1.0, source.strokeWidth);
                    object.dashPattern.clear();
                }
                addVectorObject(object, false, false);
                ++created;
            }
        }
    }
    emit endEdit();
    emit statusHint(created > 0
                        ? QStringLiteral("Создано контуров: %1").arg(created)
                        : QStringLiteral("Контур не создан: нет подходящих объектов."));
}

bool CanvasView::envelopePress(const QPointF &scenePos)
{
    const qreal tolerance = 12.0 / (m_zoom > 0.0 ? m_zoom : 1.0);

    if (m_envelopeItem) {
        int best = -1;
        qreal bestDistance = tolerance;
        for (int i = 0; i < m_envelopeGrid.size(); ++i) {
            const QPointF handle = m_envelopeItem->mapToScene(m_envelopeGrid.at(i));
            const qreal d = QLineF(handle, scenePos).length();
            if (d <= bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        if (best >= 0) {
            m_envelopeDragIndex = best;
            m_envelopeEditing = true;
            emit beginEdit();
            return true;
        }
        return false;
    }

    QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        VectorItem *hit = itemAtScene(scenePos);
        if (hit)
            selected.append(hit);
    }
    VectorItem *target = nullptr;
    for (VectorItem *item : selected) {
        if (item->hasPath() && item->isVisible()) {
            target = item;
            break;
        }
    }
    if (!target) {
        emit statusHint(QStringLiteral("Конверт: выберите векторный объект."));
        return false;
    }

    scene()->clearSelection();
    target->setSelected(true);
    m_envelopeItem = target;
    m_envelopeOriginal = target->pathElements();
    m_envelopeBounds = target->localBounds();
    if (m_envelopeBounds.width() < 1.0 || m_envelopeBounds.height() < 1.0) {
        resetEnvelope();
        emit statusHint(QStringLiteral("Конверт: объект слишком мал."));
        return false;
    }

    m_envelopeGrid.clear();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            m_envelopeGrid.append(QPointF(
                m_envelopeBounds.left()
                    + m_envelopeBounds.width() * column / 3.0,
                m_envelopeBounds.top()
                    + m_envelopeBounds.height() * row / 3.0));
        }
    }
    m_envelopeDragIndex = -1;
    viewport()->update();
    emit statusHint(QStringLiteral("Конверт: перетаскивайте узлы сетки, "
                                   "Shift+E повторно — сброс."));
    return true;
}

void CanvasView::envelopeMove(const QPointF &scenePos)
{
    if (!m_envelopeItem || m_envelopeDragIndex < 0
        || m_envelopeDragIndex >= m_envelopeGrid.size())
        return;
    m_envelopeGrid[m_envelopeDragIndex] = m_envelopeItem->mapFromScene(scenePos);
    remapEnvelope();
    viewport()->update();
}

void CanvasView::remapEnvelope()
{
    if (!m_envelopeItem || m_envelopeGrid.size() != 16
        || m_envelopeOriginal.isEmpty())
        return;

    const QRectF bounds = m_envelopeBounds;
    const qreal width = qMax<qreal>(0.001, bounds.width());
    const qreal height = qMax<qreal>(0.001, bounds.height());

    auto basis = [](qreal t, int i) {
        const qreal mt = 1.0 - t;
        switch (i) {
        case 0: return mt * mt * mt;
        case 1: return 3.0 * t * mt * mt;
        case 2: return 3.0 * t * t * mt;
        default: return t * t * t;
        }
    };
    auto basisDerivative = [](qreal t, int i) {
        const qreal mt = 1.0 - t;
        switch (i) {
        case 0: return -3.0 * mt * mt;
        case 1: return 3.0 * mt * mt - 6.0 * t * mt;
        case 2: return 6.0 * t * mt - 3.0 * t * t;
        default: return 3.0 * t * t;
        }
    };
    auto gridPoint = [this](int column, int row) {
        return m_envelopeGrid.at(row * 4 + column);
    };
    auto surface = [&](qreal u, qreal v) {
        QPointF point;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                point += gridPoint(column, row)
                         * (basis(u, column) * basis(v, row));
            }
        }
        return point;
    };
    auto tangentU = [&](qreal u, qreal v) {
        QPointF point;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                point += gridPoint(column, row)
                         * (basisDerivative(u, column) * basis(v, row));
            }
        }
        return point;
    };
    auto tangentV = [&](qreal u, qreal v) {
        QPointF point;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                point += gridPoint(column, row)
                         * (basis(u, column) * basisDerivative(v, row));
            }
        }
        return point;
    };
    auto normalize = [&](const QPointF &point) {
        return QPointF(qBound(0.0, (point.x() - bounds.left()) / width, 1.0),
                       qBound(0.0, (point.y() - bounds.top()) / height, 1.0));
    };
    auto warp = [&](const QPointF &point) {
        const QPointF uv = normalize(point);
        return surface(uv.x(), uv.y());
    };
    auto warpOffset = [&](const QPointF &anchor, const QPointF &control) {
        const QPointF uv = normalize(anchor);
        const QPointF tu = tangentU(uv.x(), uv.y()) / width;
        const QPointF tv = tangentV(uv.x(), uv.y()) / height;
        const QPointF offset = control - anchor;
        return surface(uv.x(), uv.y()) + tu * offset.x() + tv * offset.y();
    };

    QVector<QPainterPath::Element> elements = m_envelopeOriginal;
    QPointF anchor;
    QPainterPath::ElementType previous = QPainterPath::MoveToElement;
    for (int i = 0; i < elements.size(); ++i) {
        QPainterPath::Element &element = elements[i];
        const QPointF original(element.x, element.y);
        QPointF mapped;
        if (element.type == QPainterPath::CurveToElement
            || (element.type == QPainterPath::CurveToDataElement
                && previous == QPainterPath::CurveToElement)) {
            mapped = warpOffset(anchor, original);
        } else {
            mapped = warp(original);
        }
        element.x = mapped.x();
        element.y = mapped.y();

        if (element.type == QPainterPath::MoveToElement
            || element.type == QPainterPath::LineToElement)
            anchor = original;
        else if (element.type == QPainterPath::CurveToDataElement
                 && previous == QPainterPath::CurveToDataElement)
            anchor = original;
        previous = element.type;
    }
    m_envelopeItem->setPathElements(elements);
}

void CanvasView::resetEnvelope()
{
    if (!m_envelopeItem)
        return;
    if (!m_envelopeOriginal.isEmpty())
        m_envelopeItem->setPathElements(m_envelopeOriginal);
    m_envelopeGrid.clear();
    if (m_envelopeBounds.width() >= 1.0 && m_envelopeBounds.height() >= 1.0) {
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                m_envelopeGrid.append(QPointF(
                    m_envelopeBounds.left()
                        + m_envelopeBounds.width() * column / 3.0,
                    m_envelopeBounds.top()
                        + m_envelopeBounds.height() * row / 3.0));
            }
        }
    }
    m_envelopeDragIndex = -1;
    viewport()->update();
}

void CanvasView::drawEnvelopeOverlay(QPainter *painter)
{
    if (!m_envelopeItem)
        return;
    const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(0xff, 0xb3, 0x47, 170), 1.0 / s, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    for (int row = 0; row < 4; ++row) {
        QPainterPath line;
        for (int column = 0; column < 4; ++column) {
            const QPointF point = m_envelopeItem->mapToScene(
                m_envelopeGrid.at(row * 4 + column));
            if (column == 0)
                line.moveTo(point);
            else
                line.lineTo(point);
        }
        painter->drawPath(line);
    }
    for (int column = 0; column < 4; ++column) {
        QPainterPath line;
        for (int row = 0; row < 4; ++row) {
            const QPointF point = m_envelopeItem->mapToScene(
                m_envelopeGrid.at(row * 4 + column));
            if (row == 0)
                line.moveTo(point);
            else
                line.lineTo(point);
        }
        painter->drawPath(line);
    }

    const qreal radius = 4.0 / s;
    for (int i = 0; i < m_envelopeGrid.size(); ++i) {
        const QPointF point = m_envelopeItem->mapToScene(m_envelopeGrid.at(i));
        painter->setPen(QPen(QColor(0x4f, 0x7c, 0xc0), 1.0 / s));
        painter->setBrush(i == m_envelopeDragIndex ? QColor(0xff, 0xb3, 0x47)
                                                   : QColor(0xf4, 0xf7, 0xfb));
        painter->drawEllipse(point, radius, radius);
    }
    painter->restore();
}

void CanvasView::groupSelection()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.size() < 2) {
        emit statusHint(QStringLiteral("Для группировки нужно минимум два объекта."));
        return;
    }

    emit beginEdit();
    const int groupId = m_nextGroupId++;
    m_groups.append({groupId, 0});

    QSet<int> attached;
    for (VectorItem *item : selected) {
        const int current = item->groupIndex();
        if (current == 0) {
            item->setGroupIndex(groupId);
            continue;
        }
        const int top = topGroupOf(current);
        if (attached.contains(top))
            continue;
        for (VectorGroup &entry : m_groups) {
            if (entry.id == top) {
                entry.parent = groupId;
                break;
            }
        }
        attached.insert(top);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Создана группа из %1 объектов.").arg(selected.size()));
}

void CanvasView::ungroupSelection()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;

    QSet<int> tops;
    for (VectorItem *item : selected) {
        if (item->groupIndex() > 0)
            tops.insert(topGroupOf(item->groupIndex()));
    }
    if (tops.isEmpty()) {
        emit statusHint(QStringLiteral("Выделенные объекты не входят в группу."));
        return;
    }

    emit beginEdit();
    for (int top : tops) {
        int parent = 0;
        for (const VectorGroup &entry : m_groups) {
            if (entry.id == top) {
                parent = entry.parent;
                break;
            }
        }
        for (VectorGroup &entry : m_groups) {
            if (entry.parent == top)
                entry.parent = parent;
        }
        for (VectorItem *item : vectorItems()) {
            if (item->groupIndex() == top)
                item->setGroupIndex(parent);
        }
        for (int i = m_groups.size() - 1; i >= 0; --i) {
            if (m_groups.at(i).id == top)
                m_groups.removeAt(i);
        }
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Группа расформирована."));
}

void CanvasView::ungroupAll()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;

    QSet<int> tops;
    for (VectorItem *item : selected) {
        if (item->groupIndex() > 0)
            tops.insert(topGroupOf(item->groupIndex()));
    }
    if (tops.isEmpty()) {
        emit statusHint(QStringLiteral("Выделенные объекты не входят в группу."));
        return;
    }

    auto isDescendant = [this](int group, int root) {
        int current = group;
        for (int guard = 0; guard < 100 && current > 0; ++guard) {
            if (current == root)
                return true;
            int parent = 0;
            for (const VectorGroup &entry : m_groups) {
                if (entry.id == current) {
                    parent = entry.parent;
                    break;
                }
            }
            current = parent;
        }
        return false;
    };

    emit beginEdit();
    for (int root : tops) {
        for (VectorItem *item : vectorItems()) {
            if (item->groupIndex() > 0
                && isDescendant(item->groupIndex(), root))
                item->setGroupIndex(0);
        }
        for (int i = m_groups.size() - 1; i >= 0; --i) {
            if (isDescendant(m_groups.at(i).id, root))
                m_groups.removeAt(i);
        }
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Все вложенные группы расформированы."));
}

void CanvasView::blendSelected(int steps)
{
    steps = qBound(1, steps, 80);
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.size() < 2) {
        emit statusHint(QStringLiteral("Для перетекания выделите два или более объектов."));
        return;
    }

    const auto ensureId = [this](VectorItem *item) {
        VectorObject object = item->object();
        if (object.id <= 0) {
            object.id = m_nextObjectId++;
            item->setObject(object);
        }
        return object.id;
    };

    emit beginEdit();
    int created = 0;
    for (int i = 0; i + 1 < selected.size(); ++i) {
        const int fromId = ensureId(selected.at(i));
        const int toId = ensureId(selected.at(i + 1));
        if (fromId == toId)
            continue;

        int existing = -1;
        for (int r = 0; r < m_blends.size(); ++r) {
            const BlendRecord &record = m_blends.at(r);
            const bool same = (record.fromId == fromId && record.toId == toId)
                              || (record.fromId == toId && record.toId == fromId);
            if (same) {
                existing = r;
                break;
            }
        }
        if (existing >= 0) {
            m_blends[existing].steps = steps;
            m_blends[existing].spacing = 0.0;
            m_blends[existing].active = true;
            updateBlend(m_blends.at(existing).id);
        } else {
            BlendRecord record;
            record.id = m_nextBlendId++;
            record.fromId = fromId;
            record.toId = toId;
            record.steps = steps;
            m_blends.append(record);
            updateBlend(record.id);
            ++created;
        }
    }
    emit endEdit();
    if (created == 0)
        emit statusHint(QStringLiteral("Перетекание обновлено."));
    else
        emit statusHint(QStringLiteral("Создано перетеканий: %1.").arg(created));
}


void CanvasView::updateBlend(int blendId)
{
    if (m_updatingBlends)
        return;
    int index = -1;
    for (int i = 0; i < m_blends.size(); ++i) {
        if (m_blends.at(i).id == blendId) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;
    const BlendRecord record = m_blends.at(index);
    if (!record.active)
        return;

    VectorItem *from = nullptr;
    VectorItem *to = nullptr;
    for (VectorItem *item : vectorItems()) {
        const int id = item->object().id;
        if (id == record.fromId)
            from = item;
        else if (id == record.toId)
            to = item;
    }
    if (!from || !to) {
        removeBlend(blendId);
        return;
    }

    m_updatingBlends = true;

    const QList<VectorItem *> existing = vectorItems();
    for (VectorItem *item : existing) {
        if (item->object().blendId == blendId) {
            scene()->removeItem(item);
            delete item;
        }
    }

    const VectorObject base = from->object();
    const VectorObject target = to->object();
    const QRectF fromBounds = base.localBounds();
    const QRectF toBounds = target.localBounds();

    int steps = qBound(1, record.steps, 80);
    if (record.spacing > 0.5) {
        const QPointF delta = target.pos - base.pos;
        const qreal distance = std::hypot(delta.x(), delta.y());
        steps = qBound(1, qRound(distance / record.spacing) - 1, 80);
    }

    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / qreal(steps + 1);
        VectorObject object = base;
        object.pos = base.pos + (target.pos - base.pos) * t;
        object.rotation = base.rotation + (target.rotation - base.rotation) * t;
        object.opacity = base.opacity + (target.opacity - base.opacity) * t;
        object.fillOpacity =
            base.fillOpacity + (target.fillOpacity - base.fillOpacity) * t;
        object.strokeOpacity =
            base.strokeOpacity + (target.strokeOpacity - base.strokeOpacity) * t;
        object.fill = interpolateColor(base.fill, target.fill, t);
        object.fill2 = interpolateColor(base.fill2, target.fill2, t);
        object.stroke = interpolateColor(base.stroke, target.stroke, t);
        object.shadowColor =
            interpolateColor(base.shadowColor, target.shadowColor, t);
        object.shadowOpacity =
            base.shadowOpacity + (target.shadowOpacity - base.shadowOpacity) * t;

        if (!base.gradientStops.isEmpty()
            && base.gradientStops.size() == target.gradientStops.size()) {
            for (int s = 0; s < object.gradientStops.size(); ++s) {
                object.gradientStops[s].position =
                    base.gradientStops.at(s).position
                    + (target.gradientStops.at(s).position
                       - base.gradientStops.at(s).position) * t;
                object.gradientStops[s].color =
                    interpolateColor(base.gradientStops.at(s).color,
                                     target.gradientStops.at(s).color, t);
                object.gradientStops[s].opacity =
                    base.gradientStops.at(s).opacity
                    + (target.gradientStops.at(s).opacity
                       - base.gradientStops.at(s).opacity) * t;
            }
        }

        if (!object.path.isEmpty() && fromBounds.width() > 1.0
            && fromBounds.height() > 1.0) {
            const qreal scaleX = 1.0 + (toBounds.width() / fromBounds.width() - 1.0) * t;
            const qreal scaleY = 1.0 + (toBounds.height() / fromBounds.height() - 1.0) * t;
            QTransform transform;
            transform.translate(fromBounds.center().x(), fromBounds.center().y());
            transform.scale(scaleX, scaleY);
            transform.translate(-fromBounds.center().x(), -fromBounds.center().y());
            object.path = transform.map(base.path);
        } else if (object.type == ShapeType::Text || object.type == ShapeType::Image) {
            const QSizeF size(fromBounds.width() + (toBounds.width() - fromBounds.width()) * t,
                              fromBounds.height() + (toBounds.height() - fromBounds.height()) * t);
            object.textBox.setSize(size);
            if (object.type == ShapeType::Text && base.font.pixelSize() > 0
                && target.font.pixelSize() > 0) {
                QFont font = base.font;
                font.setPixelSize(qRound(base.font.pixelSize()
                                         + (target.font.pixelSize() - base.font.pixelSize()) * t));
                object.font = font;
            }
        }

        if (base.effects.size() == target.effects.size()) {
            for (int e = 0; e < object.effects.size(); ++e) {
                const Effect &a = base.effects.at(e);
                const Effect &b = target.effects.at(e);
                if (a.type != b.type)
                    continue;
                Effect &effect = object.effects[e];
                effect.opacity = a.opacity + (b.opacity - a.opacity) * t;
                effect.radius = a.radius + (b.radius - a.radius) * t;
                effect.intensity = a.intensity + (b.intensity - a.intensity) * t;
                effect.angle = a.angle + (b.angle - a.angle) * t;
                effect.distance = a.distance + (b.distance - a.distance) * t;
                effect.amount = a.amount + (b.amount - a.amount) * t;
                effect.offset = a.offset + (b.offset - a.offset) * t;
                effect.color = interpolateColor(a.color, b.color, t);
            }
        }

        object.blendId = blendId;
        object.id = 0;
        object.masterId = 0;
        object.group = 0;
        object.symbolId = 0;
        object.instanceId = 0;
        addVectorObject(object, false, false);
    }

    m_updatingBlends = false;
}

void CanvasView::updateAllBlends()
{
    if (m_updatingBlends)
        return;
    const QVector<BlendRecord> copy = m_blends;
    for (const BlendRecord &record : copy)
        updateBlend(record.id);
}

void CanvasView::updateBlendsFor(VectorItem *item)
{
    if (!item || m_updatingBlends)
        return;
    const int id = item->object().id;
    if (id <= 0)
        return;
    const QVector<BlendRecord> copy = m_blends;
    for (const BlendRecord &record : copy) {
        if (record.fromId == id || record.toId == id)
            updateBlend(record.id);
    }
}

void CanvasView::removeBlend(int blendId)
{
    for (int i = 0; i < m_blends.size(); ++i) {
        if (m_blends.at(i).id == blendId) {
            m_blends.removeAt(i);
            break;
        }
    }
    const QList<VectorItem *> items = vectorItems();
    for (VectorItem *item : items) {
        if (item->object().blendId == blendId) {
            scene()->removeItem(item);
            delete item;
        }
    }
}

void CanvasView::setBlends(const QVector<BlendRecord> &blends)
{
    m_blends = blends;
    m_nextBlendId = 1;
    for (const BlendRecord &record : m_blends)
        m_nextBlendId = qMax(m_nextBlendId, record.id + 1);
}

void CanvasView::setBlendSteps(int blendId, int steps)
{
    for (BlendRecord &record : m_blends) {
        if (record.id != blendId)
            continue;
        record.steps = qBound(1, steps, 80);
        record.spacing = 0.0;
        emit beginEdit();
        updateBlend(blendId);
        emit endEdit();
        return;
    }
}

void CanvasView::setBlendSpacing(int blendId, qreal spacing)
{
    for (BlendRecord &record : m_blends) {
        if (record.id != blendId)
            continue;
        record.spacing = qMax<qreal>(0.0, spacing);
        emit beginEdit();
        updateBlend(blendId);
        emit endEdit();
        return;
    }
}

int CanvasView::blendIdFor(VectorItem *item) const
{
    return item ? item->object().blendId : 0;
}

namespace {

QRectF symbolVisualBounds(VectorItem *item)
{
    if (!item)
        return QRectF();
    return item->mapToScene(item->object().localBounds()).boundingRect();
}

QPainterPath scalePathToBox(const QPainterPath &path, const QRectF &target)
{
    const QRectF bounds = path.boundingRect();
    if (bounds.width() < 0.01 || bounds.height() < 0.01)
        return path;
    QTransform transform;
    transform.translate(target.left(), target.top());
    transform.scale(target.width() / bounds.width(), target.height() / bounds.height());
    transform.translate(-bounds.left(), -bounds.top());
    return transform.map(path);
}

// Maps a symbol object from the definition box into an instance box. Keeps the
// object's own style and rotation, so every instance can scale independently.
VectorObject mappedSymbolObject(const VectorObject &source, const QRectF &sourceBox,
                                const QRectF &targetBox)
{
    VectorObject object = source;
    const QRectF local = source.localBounds();
    if (sourceBox.width() < 0.01 || sourceBox.height() < 0.01
        || local.width() < 0.01 || local.height() < 0.01)
        return object;

    const qreal scaleX = targetBox.width() / sourceBox.width();
    const qreal scaleY = targetBox.height() / sourceBox.height();
    const QPointF sourceTopLeft(local.left() + source.pos.x(),
                                local.top() + source.pos.y());
    const QPointF relative = sourceTopLeft - sourceBox.topLeft();
    const QPointF targetTopLeft(targetBox.left() + relative.x() * scaleX,
                                targetBox.top() + relative.y() * scaleY);

    if (!object.path.isEmpty()) {
        object.path = scalePathToBox(object.path,
                                     QRectF(0.0, 0.0, local.width() * scaleX,
                                            local.height() * scaleY));
    } else if (object.type == ShapeType::Text || object.type == ShapeType::Image) {
        object.textBox = QRectF(0.0, 0.0, local.width() * scaleX,
                                local.height() * scaleY);
        if (object.type == ShapeType::Text && object.font.pixelSize() > 0) {
            QFont font = object.font;
            font.setPixelSize(qMax(4, qRound(object.font.pixelSize()
                                             * (scaleX + scaleY) / 2.0)));
            object.font = font;
        }
    }

    object.pos = targetTopLeft;
    return object;
}

} // namespace

QStringList CanvasView::symbolNames() const
{
    QStringList names;
    for (const SymbolDefinition &symbol : m_symbols)
        names.append(symbol.name);
    return names;
}

int CanvasView::createSymbolFromSelection(const QString &name)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выделите объекты для символа."));
        return 0;
    }

    QRectF bounds;
    for (VectorItem *item : selected) {
        const QRectF box = symbolVisualBounds(item);
        bounds = bounds.isNull() ? box : bounds.united(box);
    }
    if (bounds.width() < 1.0 || bounds.height() < 1.0) {
        emit statusHint(QStringLiteral("У выделения нет геометрии для символа."));
        return 0;
    }

    SymbolDefinition definition;
    definition.id = m_nextSymbolId++;
    definition.name = name.trimmed().isEmpty()
                          ? QStringLiteral("Символ %1").arg(definition.id)
                          : name.trimmed();
    definition.bounds = bounds;
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        object.page = m_currentPage;
        object.symbolId = 0;
        object.instanceId = 0;
        object.group = 0;
        definition.objects.append(object);
    }

    const int group = m_nextGroupId++;
    const int instanceId = m_nextInstanceId++;

    emit beginEdit();
    m_symbols.append(definition);
    m_groups.append({group, 0});
    SymbolInstance instance;
    instance.id = instanceId;
    instance.symbolId = definition.id;
    instance.bounds = bounds;
    instance.rotation = selected.first()->object().rotation;
    instance.page = m_currentPage;
    instance.group = group;
    m_instances.append(instance);

    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        object.symbolId = definition.id;
        object.instanceId = instanceId;
        object.group = group;
        item->setObject(object);
    }
    emit endEdit();

    emit statusHint(QStringLiteral("Символ «%1» создан (%2 объектов).")
                        .arg(definition.name)
                        .arg(definition.objects.size()));
    emit symbolsChanged();
    return definition.id;
}

int CanvasView::insertSymbol(int symbolId, const QPointF &topLeft)
{
    int definitionIndex = -1;
    for (int i = 0; i < m_symbols.size(); ++i) {
        if (m_symbols.at(i).id == symbolId) {
            definitionIndex = i;
            break;
        }
    }
    if (definitionIndex < 0) {
        emit statusHint(QStringLiteral("Символ не найден."));
        return 0;
    }

    const SymbolDefinition definition = m_symbols.at(definitionIndex);
    QSizeF size = definition.bounds.size();
    if (size.width() < 1.0 || size.height() < 1.0) {
        emit statusHint(QStringLiteral("У символа пустая рамка."));
        return 0;
    }

    QPointF position = topLeft;
    if (position.isNull()) {
        position = QPointF((m_pageSize.width() - size.width()) / 2.0,
                           (m_pageSize.height() - size.height()) / 2.0);
    }
    const QRectF target(position, size);
    const int group = m_nextGroupId++;
    const int instanceId = m_nextInstanceId++;

    emit beginEdit();
    m_groups.append({group, 0});
    for (const VectorObject &source : definition.objects) {
        VectorObject object = mappedSymbolObject(source, definition.bounds, target);
        object.symbolId = definition.id;
        object.instanceId = instanceId;
        object.group = group;
        object.page = m_currentPage;
        object.layer = m_activeLayer;
        object.id = 0;
        object.masterId = 0;
        object.blendId = 0;
        addVectorObject(object, false, false);
    }

    SymbolInstance instance;
    instance.id = instanceId;
    instance.symbolId = definition.id;
    instance.bounds = target;
    instance.rotation = 0.0;
    instance.page = m_currentPage;
    instance.group = group;
    m_instances.append(instance);
    emit endEdit();

    scene()->clearSelection();
    for (VectorItem *item : vectorItems()) {
        if (item->object().instanceId == instanceId)
            item->setSelected(true);
    }
    emit selectionChanged(selectedVectorItems().size());
    emit statusHint(QStringLiteral("Вставлен экземпляр символа «%1».")
                        .arg(definition.name));
    emit symbolsChanged();
    viewport()->update();
    return instanceId;
}

void CanvasView::rebuildSymbolInstance(int instanceId)
{
    int instanceIndex = -1;
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).id == instanceId) {
            instanceIndex = i;
            break;
        }
    }
    if (instanceIndex < 0)
        return;

    SymbolInstance record = m_instances.at(instanceIndex);
    int definitionIndex = -1;
    for (int i = 0; i < m_symbols.size(); ++i) {
        if (m_symbols.at(i).id == record.symbolId) {
            definitionIndex = i;
            break;
        }
    }
    if (definitionIndex < 0)
        return;
    const SymbolDefinition definition = m_symbols.at(definitionIndex);

    QList<VectorItem *> copies;
    QRectF current;
    qreal rotation = 0.0;
    for (VectorItem *item : vectorItems()) {
        if (item->object().instanceId != instanceId)
            continue;
        copies.append(item);
        const QRectF box = symbolVisualBounds(item);
        current = current.isNull() ? box : current.united(box);
    }
    if (copies.isEmpty())
        current = record.bounds;
    if (current.width() < 1.0 || current.height() < 1.0)
        current = record.bounds;
    if (!copies.isEmpty() && !definition.objects.isEmpty()) {
        rotation = copies.first()->object().rotation
                   - definition.objects.first().rotation;
    }

    const int group = record.group;
    int page = record.page == -9999 ? m_currentPage : record.page;
    int layer = m_activeLayer;
    if (!copies.isEmpty())
        layer = copies.first()->object().layer;
    for (VectorItem *item : copies) {
        scene()->removeItem(item);
        delete item;
    }

    for (const VectorObject &source : definition.objects) {
        VectorObject object = mappedSymbolObject(source, definition.bounds, current);
        object.symbolId = definition.id;
        object.instanceId = instanceId;
        object.group = group;
        object.rotation = source.rotation + rotation;
        object.page = page;
        object.layer = layer;
        object.id = 0;
        object.masterId = 0;
        object.blendId = 0;
        addVectorObject(object, false, false);
    }

    m_instances[instanceIndex].bounds = current;
    m_instances[instanceIndex].rotation = rotation;
    m_instances[instanceIndex].page = page;
    normalizeZOrder();
    viewport()->update();
}

bool CanvasView::updateSymbolFromSelection(int symbolId)
{
    const int instanceId = selectedInstanceId();
    if (instanceId <= 0) {
        emit statusHint(QStringLiteral(
            "Выделите экземпляр символа, чтобы обновить исходный символ."));
        return false;
    }

    int instanceIndex = -1;
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).id == instanceId) {
            instanceIndex = i;
            break;
        }
    }
    if (instanceIndex < 0)
        return false;

    const int targetSymbol = symbolId > 0 ? symbolId : m_instances.at(instanceIndex).symbolId;
    int definitionIndex = -1;
    for (int i = 0; i < m_symbols.size(); ++i) {
        if (m_symbols.at(i).id == targetSymbol) {
            definitionIndex = i;
            break;
        }
    }
    if (definitionIndex < 0) {
        emit statusHint(QStringLiteral("Исходный символ не найден."));
        return false;
    }

    QList<VectorItem *> sourceItems;
    QRectF bounds;
    for (VectorItem *item : vectorItems()) {
        if (item->object().instanceId != instanceId)
            continue;
        sourceItems.append(item);
        const QRectF box = symbolVisualBounds(item);
        bounds = bounds.isNull() ? box : bounds.united(box);
    }
    if (sourceItems.isEmpty() || bounds.width() < 1.0 || bounds.height() < 1.0) {
        emit statusHint(QStringLiteral("Экземпляр не найден на холсте."));
        return false;
    }

    emit beginEdit();
    SymbolDefinition &definition = m_symbols[definitionIndex];
    definition.bounds = bounds;
    definition.objects.clear();
    for (VectorItem *item : sourceItems) {
        VectorObject object = item->object();
        object.symbolId = 0;
        object.instanceId = 0;
        object.group = 0;
        definition.objects.append(object);
    }

    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).symbolId != targetSymbol
            || m_instances.at(i).id == instanceId)
            continue;
        rebuildSymbolInstance(m_instances.at(i).id);
    }
    m_instances[instanceIndex].bounds = bounds;
    emit endEdit();

    emit statusHint(QStringLiteral("Символ «%1» обновлён, экземпляры пересобраны.")
                        .arg(definition.name));
    emit symbolsChanged();
    return true;
}

void CanvasView::detachSelectedSymbolInstances()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    QSet<int> instanceIds;
    for (VectorItem *item : selected) {
        const int id = item->object().instanceId;
        if (id > 0)
            instanceIds.insert(id);
    }
    if (instanceIds.isEmpty()) {
        emit statusHint(QStringLiteral("Выделение не содержит экземпляров символа."));
        return;
    }

    emit beginEdit();
    for (VectorItem *item : vectorItems()) {
        if (!instanceIds.contains(item->object().instanceId))
            continue;
        VectorObject object = item->object();
        object.symbolId = 0;
        object.instanceId = 0;
        item->setObject(object);
    }
    for (int i = m_instances.size() - 1; i >= 0; --i) {
        if (instanceIds.contains(m_instances.at(i).id))
            m_instances.removeAt(i);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Экземпляры отделены от символа."));
    emit symbolsChanged();
}

void CanvasView::renameSymbol(int symbolId, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    for (SymbolDefinition &symbol : m_symbols) {
        if (symbol.id != symbolId)
            continue;
        if (symbol.name == name.trimmed())
            return;
        emit beginEdit();
        symbol.name = name.trimmed();
        emit endEdit();
        emit symbolsChanged();
        return;
    }
}

void CanvasView::deleteSymbol(int symbolId)
{
    emit beginEdit();
    QSet<int> instanceIds;
    for (int i = m_instances.size() - 1; i >= 0; --i) {
        if (m_instances.at(i).symbolId == symbolId) {
            instanceIds.insert(m_instances.at(i).id);
            m_instances.removeAt(i);
        }
    }
    for (VectorItem *item : vectorItems()) {
        if (!instanceIds.contains(item->object().instanceId))
            continue;
        VectorObject object = item->object();
        object.symbolId = 0;
        object.instanceId = 0;
        item->setObject(object);
    }
    for (int i = m_symbols.size() - 1; i >= 0; --i) {
        if (m_symbols.at(i).id == symbolId)
            m_symbols.removeAt(i);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Символ удалён, его экземпляры стали обычными объектами."));
    emit symbolsChanged();
}

int CanvasView::selectedSymbolId() const
{
    for (VectorItem *item : selectedVectorItems()) {
        if (item->object().symbolId > 0)
            return item->object().symbolId;
    }
    return 0;
}

int CanvasView::selectedInstanceId() const
{
    for (VectorItem *item : selectedVectorItems()) {
        if (item->object().instanceId > 0)
            return item->object().instanceId;
    }
    return 0;
}

QImage CanvasView::renderSymbolImage(int symbolId, const QSize &size) const
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const SymbolDefinition *definition = nullptr;
    for (const SymbolDefinition &symbol : m_symbols) {
        if (symbol.id == symbolId) {
            definition = &symbol;
            break;
        }
    }
    if (!definition || definition->bounds.width() < 1.0
        || definition->bounds.height() < 1.0)
        return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const qreal scale = qMin(size.width() / definition->bounds.width(),
                             size.height() / definition->bounds.height()) * 0.88;
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-definition->bounds.center());
    for (const VectorObject &object : definition->objects) {
        painter.save();
        const QRectF bounds = object.localBounds();
        painter.translate(object.pos - bounds.topLeft());
        painter.rotate(object.rotation);
        Document::paintObject(&painter, object);
        painter.restore();
    }
    painter.end();
    return image;
}

namespace {

QString formattedMeasurement(qreal pixels, int units, int precision, qreal scale)
{
    qreal value = pixels;
    QString suffix;
    const qreal pxPerMm = 3.7795275591;
    if (units == 3) {
        suffix = QStringLiteral(" px");
    } else if (units == 2) {
        value = pixels / 96.0 * scale;
        suffix = QStringLiteral(" in");
    } else if (units == 1) {
        value = pixels / pxPerMm * 10.0 * scale;
        suffix = QStringLiteral(" см");
    } else {
        value = pixels / pxPerMm * scale;
        suffix = QStringLiteral(" мм");
    }
    return QString::number(value, 'f', qBound(0, precision, 4)) + suffix;
}

} // namespace

QString CanvasView::measurementText(qreal pixels) const
{
    return formattedMeasurement(pixels, m_dimensionUnits, m_dimensionPrecision,
                                m_drawingScale);
}

void CanvasView::setDimensionMode(DimensionMode mode)
{
    m_dimensionMode = mode;
    m_dimensionPoints.clear();
    m_dimensionRefs.clear();
    m_dimensionAnchors.clear();
    m_dimensionHasCursor = false;
    viewport()->update();
}

void CanvasView::setDimensionUnits(int units)
{
    m_dimensionUnits = qBound(0, units, 3);
}

void CanvasView::setDimensionPrecision(int digits)
{
    m_dimensionPrecision = qBound(0, digits, 4);
}

void CanvasView::setDimensionOffset(qreal offset)
{
    m_dimensionOffset = qBound(-300.0, offset, 300.0);
}

void CanvasView::setDimensionAssociative(bool on)
{
    m_dimensionAssociative = on;
}

QPointF CanvasView::anchorScenePoint(VectorItem *item, const QPointF &anchor) const
{
    if (!item)
        return QPointF();
    const QRectF bounds = item->object().localBounds();
    if (bounds.width() < 0.01 || bounds.height() < 0.01)
        return item->scenePos();
    const QPointF local(bounds.left() + bounds.width() * anchor.x(),
                        bounds.top() + bounds.height() * anchor.y());
    return item->mapToScene(local);
}

bool CanvasView::resolveDimension(VectorObject &object) const
{
    if (object.type != ShapeType::Dimension)
        return false;

    const auto findItem = [this](int id) -> VectorItem * {
        if (id <= 0)
            return nullptr;
        for (VectorItem *item : vectorItems()) {
            if (item->object().id == id)
                return item;
        }
        return nullptr;
    };

    bool changed = false;
    if (object.dimRefA > 0) {
        if (VectorItem *ref = findItem(object.dimRefA)) {
            object.dimA = anchorScenePoint(ref, object.dimAnchorA);
            changed = true;
        } else {
            object.dimRefA = 0;
        }
    }
    if (object.dimRefB > 0) {
        if (VectorItem *ref = findItem(object.dimRefB)) {
            object.dimB = anchorScenePoint(ref, object.dimAnchorB);
            changed = true;
        } else {
            object.dimRefB = 0;
        }
    }
    if (object.dimRefC > 0) {
        if (VectorItem *ref = findItem(object.dimRefC)) {
            object.dimC = anchorScenePoint(ref, object.dimAnchorC);
            changed = true;
        } else {
            object.dimRefC = 0;
        }
    }
    return changed;
}

void CanvasView::updateDimensionsFor(VectorItem *item)
{
    if (!item || m_updatingDimensions)
        return;
    const int id = item->object().id;
    if (id <= 0)
        return;

    m_updatingDimensions = true;
    for (VectorItem *dimensionItem : vectorItems()) {
        VectorObject object = dimensionItem->object();
        if (object.type != ShapeType::Dimension || !object.dimAssociative)
            continue;
        if (object.dimRefA != id && object.dimRefB != id && object.dimRefC != id)
            continue;
        if (resolveDimension(object))
            dimensionItem->setObject(object);
    }
    m_updatingDimensions = false;
    viewport()->update();
}

void CanvasView::updateAllDimensions()
{
    if (m_updatingDimensions)
        return;
    m_updatingDimensions = true;
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.type != ShapeType::Dimension)
            continue;
        bool changed = false;
        if (object.dimScale != m_drawingScale) {
            object.dimScale = m_drawingScale;
            changed = true;
        }
        if (object.dimAssociative && resolveDimension(object))
            changed = true;
        if (changed)
            item->setObject(object);
    }
    m_updatingDimensions = false;
}

void CanvasView::applyDimensionSettingsToSelection()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    bool any = false;
    for (VectorItem *item : selected) {
        if (item->object().type == ShapeType::Dimension) {
            any = true;
            break;
        }
    }
    if (!any) {
        emit statusHint(QStringLiteral("Выделите размерные объекты."));
        return;
    }

    emit beginEdit();
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.type != ShapeType::Dimension)
            continue;
        object.dimUnits = m_dimensionUnits;
        object.dimPrecision = m_dimensionPrecision;
        object.dimOffset = m_dimensionOffset;
        object.dimScale = m_drawingScale;
        object.dimAssociative = m_dimensionAssociative;
        item->setObject(object);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Параметры размеров применены."));
}

void CanvasView::dimensionPress(const QPointF &scenePos)
{
    const QPointF snapped = snapNodePoint(scenePos, nullptr);

    const auto pickAnchor = [this, &snapped](QPointF *point) {
        *point = snapped;
        int refId = 0;
        QPointF anchor(-1.0, -1.0);
        VectorItem *item = itemAtScene(snapped);
        if (item) {
            const QRectF bounds = item->object().localBounds();
            if (bounds.width() > 0.5 && bounds.height() > 0.5) {
                const QPointF local = item->mapFromScene(snapped);
                const QPointF normalised((local.x() - bounds.left()) / bounds.width(),
                                         (local.y() - bounds.top()) / bounds.height());
                if (normalised.x() > -0.08 && normalised.x() < 1.08
                    && normalised.y() > -0.08 && normalised.y() < 1.08) {
                    anchor = QPointF(qBound(0.0, normalised.x(), 1.0),
                                     qBound(0.0, normalised.y(), 1.0));
                    refId = item->object().id;
                    *point = anchorScenePoint(item, anchor);
                }
            }
        }
        return qMakePair(refId, anchor);
    };

    if (m_dimensionMode == DimensionMode::Radius
        || m_dimensionMode == DimensionMode::Diameter) {
        VectorItem *item = itemAtScene(snapped);
        if (!item) {
            emit statusHint(QStringLiteral(
                "Щёлкните по окружности или эллипсу для радиального размера."));
            return;
        }
        const QRectF bounds = item->object().localBounds();
        if (bounds.width() < 1.0 || bounds.height() < 1.0)
            return;
        const QPointF center = bounds.center();
        const qreal rx = bounds.width() / 2.0;
        const qreal ry = bounds.height() / 2.0;
        const QPointF local = item->mapFromScene(snapped);
        QPointF direction = local - center;
        if (direction.manhattanLength() < 0.01)
            direction = QPointF(rx, 0);
        const qreal divisor = std::hypot(direction.x() / rx, direction.y() / ry);
        const QPointF edge = divisor > 0.0001
                                 ? center + direction / divisor
                                 : center + QPointF(rx, 0);

        VectorObject dimension;
        dimension.type = ShapeType::Dimension;
        dimension.dimensionType = m_dimensionMode == DimensionMode::Radius ? 4 : 5;
        dimension.dimA = item->mapToScene(center);
        dimension.dimB = item->mapToScene(edge);
        dimension.dimOffset = m_dimensionOffset;
        dimension.dimUnits = m_dimensionUnits;
        dimension.dimPrecision = m_dimensionPrecision;
        dimension.dimScale = m_drawingScale;
        dimension.dimAssociative = m_dimensionAssociative;
        dimension.dimRefA = item->object().id;
        dimension.dimRefB = item->object().id;
        dimension.dimAnchorA = QPointF(0.5, 0.5);
        dimension.dimAnchorB = QPointF((edge.x() - bounds.left()) / bounds.width(),
                                       (edge.y() - bounds.top()) / bounds.height());
        dimension.stroke = m_stroke;
        dimension.strokeWidth = 1.0;
        dimension.hasFill = false;
        dimension.hasStroke = true;
        dimension.fill = m_stroke;
        dimension.font = QFont(QStringLiteral("Adwaita Sans"));
        dimension.font.setPixelSize(11);
        dimension.page = m_currentPage;
        dimension.layer = m_activeLayer;
        dimension.pos = QPointF();

        emit beginEdit();
        addVectorObject(dimension, false, true);
        emit endEdit();
        emit statusHint(QStringLiteral("Радиальный размер создан."));
        viewport()->update();
        return;
    }

    QPointF point;
    const QPair<int, QPointF> anchorInfo = pickAnchor(&point);
    m_dimensionPoints.append(point);
    m_dimensionRefs.append(anchorInfo.first);
    m_dimensionAnchors.append(anchorInfo.second);

    const int needed = m_dimensionMode == DimensionMode::Angular ? 3 : 2;
    if (m_dimensionPoints.size() < needed) {
        if (m_dimensionMode == DimensionMode::Angular && m_dimensionPoints.size() == 1)
            emit statusHint(QStringLiteral("Укажите точку первой стороны угла."));
        else if (m_dimensionMode == DimensionMode::Angular)
            emit statusHint(QStringLiteral("Укажите точку второй стороны угла."));
        else
            emit statusHint(QStringLiteral("Укажите вторую точку размера."));
        viewport()->update();
        return;
    }

    VectorObject dimension;
    dimension.type = ShapeType::Dimension;
    dimension.dimensionType = static_cast<int>(m_dimensionMode);
    dimension.dimA = m_dimensionPoints.at(0);
    dimension.dimB = m_dimensionPoints.at(1);
    if (m_dimensionMode == DimensionMode::Angular)
        dimension.dimC = m_dimensionPoints.at(2);
    else
        dimension.dimC = QPointF();
    dimension.dimOffset = m_dimensionOffset;
    dimension.dimUnits = m_dimensionUnits;
    dimension.dimPrecision = m_dimensionPrecision;
    dimension.dimScale = m_drawingScale;
    dimension.dimAssociative = m_dimensionAssociative;
    dimension.dimRefA = m_dimensionRefs.value(0);
    dimension.dimRefB = m_dimensionRefs.value(1);
    dimension.dimRefC = m_dimensionRefs.value(2);
    dimension.dimAnchorA = m_dimensionAnchors.value(0, QPointF(-1.0, -1.0));
    dimension.dimAnchorB = m_dimensionAnchors.value(1, QPointF(-1.0, -1.0));
    dimension.dimAnchorC = m_dimensionAnchors.value(2, QPointF(-1.0, -1.0));
    dimension.stroke = m_stroke;
    dimension.strokeWidth = 1.0;
    dimension.hasFill = false;
    dimension.hasStroke = true;
    dimension.fill = m_stroke;
    dimension.font = QFont(QStringLiteral("Adwaita Sans"));
    dimension.font.setPixelSize(11);
    dimension.page = m_currentPage;
    dimension.layer = m_activeLayer;
    dimension.pos = QPointF();

    m_dimensionPoints.clear();
    m_dimensionRefs.clear();
    m_dimensionAnchors.clear();
    m_dimensionHasCursor = false;

    emit beginEdit();
    addVectorObject(dimension, false, true);
    emit endEdit();
    emit statusHint(QStringLiteral("Размер создан."));
    viewport()->update();
}

void CanvasView::drawDimensionPreview(QPainter *painter)
{
    if (m_tool != Tool::Dimension || m_dimensionPoints.isEmpty())
        return;

    const qreal scale = transform().m11() > 0 ? transform().m11() : 1.0;
    const qreal size = 4.0 / scale;
    QPen pen(QColor(0x00, 0xb8, 0xd9), 1.3 / scale);
    pen.setStyle(Qt::DashLine);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->setBrush(QColor(0x1b, 0x1d, 0x21));
    for (const QPointF &point : m_dimensionPoints)
        painter->drawRect(QRectF(point - QPointF(size, size), QSizeF(size * 2, size * 2)));

    painter->setBrush(Qt::NoBrush);
    if (m_dimensionPoints.size() >= 2) {
        painter->drawLine(m_dimensionPoints.at(0), m_dimensionPoints.at(1));
        if (m_dimensionMode == DimensionMode::Angular)
            painter->drawLine(m_dimensionPoints.at(0), m_dimensionPoints.at(1));
    }
    if (m_dimensionHasCursor) {
        painter->setPen(QPen(QColor(0xff, 0x66, 0x99), 1.0 / scale));
        painter->drawLine(m_dimensionPoints.last(), m_dimensionCursor);
        QString label;
        if (m_dimensionMode == DimensionMode::Angular && m_dimensionPoints.size() >= 2) {
            const QPointF a = m_dimensionPoints.at(0);
            const QPointF b = m_dimensionPoints.at(1);
            const qreal a1 = std::atan2(b.y() - a.y(), b.x() - a.x());
            const qreal a2 = std::atan2(m_dimensionCursor.y() - a.y(),
                                        m_dimensionCursor.x() - a.x());
            qreal degrees = qAbs(a2 - a1) * 180.0 / kPi;
            if (degrees > 180.0)
                degrees = 360.0 - degrees;
            label = QString::number(degrees, 'f', m_dimensionPrecision)
                    + QStringLiteral("°");
        } else {
            label = measurementText(QLineF(m_dimensionPoints.last(),
                                           m_dimensionCursor).length());
        }
        QFont font = painter->font();
        font.setPixelSize(qMax(6, qRound(11.0 / scale)));
        painter->setFont(font);
        painter->setPen(QColor(0x9d, 0xc0, 0xf2));
        painter->drawText(m_dimensionCursor + QPointF(10.0 / scale, -10.0 / scale),
                          label);
    }
    painter->restore();
}

VectorItem *CanvasView::addConstructedPath(const QPainterPath &path, bool closed)
{
    if (path.isEmpty())
        return nullptr;

    VectorObject object;
    object.type = ShapeType::Bezier;
    object.path = path;
    object.pos = QPointF();
    object.hasFill = closed;
    object.fill = m_fill;
    object.hasStroke = true;
    object.stroke = m_stroke;
    object.strokeWidth = m_strokeWidth;
    object.page = m_currentPage;
    object.layer = m_activeLayer;
    object.id = 0;

    emit beginEdit();
    scene()->clearSelection();
    VectorItem *item = addVectorObject(object, false, true);
    emit endEdit();
    viewport()->update();
    return item;
}

void CanvasView::constructLine(const QPointF &a, const QPointF &b)
{
    QPainterPath path;
    path.moveTo(a);
    path.lineTo(b);
    if (addConstructedPath(path, false))
        emit statusHint(QStringLiteral("Линия построена по координатам."));
}

void CanvasView::constructLineFrom(const QPointF &start, qreal length, qreal angleDegrees)
{
    const qreal radians = angleDegrees * kPi / 180.0;
    const QPointF end = start + QPointF(std::cos(radians), std::sin(radians)) * length;
    QPainterPath path;
    path.moveTo(start);
    path.lineTo(end);
    if (addConstructedPath(path, false))
        emit statusHint(QStringLiteral("Линия %1 px под углом %2° построена.")
                            .arg(length, 0, 'f', 1)
                            .arg(angleDegrees, 0, 'f', 1));
}

void CanvasView::constructCircle(const QPointF &center, qreal radius)
{
    QPainterPath path;
    path.addEllipse(center, radius, radius);
    if (addConstructedPath(path, true))
        emit statusHint(QStringLiteral("Окружность R%1 построена.")
                            .arg(radius, 0, 'f', 1));
}

void CanvasView::constructArc(const QPointF &center, qreal radius,
                              qreal startDegrees, qreal sweepDegrees)
{
    const QRectF box(center.x() - radius, center.y() - radius,
                     radius * 2.0, radius * 2.0);
    QPainterPath path;
    path.arcMoveTo(box, startDegrees);
    path.arcTo(box, startDegrees, sweepDegrees);
    if (addConstructedPath(path, false))
        emit statusHint(QStringLiteral("Дуга R%1 построена.").arg(radius, 0, 'f', 1));
}

void CanvasView::constructPolygon(const QPointF &center, int sides, qreal radius)
{
    const QRectF box(center.x() - radius, center.y() - radius,
                     radius * 2.0, radius * 2.0);
    const QPainterPath path = buildPolygonPath(box, qBound(3, sides, 64));
    if (addConstructedPath(path, true))
        emit statusHint(QStringLiteral("Правильный %1-угольник построен.").arg(sides));
}

bool CanvasView::createParallelFromSelection(qreal offset)
{
    VectorItem *lineItem = nullptr;
    for (VectorItem *item : selectedVectorItems()) {
        if (!item->object().path.isEmpty()) {
            lineItem = item;
            break;
        }
    }
    if (!lineItem) {
        emit statusHint(QStringLiteral("Выделите линию или контур для параллели."));
        return false;
    }
    const QPainterPath path = lineItem->object().path;
    const QPointF a = lineItem->mapToScene(path.pointAtPercent(0.0));
    const QPointF b = lineItem->mapToScene(path.pointAtPercent(1.0));
    QPointF direction = b - a;
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length < 1.0) {
        emit statusHint(QStringLiteral("Линия слишком короткая."));
        return false;
    }
    direction /= length;
    const QPointF normal(-direction.y(), direction.x());
    QPainterPath parallel;
    parallel.moveTo(a + normal * offset);
    parallel.lineTo(b + normal * offset);
    if (!addConstructedPath(parallel, false))
        return false;
    emit statusHint(QStringLiteral("Параллельная линия построена."));
    return true;
}

bool CanvasView::createPerpendicularFromSelection(qreal length)
{
    VectorItem *lineItem = nullptr;
    for (VectorItem *item : selectedVectorItems()) {
        if (!item->object().path.isEmpty()) {
            lineItem = item;
            break;
        }
    }
    if (!lineItem) {
        emit statusHint(QStringLiteral("Выделите линию для перпендикуляра."));
        return false;
    }
    const QPainterPath path = lineItem->object().path;
    const QPointF a = lineItem->mapToScene(path.pointAtPercent(0.0));
    const QPointF b = lineItem->mapToScene(path.pointAtPercent(1.0));
    QPointF direction = b - a;
    const qreal baseLength = std::hypot(direction.x(), direction.y());
    if (baseLength < 1.0) {
        emit statusHint(QStringLiteral("Линия слишком короткая."));
        return false;
    }
    direction /= baseLength;
    const QPointF normal(-direction.y(), direction.x());
    const QPointF middle = (a + b) / 2.0;
    QPainterPath perpendicular;
    perpendicular.moveTo(middle - normal * (length / 2.0));
    perpendicular.lineTo(middle + normal * (length / 2.0));
    if (!addConstructedPath(perpendicular, false))
        return false;
    emit statusHint(QStringLiteral("Перпендикуляр построен из середины линии."));
    return true;
}

bool CanvasView::createTangentFromSelection(const QPointF &externalPoint)
{
    VectorItem *shapeItem = nullptr;
    for (VectorItem *item : selectedVectorItems()) {
        const ShapeType type = item->object().type;
        if (type == ShapeType::Ellipse || type == ShapeType::Rectangle) {
            shapeItem = item;
            break;
        }
    }
    if (!shapeItem) {
        emit statusHint(QStringLiteral("Выделите окружность или эллипс для касательной."));
        return false;
    }
    const QRectF bounds = shapeItem->mapRectToScene(shapeItem->object().localBounds());
    const QPointF center = bounds.center();
    const qreal rx = bounds.width() / 2.0;
    const qreal ry = bounds.height() / 2.0;
    if (rx < 0.5 || ry < 0.5)
        return false;

    const qreal dx = (externalPoint.x() - center.x()) / rx;
    const qreal dy = (externalPoint.y() - center.y()) / ry;
    const qreal distanceSquared = dx * dx + dy * dy;
    if (distanceSquared <= 1.01) {
        emit statusHint(QStringLiteral("Точка должна лежать вне фигуры."));
        return false;
    }
    const qreal root = std::sqrt(distanceSquared - 1.0);
    const QVector<QPointF> unitPoints = {
        QPointF((dx - root * dy) / distanceSquared, (dy + root * dx) / distanceSquared),
        QPointF((dx + root * dy) / distanceSquared, (dy - root * dx) / distanceSquared),
    };

    emit beginEdit();
    for (const QPointF &unit : unitPoints) {
        const QPointF tangent(center.x() + unit.x() * rx,
                              center.y() + unit.y() * ry);
        QPainterPath path;
        path.moveTo(externalPoint);
        path.lineTo(tangent);
        VectorObject object;
        object.type = ShapeType::Bezier;
        object.path = path;
        object.pos = QPointF();
        object.hasFill = false;
        object.hasStroke = true;
        object.stroke = m_stroke;
        object.strokeWidth = m_strokeWidth;
        object.page = m_currentPage;
        object.layer = m_activeLayer;
        addVectorObject(object, false, true);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Касательные построены."));
    return true;
}

void CanvasView::scaleSelectionBy(qreal factor)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty() || factor <= 0.0) {
        emit statusHint(QStringLiteral("Выделите объекты для масштабирования."));
        return;
    }

    QRectF bounds;
    for (VectorItem *item : selected) {
        const QRectF visual = item->mapRectToScene(item->localBounds());
        bounds = bounds.isNull() ? visual : bounds.united(visual);
    }
    if (bounds.width() < 0.5 || bounds.height() < 0.5)
        return;
    const QPointF pivot = bounds.center();

    emit beginEdit();
    for (VectorItem *item : selected) {
        if (item->object().type == ShapeType::Dimension)
            continue;
        const QRectF local = item->object().localBounds();
        const QRectF visual = item->mapRectToScene(local);
        const QPointF oldCenter = visual.center();
        item->setFrameSize(QSizeF(qMax<qreal>(1.0, local.width() * factor),
                                  qMax<qreal>(1.0, local.height() * factor)));
        const QPointF currentCenter = item->mapToScene(item->object().localBounds().center());
        const QPointF wanted = pivot + (oldCenter - pivot) * factor;
        item->moveBy(wanted.x() - currentCenter.x(), wanted.y() - currentCenter.y());
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Выделение масштабировано в %1×.")
                        .arg(factor, 0, 'f', 3));
}

int CanvasView::createPatternFromSelection(const QString &name)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выделите объекты для узора."));
        return 0;
    }
    QVector<VectorObject> objects;
    for (VectorItem *item : selected)
        objects.append(item->object());
    PatternDefinition definition = patternFromObjects(objects, name);
    if (definition.objects.isEmpty()) {
        emit statusHint(QStringLiteral("У выделения нет геометрии для узора."));
        return 0;
    }

    emit beginEdit();
    definition.id = m_nextPatternId++;
    m_patterns.append(definition);
    emit endEdit();
    emit statusHint(QStringLiteral("Узор «%1» создан в документе.")
                        .arg(definition.name));
    return definition.id;
}

int CanvasView::storePatternDefinition(const PatternDefinition &pattern)
{
    if (pattern.objects.isEmpty())
        return 0;
    emit beginEdit();
    PatternDefinition definition = pattern;
    int index = -1;
    for (int i = 0; i < m_patterns.size(); ++i) {
        if (m_patterns.at(i).id == definition.id) {
            index = i;
            break;
        }
    }
    if (definition.id <= 0 || index < 0) {
        definition.id = m_nextPatternId++;
        m_patterns.append(definition);
    } else {
        m_patterns[index] = definition;
    }
    emit endEdit();
    return definition.id;
}

bool CanvasView::applyPatternToSelection(int patternId)
{
    const PatternDefinition definition = patternDefinition(patternId);
    if (definition.objects.isEmpty()) {
        emit statusHint(QStringLiteral("Узор не найден."));
        return false;
    }
    return applyPatternDefinitionToSelection(definition);
}

bool CanvasView::applyPatternDefinitionToSelection(const PatternDefinition &pattern)
{
    if (pattern.objects.isEmpty()) {
        emit statusHint(QStringLiteral("В узоре нет объектов."));
        return false;
    }
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выделите объекты для заливки узором."));
        return false;
    }

    emit beginEdit();
    PatternDefinition definition = pattern;
    int index = -1;
    for (int i = 0; i < m_patterns.size(); ++i) {
        if (m_patterns.at(i).id == definition.id) {
            index = i;
            break;
        }
    }
    if (definition.id <= 0 || index < 0) {
        definition.id = m_nextPatternId++;
        m_patterns.append(definition);
    } else {
        m_patterns[index] = definition;
    }

    const QVector<PatternShape> shapes = bakePatternShapes(definition);
    int applied = 0;
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.path.isEmpty()
            || object.type == ShapeType::Dimension
            || object.type == ShapeType::Image)
            continue;
        object.patternId = definition.id;
        object.pattern = definition.settings;
        object.patternShapes = shapes;
        object.hasFill = true;
        item->setObject(object);
        ++applied;
    }
    emit endEdit();

    if (applied == 0) {
        emit statusHint(QStringLiteral(
            "Для узорной заливки нужны фигуры с замкнутым контуром."));
        return false;
    }
    emit statusHint(QStringLiteral("Узор «%1» применён к объектам: %2.")
                        .arg(definition.name)
                        .arg(applied));
    viewport()->update();
    return true;
}

void CanvasView::updatePatternDefinition(const PatternDefinition &pattern)
{
    if (pattern.id <= 0)
        return;
    int index = -1;
    for (int i = 0; i < m_patterns.size(); ++i) {
        if (m_patterns.at(i).id == pattern.id) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    emit beginEdit();
    m_patterns[index] = pattern;
    const QVector<PatternShape> shapes = bakePatternShapes(pattern);
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.patternId != pattern.id)
            continue;
        object.pattern = pattern.settings;
        object.patternShapes = shapes;
        item->setObject(object);
    }
    emit endEdit();
    viewport()->update();
}

bool CanvasView::updatePatternFromSelection(int patternId)
{
    const PatternDefinition existing = patternDefinition(patternId);
    if (existing.id <= 0) {
        emit statusHint(QStringLiteral("Узор не найден."));
        return false;
    }
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выделите новые элементы узора."));
        return false;
    }
    QVector<VectorObject> objects;
    for (VectorItem *item : selected)
        objects.append(item->object());
    PatternDefinition updated = patternFromObjects(objects, existing.name);
    updated.id = existing.id;
    updated.settings = existing.settings;
    updatePatternDefinition(updated);
    emit statusHint(QStringLiteral("Исходные элементы узора обновлены."));
    return true;
}

int CanvasView::expandSelectedPatterns()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    int expanded = 0;
    for (VectorItem *item : selected) {
        if (!item->object().patternShapes.isEmpty())
            ++expanded;
    }
    if (expanded == 0) {
        emit statusHint(QStringLiteral("Выделите объекты с узорной заливкой."));
        return 0;
    }

    emit beginEdit();
    scene()->clearSelection();
    int created = 0;
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        if (object.patternShapes.isEmpty())
            continue;
        const QVector<VectorObject> pieces = expandPatternObject(object);
        if (pieces.isEmpty())
            continue;
        const int group = pieces.size() > 1 ? m_nextGroupId++ : 0;
        if (group > 0)
            m_groups.append({group, 0});
        scene()->removeItem(item);
        delete item;
        for (VectorObject piece : pieces) {
            piece.group = group;
            piece.id = 0;
            piece.masterId = 0;
            piece.blendId = 0;
            piece.symbolId = 0;
            piece.instanceId = 0;
            if (VectorItem *copy = addVectorObject(piece, false, true)) {
                copy->setSelected(true);
                ++created;
            }
        }
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Узор разобран на объекты: %1.").arg(created));
    viewport()->update();
    return created;
}

void CanvasView::removePattern(int patternId)
{
    emit beginEdit();
    for (VectorItem *item : vectorItems()) {
        VectorObject object = item->object();
        if (object.patternId != patternId)
            continue;
        object.patternId = 0;
        object.patternShapes.clear();
        object.pattern = PatternSettings();
        item->setObject(object);
    }
    for (int i = m_patterns.size() - 1; i >= 0; --i) {
        if (m_patterns.at(i).id == patternId)
            m_patterns.removeAt(i);
    }
    emit endEdit();
}

int CanvasView::selectedPatternId() const
{
    for (VectorItem *item : selectedVectorItems()) {
        if (item->object().patternId > 0)
            return item->object().patternId;
    }
    return 0;
}

PatternDefinition CanvasView::patternDefinition(int patternId) const
{
    for (const PatternDefinition &pattern : m_patterns) {
        if (pattern.id == patternId)
            return pattern;
    }
    return PatternDefinition();
}

QString CanvasView::selectionToSvg() const
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return QString();

    QRectF box;
    for (VectorItem *item : selected) {
        const QRectF current = symbolVisualBounds(item);
        box = box.isNull() ? current : box.united(current);
    }
    if (box.width() < 0.5 || box.height() < 0.5)
        return QString();

    Document document;
    document.pageSize = box.size();
    document.pages.clear();
    for (VectorItem *item : selected) {
        VectorObject object = item->object();
        object.pos -= box.topLeft();
        object.page = 0;
        object.layer = 0;
        object.group = 0;
        object.symbolId = 0;
        object.instanceId = 0;
        object.masterId = 0;
        object.blendId = 0;
        document.objects.append(object);
    }
    return document.toSvg(false);
}

VectorItem *CanvasView::addCodeObject(const VectorObject &object)
{
    VectorObject code = object;
    code.type = ShapeType::Code;
    code.page = m_currentPage;
    code.layer = m_activeLayer;
    code.id = 0;
    emit beginEdit();
    scene()->clearSelection();
    VectorItem *item = addVectorObject(code, false, true);
    emit endEdit();
    emit statusHint(QStringLiteral("Код добавлен на страницу."));
    viewport()->update();
    return item;
}

void CanvasView::renameItem(VectorItem *item, const QString &name)
{
    if (!item)
        return;
    VectorObject object = item->object();
    object.name = name.trimmed();
    emit beginEdit();
    item->setObject(object);
    emit endEdit();
    emit statusHint(object.name.isEmpty()
                        ? QStringLiteral("Имя объекта сброшено.")
                        : QStringLiteral("Объект переименован: %1").arg(object.name));
}

void CanvasView::focusItem(VectorItem *item)
{
    if (!item)
        return;
    const VectorObject object = item->object();
    if (object.page >= 0 && object.page < m_pages.size())
        setCurrentPage(object.page);
    if (object.layer >= 0 && object.layer < m_layers.size())
        setActiveLayer(object.layer);
    if (object.group > 0)
        selectGroupItems(object.group);
    else {
        scene()->clearSelection();
        item->setSelected(true);
    }
    ensureVisible(item, 80, 80);
    centerOn(item);
    viewport()->update();
}

void CanvasView::updateCodeItem(VectorItem *item, const VectorObject &object)
{
    if (!item)
        return;
    VectorObject updated = object;
    updated.type = ShapeType::Code;
    const VectorObject current = item->object();
    updated.id = current.id;
    updated.pos = current.pos;
    updated.rotation = current.rotation;
    updated.page = current.page;
    updated.layer = current.layer;
    updated.locked = current.locked;
    updated.effects = current.effects;
    emit beginEdit();
    item->setObject(updated);
    emit endEdit();
    emit statusHint(QStringLiteral("Код обновлён."));
    viewport()->update();
}

int CanvasView::insertSvgAsset(const QString &svg, const QPointF &centre)
{
    Document imported;
    QString error;
    if (!importSvgData(svg.toUtf8(), &imported, &error) || imported.objects.isEmpty()) {
        emit statusHint(error.isEmpty()
                            ? QStringLiteral("В ресурсе нет объектов.")
                            : QStringLiteral("Не удалось вставить ресурс: %1").arg(error));
        return 0;
    }
    return insertImportedObjects(imported.objects, centre);
}

int CanvasView::insertImportedObjects(const QVector<VectorObject> &objects,
                                      const QPointF &centre)
{
    if (objects.isEmpty())
        return 0;
    QRectF box;
    for (const VectorObject &object : objects) {
        const QRectF current = object.localBounds().translated(object.pos);
        box = box.isNull() ? current : box.united(current);
    }
    if (box.width() < 0.5 || box.height() < 0.5)
        return 0;

    const QPointF offset = centre - box.center();
    emit beginEdit();
    scene()->clearSelection();
    int added = 0;
    for (VectorObject object : objects) {
        object.pos += offset;
        object.page = m_currentPage;
        object.layer = m_activeLayer;
        object.group = 0;
        object.symbolId = 0;
        object.instanceId = 0;
        object.masterId = 0;
        object.blendId = 0;
        object.id = 0;
        if (VectorItem *item = addVectorObject(object, false, false)) {
            item->setSelected(true);
            ++added;
        }
    }
    emit endEdit();
    emit selectionChanged(added);
    emit statusHint(QStringLiteral("Вставлено объектов из библиотеки: %1.").arg(added));
    viewport()->update();
    return added;
}

void CanvasView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-vectora-asset"))
        || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(event);
}

void CanvasView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-vectora-asset"))
        || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(event);
}

void CanvasView::dropEvent(QDropEvent *event)
{
    handleDrop(event);
}

bool CanvasView::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *drag = static_cast<QDragEnterEvent *>(event);
        if (drag->mimeData()->hasFormat(
                QStringLiteral("application/x-vectora-asset"))) {
            prepareAssetDrag(drag->mimeData(),
                             mapToScene(drag->position().toPoint()));
            drag->acceptProposedAction();
            return true;
        }
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasFormat(
                QStringLiteral("application/x-vectora-asset"))) {
            if (m_assetGhost)
                m_assetGhost->setPos(mapToScene(drag->position().toPoint())
                                     - m_dragBounds.center());
            drag->acceptProposedAction();
            return true;
        }
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragLeave:
        clearAssetDrag();
        return true;
    case QEvent::Drop:
        handleDrop(static_cast<QDropEvent *>(event));
        return true;
    default:
        break;
    }
    return QGraphicsView::viewportEvent(event);
}

void CanvasView::prepareAssetDrag(const QMimeData *mime, const QPointF &scenePos)
{
    clearAssetDrag();
    if (!mime->hasFormat(QStringLiteral("application/x-vectora-asset")))
        return;
    Document imported;
    QString error;
    if (!importSvgData(mime->data(QStringLiteral("application/x-vectora-asset")),
                       &imported, &error)
        || imported.objects.isEmpty())
        return;

    QRectF box;
    for (const VectorObject &object : imported.objects) {
        const QRectF current = object.localBounds().translated(object.pos);
        box = box.isNull() ? current : box.united(current);
    }
    if (box.width() < 0.5 || box.height() < 0.5)
        return;

    QPainterPath preview;
    for (const VectorObject &object : imported.objects) {
        QPainterPath path = object.path;
        if (path.isEmpty() && object.type == ShapeType::Text)
            path = buildTextPath(object);
        if (path.isEmpty() && object.type == ShapeType::Image)
            path.addRect(object.textBox);
        if (path.isEmpty())
            continue;
        QTransform transform;
        transform.translate(object.pos.x() - box.left(),
                            object.pos.y() - box.top());
        preview.addPath(transform.map(path));
    }
    if (preview.isEmpty())
        preview.addRect(QRectF(0, 0, box.width(), box.height()));

    m_dragObjects = imported.objects;
    m_dragBounds = box;
    m_assetGhost = new QGraphicsPathItem(preview);
    QPen pen(QColor(0x4f, 0x7c, 0xc0, 235), 1.2);
    pen.setStyle(Qt::DashLine);
    m_assetGhost->setPen(pen);
    m_assetGhost->setBrush(QColor(0x4f, 0x7c, 0xc0, 55));
    m_assetGhost->setZValue(1.0e6);
    scene()->addItem(m_assetGhost);
    m_assetGhost->setPos(scenePos - box.center());
}

void CanvasView::clearAssetDrag()
{
    if (m_assetGhost) {
        scene()->removeItem(m_assetGhost);
        delete m_assetGhost;
        m_assetGhost = nullptr;
    }
    m_dragObjects.clear();
    m_dragBounds = QRectF();
}

void CanvasView::handleDrop(QDropEvent *event)
{
    const QPointF scenePos = mapToScene(event->position().toPoint());

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-vectora-asset"))) {
        if (!m_dragObjects.isEmpty()) {
            const QVector<VectorObject> objects = m_dragObjects;
            clearAssetDrag();
            if (insertImportedObjects(objects, scenePos) > 0)
                event->acceptProposedAction();
        } else {
            const QString svg = QString::fromUtf8(
                event->mimeData()->data(QStringLiteral("application/x-vectora-asset")));
            clearAssetDrag();
            if (insertSvgAsset(svg, scenePos) > 0)
                event->acceptProposedAction();
        }
        return;
    }

    if (event->mimeData()->hasUrls()) {
        bool inserted = false;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (!url.isLocalFile())
                continue;
            const QString path = url.toLocalFile();
            if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                QFile file(path);
                if (file.open(QIODevice::ReadOnly))
                    inserted = insertSvgAsset(QString::fromUtf8(file.readAll()), scenePos) > 0
                               || inserted;
            } else {
                QImageReader reader(path);
                const QImage image = reader.read();
                if (image.isNull())
                    continue;
                VectorObject object;
                object.type = ShapeType::Image;
                object.image = image;
                object.textBox = QRectF(0, 0, image.width(), image.height());
                object.pos = scenePos - QPointF(image.width() / 2.0, image.height() / 2.0);
                object.page = m_currentPage;
                object.layer = m_activeLayer;
                object.id = 0;
                emit beginEdit();
                if (addVectorObject(object, false, true))
                    inserted = true;
                emit endEdit();
            }
        }
        clearAssetDrag();
        if (inserted) {
            event->acceptProposedAction();
            return;
        }
    }
    clearAssetDrag();
    QGraphicsView::dropEvent(event);
}

QVector<Effect> CanvasView::selectionEffects() const
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return {};
    return selected.first()->effects();
}

void CanvasView::setSelectionEffects(const QVector<Effect> &effects)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объекты."));
        return;
    }
    emit beginEdit();
    for (VectorItem *item : selected)
        item->setEffects(effects);
    emit endEdit();
}

void CanvasView::addEffectToSelection(const Effect &effect)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите объекты для эффекта."));
        return;
    }
    emit beginEdit();
    for (VectorItem *item : selected) {
        QVector<Effect> list = item->effects();
        list.append(effect);
        item->setEffects(list);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Эффект «%1» добавлен.").arg(effectDisplayName(effect.type)));
}

void CanvasView::removeEffectFromSelection(int index)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;
    emit beginEdit();
    for (VectorItem *item : selected) {
        QVector<Effect> list = item->effects();
        if (index >= 0 && index < list.size()) {
            list.removeAt(index);
            item->setEffects(list);
        }
    }
    emit endEdit();
}

void CanvasView::moveEffectInSelection(int index, int delta)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.isEmpty())
        return;
    emit beginEdit();
    for (VectorItem *item : selected) {
        QVector<Effect> list = item->effects();
        const int target = index + delta;
        if (index < 0 || index >= list.size() || target < 0 || target >= list.size())
            continue;
        list.move(index, target);
        item->setEffects(list);
    }
    emit endEdit();
}

void CanvasView::applyMaskFromSelection(const QString &type)
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.size() < 2) {
        emit statusHint(QStringLiteral("Выберите объект и фигуру-маску."));
        return;
    }
    VectorItem *maskItem = selected.first();
    VectorItem *target = selected.last();
    if (!maskItem || !target || maskItem == target)
        return;
    const VectorObject maskObject = maskItem->object();
    QPainterPath sceneMask =
        maskItem->mapToScene(maskObject.type == ShapeType::Text
                                 ? buildTextPath(maskObject)
                                 : maskObject.path);
    if (sceneMask.isEmpty()) {
        emit statusHint(QStringLiteral("У маски нет геометрии."));
        return;
    }
    const QPainterPath localMask = target->mapFromScene(sceneMask);

    Effect effect = makeEffect(type);
    effect.mask = localMask;
    effect.opacity = 1.0;

    emit beginEdit();
    QVector<Effect> effects = target->effects();
    effects.append(effect);
    target->setEffects(effects);
    scene()->removeItem(maskItem);
    delete maskItem;
    emit endEdit();
    emit statusHint(type == QLatin1String("clip-mask")
                        ? QStringLiteral("Обтравочная маска применена.")
                        : QStringLiteral("Маска прозрачности применена."));
}


void CanvasView::cropTo(const QRectF &rect)
{
    if (rect.width() < 3.0 || rect.height() < 3.0)
        return;

    const QList<VectorItem *> items = vectorItems();
    QList<VectorItem *> toDelete;
    int clippedCount = 0;
    emit beginEdit();

    for (VectorItem *item : items) {
        if (!item->sceneBoundingRect().intersects(rect))
            continue;
        const VectorObject object = item->object();
        if (!item->hasPath() || !object.hasFill)
            continue;

        const QPainterPath scenePath = item->mapToScene(object.path);
        QPainterPath clipPath;
        clipPath.addRect(rect);
        const QPainterPath clipped = scenePath.intersected(clipPath);
        if (clipped.isEmpty()) {
            toDelete.append(item);
            continue;
        }
        item->setPath(item->mapFromScene(clipped));
        ++clippedCount;
    }

    for (VectorItem *item : toDelete) {
        scene()->removeItem(item);
        delete item;
    }

    for (VectorItem *item : vectorItems())
        item->setPos(item->pos() - rect.topLeft());

    setPageSize(rect.size());
    emit endEdit();
    emit statusHint(QStringLiteral("Страница обрезана до %1 × %2 px, изменено объектов: %3")
                        .arg(qRound(rect.width()))
                        .arg(qRound(rect.height()))
                        .arg(clippedCount));
}

bool CanvasView::nodePress(const QPointF &scenePos)
{
    const qreal tolerance = 9.0 / (m_zoom > 0.0 ? m_zoom : 1.0);
    for (VectorItem *item : selectedVectorItems()) {
        if (!item->hasPath())
            continue;
        const QVector<QPainterPath::Element> elements = item->pathElements();
        for (int i = 0; i < elements.size(); ++i) {
            const QPointF point = item->mapToScene(
                QPointF(elements.at(i).x, elements.at(i).y));
            if (QLineF(point, scenePos).length() <= tolerance) {
                if (m_nodeItem != item) {
                    m_selectedNodeElements.clear();
                    m_nodeItem = item;
                }
                if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
                    if (m_selectedNodeElements.contains(i))
                        m_selectedNodeElements.remove(i);
                    else
                        m_selectedNodeElements.insert(i);
                } else {
                    m_selectedNodeElements.clear();
                    m_selectedNodeElements.insert(i);
                }
                m_nodeElement = i;
                m_nodeLastScene = scenePos;
                if (m_selectedNodeElements.isEmpty())
                    return true;
                emit beginEdit();
                m_nodeDragging = true;
                return true;
            }
        }
    }
    return false;
}

void CanvasView::insertNodeAt(const QPointF &scenePos, bool nearestSegment)
{
    VectorItem *item = m_nodeItem;
    if (!item) {
        const QList<VectorItem *> selected = selectedVectorItems();
        for (VectorItem *candidate : selected) {
            if (candidate->hasPath()) {
                item = candidate;
                break;
            }
        }
    }
    if (!item) {
        emit statusHint(QStringLiteral("Выберите объект с контуром."));
        return;
    }

    QVector<QPainterPath::Element> elements = item->pathElements();
    if (elements.size() < 2)
        return;

    const QPointF local = item->mapFromScene(scenePos);
    int insertIndex = -1;
    qreal bestT = 0.5;
    qreal bestDistance = 1e9;

    const auto consider = [&](qreal distance, int index, qreal t) {
        if (distance < bestDistance) {
            bestDistance = distance;
            insertIndex = index;
            bestT = t;
        }
    };

    if (!nearestSegment && !m_selectedNodeElements.isEmpty()) {
        int anchor = anchorForElement(elements, *m_selectedNodeElements.begin());
        if (anchor >= 0) {
            if (anchor + 1 < elements.size()
                && elements.at(anchor + 1).type == QPainterPath::LineToElement) {
                insertIndex = anchor + 1;
                bestT = 0.5;
            } else if (anchor + 3 < elements.size()
                       && elements.at(anchor + 1).type
                              == QPainterPath::CurveToElement) {
                insertIndex = anchor + 3;
                bestT = 0.5;
            }
        }
    }

    if (insertIndex < 0) {
        int anchorIndex = -1;
        for (int i = 0; i < elements.size(); ++i) {
            const auto &element = elements.at(i);
            if (element.type == QPainterPath::MoveToElement) {
                anchorIndex = i;
                continue;
            }
            if (element.type == QPainterPath::LineToElement && anchorIndex >= 0) {
                const QPointF p0(elements.at(anchorIndex).x, elements.at(anchorIndex).y);
                const QPointF p3(element.x, element.y);
                for (int step = 0; step <= 16; ++step) {
                    const qreal t = step / 16.0;
                    const QPointF point = p0 + (p3 - p0) * t;
                    consider(QLineF(item->mapToScene(point), scenePos).length(), i, t);
                }
                anchorIndex = i;
            } else if (element.type == QPainterPath::CurveToElement
                       && i + 2 < elements.size()) {
                const QPointF p0(elements.at(anchorIndex).x, elements.at(anchorIndex).y);
                const QPointF c1(element.x, element.y);
                const QPointF c2(elements.at(i + 1).x, elements.at(i + 1).y);
                const QPointF p3(elements.at(i + 2).x, elements.at(i + 2).y);
                for (int step = 0; step <= 24; ++step) {
                    const qreal t = step / 24.0;
                    consider(QLineF(item->mapToScene(bezierPoint(p0, c1, c2, p3, t)),
                                    scenePos).length(),
                             i + 2, t);
                }
                anchorIndex = i + 2;
                i += 2;
            }
        }
    }

    if (insertIndex < 0 || insertIndex >= elements.size()) {
        emit statusHint(QStringLiteral("Рядом нет сегмента контура."));
        return;
    }

    int newAnchor = -1;
    emit beginEdit();
    if (elements.at(insertIndex).type == QPainterPath::LineToElement) {
        const QPointF end(elements.at(insertIndex).x, elements.at(insertIndex).y);
        int startIndex = insertIndex - 1;
        while (startIndex > 0
               && elements.at(startIndex).type == QPainterPath::CurveToDataElement)
            --startIndex;
        const QPointF start(elements.at(startIndex).x, elements.at(startIndex).y);
        const QPointF mid = start + (end - start) * bestT;
        QPainterPath::Element node;
        node.x = mid.x();
        node.y = mid.y();
        node.type = QPainterPath::LineToElement;
        elements.insert(insertIndex, node);
        newAnchor = insertIndex;
    } else if (insertIndex >= 2
               && elements.at(insertIndex - 2).type
                      == QPainterPath::CurveToElement) {
        const int start = insertIndex - 2;
        const QPointF p0 = start > 0
                               ? QPointF(elements.at(start - 1).x,
                                         elements.at(start - 1).y)
                               : QPointF(elements.at(start).x, elements.at(start).y);
        const QPointF c1(elements.at(start).x, elements.at(start).y);
        const QPointF c2(elements.at(start + 1).x, elements.at(start + 1).y);
        const QPointF p3(elements.at(start + 2).x, elements.at(start + 2).y);
        const qreal t = bestT;
        const QPointF a = p0 + (c1 - p0) * t;
        const QPointF b = c1 + (c2 - c1) * t;
        const QPointF c = c2 + (p3 - c2) * t;
        const QPointF d = a + (b - a) * t;
        const QPointF e = b + (c - b) * t;
        const QPointF middle = d + (e - d) * t;

        QVector<QPainterPath::Element> replacement;
        const auto makeElement = [](qreal x, qreal y, QPainterPath::ElementType type) {
            QPainterPath::Element element;
            element.x = x;
            element.y = y;
            element.type = type;
            return element;
        };
        replacement.append(makeElement(a.x(), a.y(), QPainterPath::CurveToElement));
        replacement.append(makeElement(d.x(), d.y(), QPainterPath::CurveToDataElement));
        replacement.append(makeElement(middle.x(), middle.y(),
                                       QPainterPath::CurveToDataElement));
        replacement.append(makeElement(e.x(), e.y(), QPainterPath::CurveToElement));
        replacement.append(makeElement(c.x(), c.y(), QPainterPath::CurveToDataElement));
        replacement.append(makeElement(p3.x(), p3.y(),
                                       QPainterPath::CurveToDataElement));

        elements.removeAt(start);
        elements.removeAt(start);
        elements.removeAt(start);
        for (int i = replacement.size() - 1; i >= 0; --i)
            elements.insert(start, replacement.at(i));
        newAnchor = start + 2;
    }

    if (newAnchor >= 0) {
        item->setPathElements(elements);
        m_nodeItem = item;
        m_selectedNodeElements.clear();
        m_selectedNodeElements.insert(newAnchor);
        m_nodeElement = newAnchor;
        emit statusHint(QStringLiteral("Узел добавлен."));
    }
    emit endEdit();
}

void CanvasView::addNodeAtSelectedSegment()
{
    insertNodeAt(QPointF(), false);
}

void CanvasView::deleteSelectedNodes()
{
    if (!m_nodeItem || m_selectedNodeElements.isEmpty())
        return;
    QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();

    QSet<int> anchors;
    for (int index : m_selectedNodeElements) {
        const int anchor = anchorForElement(elements, index);
        if (anchor >= 2 && isCurveEndpoint(elements, anchor))
            anchors.insert(anchor);
        else if (anchor >= 0)
            anchors.insert(anchor);
    }

    QList<int> sorted = anchors.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    if (sorted.isEmpty())
        return;

    emit beginEdit();
    for (int anchor : sorted) {
        if (anchor < 0 || anchor >= elements.size())
            continue;
        if (isCurveEndpoint(elements, anchor)) {
            elements.removeAt(anchor);
            elements.removeAt(anchor - 1);
            elements.removeAt(anchor - 2);
        } else {
            elements.removeAt(anchor);
        }
    }

    if (!elements.isEmpty()
        && elements.first().type != QPainterPath::MoveToElement) {
        QPainterPath::Element move = elements.first();
        move.type = QPainterPath::MoveToElement;
        elements.prepend(move);
    }

    m_nodeItem->setPathElements(elements);
    m_selectedNodeElements.clear();
    m_nodeElement = -1;
    emit endEdit();
    emit statusHint(QStringLiteral("Узлы удалены."));
}

void CanvasView::closeSelectedPath()
{
    VectorItem *item = m_nodeItem;
    if (!item) {
        const QList<VectorItem *> selected = selectedVectorItems();
        for (VectorItem *candidate : selected) {
            if (candidate->hasPath()) {
                item = candidate;
                break;
            }
        }
    }
    if (!item || !item->hasPath())
        return;

    QVector<QPainterPath::Element> elements = item->pathElements();
    if (elements.size() < 2)
        return;
    const QPointF first(elements.first().x, elements.first().y);
    const QPointF last(elements.last().x, elements.last().y);
    if (QLineF(first, last).length() < 0.1) {
        emit statusHint(QStringLiteral("Контур уже замкнут."));
        return;
    }

    emit beginEdit();
    QPainterPath::Element node;
    node.x = first.x();
    node.y = first.y();
    node.type = QPainterPath::LineToElement;
    elements.append(node);
    item->setPathElements(elements);
    m_selectedNodeElements.clear();
    emit endEdit();
    emit statusHint(QStringLiteral("Контур замкнут."));
}

void CanvasView::openSelectedPath()
{
    VectorItem *item = m_nodeItem;
    if (!item) {
        const QList<VectorItem *> selected = selectedVectorItems();
        for (VectorItem *candidate : selected) {
            if (candidate->hasPath()) {
                item = candidate;
                break;
            }
        }
    }
    if (!item || !item->hasPath())
        return;

    QVector<QPainterPath::Element> elements = item->pathElements();
    if (elements.size() < 2)
        return;
    const QPointF first(elements.first().x, elements.first().y);
    const QPointF last(elements.last().x, elements.last().y);
    if (QLineF(first, last).length() > 0.1) {
        emit statusHint(QStringLiteral("Контур уже разомкнут."));
        return;
    }

    emit beginEdit();
    if (elements.last().type == QPainterPath::LineToElement) {
        elements.removeLast();
    } else if (elements.size() >= 3
               && elements.last().type == QPainterPath::CurveToDataElement
               && elements.at(elements.size() - 3).type
                      == QPainterPath::CurveToElement) {
        elements.removeLast();
        elements.removeLast();
        elements.removeLast();
    } else {
        elements.removeLast();
    }
    item->setPathElements(elements);
    m_selectedNodeElements.clear();
    emit endEdit();
    emit statusHint(QStringLiteral("Контур разомкнут."));
}

void CanvasView::joinSelectedEndpoints()
{
    const QList<VectorItem *> selected = selectedVectorItems();
    if (selected.size() == 1) {
        closeSelectedPath();
        return;
    }
    if (selected.size() != 2) {
        emit statusHint(QStringLiteral("Для соединения выберите одну или две кривые."));
        return;
    }

    VectorItem *first = selected.at(0);
    VectorItem *second = selected.at(1);
    if (!first->hasPath() || !second->hasPath())
        return;

    const QPainterPath firstScene = first->mapToScene(first->object().path);
    const QPainterPath secondScene = second->mapToScene(second->object().path);
    if (firstScene.isEmpty() || secondScene.isEmpty())
        return;

    QVector<QPainterPath::Element> a;
    QVector<QPainterPath::Element> b;
    for (int i = 0; i < firstScene.elementCount(); ++i)
        a.append(firstScene.elementAt(i));
    for (int i = 0; i < secondScene.elementCount(); ++i)
        b.append(secondScene.elementAt(i));
    if (a.isEmpty() || b.isEmpty())
        return;

    QPainterPath merged;
    merged.moveTo(a.first().x, a.first().y);
    int i = 1;
    while (i < a.size()) {
        if (a.at(i).type == QPainterPath::LineToElement) {
            merged.lineTo(a.at(i).x, a.at(i).y);
            ++i;
        } else if (a.at(i).type == QPainterPath::CurveToElement && i + 2 < a.size()) {
            merged.cubicTo(a.at(i).x, a.at(i).y, a.at(i + 1).x, a.at(i + 1).y,
                           a.at(i + 2).x, a.at(i + 2).y);
            i += 3;
        } else {
            ++i;
        }
    }
    merged.lineTo(b.first().x, b.first().y);
    i = 1;
    while (i < b.size()) {
        if (b.at(i).type == QPainterPath::LineToElement) {
            merged.lineTo(b.at(i).x, b.at(i).y);
            ++i;
        } else if (b.at(i).type == QPainterPath::CurveToElement && i + 2 < b.size()) {
            merged.cubicTo(b.at(i).x, b.at(i).y, b.at(i + 1).x, b.at(i + 1).y,
                           b.at(i + 2).x, b.at(i + 2).y);
            i += 3;
        } else {
            ++i;
        }
    }

    VectorObject object = first->object();
    object.type = ShapeType::Bezier;
    object.path = merged;
    object.pos = QPointF(0, 0);
    object.rotation = 0;
    object.id = 0;
    object.masterId = 0;
    object.group = 0;

    emit beginEdit();
    scene()->removeItem(first);
    delete first;
    scene()->removeItem(second);
    delete second;
    VectorItem *joined = addVectorObject(object, false, true);
    if (joined)
        joined->setSelected(true);
    emit endEdit();
    emit statusHint(QStringLiteral("Кривые соединены."));
}

void CanvasView::breakAtSelectedNode()
{
    if (!m_nodeItem || m_selectedNodeElements.isEmpty()) {
        emit statusHint(QStringLiteral("Выберите узел для разрыва."));
        return;
    }

    QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
    int anchor = anchorForElement(elements, *m_selectedNodeElements.begin());
    if (anchor <= 0 || anchor >= elements.size() - 1) {
        emit statusHint(QStringLiteral("Разорвать можно только внутренний узел."));
        return;
    }

    QVector<QPainterPath::Element> prefix;
    for (int i = 0; i <= anchor; ++i)
        prefix.append(elements.at(i));
    QVector<QPainterPath::Element> suffix;
    QPainterPath::Element move = elements.at(anchor);
    move.type = QPainterPath::MoveToElement;
    suffix.append(move);
    for (int i = anchor + 1; i < elements.size(); ++i)
        suffix.append(elements.at(i));

    VectorObject object = m_nodeItem->object();
    object.id = 0;
    object.masterId = 0;
    object.group = 0;

    VectorObject prefixObject = object;
    QPainterPath prefixPath;
    for (int i = 0; i < prefix.size();) {
        if (prefix.at(i).type == QPainterPath::MoveToElement) {
            prefixPath.moveTo(prefix.at(i).x, prefix.at(i).y);
            ++i;
        } else if (prefix.at(i).type == QPainterPath::LineToElement) {
            prefixPath.lineTo(prefix.at(i).x, prefix.at(i).y);
            ++i;
        } else if (prefix.at(i).type == QPainterPath::CurveToElement
                   && i + 2 < prefix.size()) {
            prefixPath.cubicTo(prefix.at(i).x, prefix.at(i).y,
                               prefix.at(i + 1).x, prefix.at(i + 1).y,
                               prefix.at(i + 2).x, prefix.at(i + 2).y);
            i += 3;
        } else {
            ++i;
        }
    }
    prefixObject.path = prefixPath;
    prefixObject.type = ShapeType::Bezier;
    prefixObject.pos = m_nodeItem->pos();
    prefixObject.rotation = m_nodeItem->rotation();
    prefixObject.cornerRadius = 0;

    VectorObject suffixObject = prefixObject;
    QPainterPath suffixPath;
    for (int i = 0; i < suffix.size();) {
        if (suffix.at(i).type == QPainterPath::MoveToElement) {
            suffixPath.moveTo(suffix.at(i).x, suffix.at(i).y);
            ++i;
        } else if (suffix.at(i).type == QPainterPath::LineToElement) {
            suffixPath.lineTo(suffix.at(i).x, suffix.at(i).y);
            ++i;
        } else if (suffix.at(i).type == QPainterPath::CurveToElement
                   && i + 2 < suffix.size()) {
            suffixPath.cubicTo(suffix.at(i).x, suffix.at(i).y,
                               suffix.at(i + 1).x, suffix.at(i + 1).y,
                               suffix.at(i + 2).x, suffix.at(i + 2).y);
            i += 3;
        } else {
            ++i;
        }
    }
    suffixObject.path = suffixPath;

    emit beginEdit();
    VectorItem *original = m_nodeItem;
    scene()->removeItem(original);
    delete original;
    m_nodeItem = nullptr;
    m_selectedNodeElements.clear();
    addVectorObject(prefixObject, false, true);
    addVectorObject(suffixObject, false, true);
    emit endEdit();
    emit statusHint(QStringLiteral("Кривая разорвана на два объекта."));
}

void CanvasView::setSelectedNodeType(int type)
{
    if (!m_nodeItem || m_selectedNodeElements.isEmpty())
        return;
    QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
    const int anchor = anchorForElement(elements, *m_selectedNodeElements.begin());
    if (anchor < 0) {
        emit statusHint(QStringLiteral("Выберите узел."));
        return;
    }

    const bool hasIncoming = anchor >= 2
                             && elements.at(anchor - 2).type
                                    == QPainterPath::CurveToElement
                             && isCurveEndpoint(elements, anchor);
    const bool hasOutgoing = anchor + 3 < elements.size()
                             && elements.at(anchor + 1).type
                                    == QPainterPath::CurveToElement;
    if (!hasIncoming || !hasOutgoing) {
        emit statusHint(QStringLiteral("У узла нет двух направляющих."));
        return;
    }

    const QPointF a(elements.at(anchor).x, elements.at(anchor).y);
    int inIndex = anchor - 1;
    int outIndex = anchor + 1;
    QPointF inPoint(elements.at(inIndex).x, elements.at(inIndex).y);
    QPointF outPoint(elements.at(outIndex).x, elements.at(outIndex).y);

    if (type == 0)
        return; // cusp — handles keep their independent positions

    QPointF direction = outPoint - a;
    if (QLineF(QPointF(), direction).length() < 0.01)
        direction = a - inPoint;
    const qreal length = QLineF(QPointF(), direction).length();
    if (length < 0.01) {
        emit statusHint(QStringLiteral("Направляющие слишком короткие."));
        return;
    }
    const QPointF unit = direction / length;
    const qreal outLength = type == 2 ? length
                                      : QLineF(a, outPoint).length();
    const qreal inLength = type == 2 ? length
                                     : QLineF(a, inPoint).length();

    emit beginEdit();
    elements[outIndex].x = a.x() + unit.x() * outLength;
    elements[outIndex].y = a.y() + unit.y() * outLength;
    elements[inIndex].x = a.x() - unit.x() * inLength;
    elements[inIndex].y = a.y() - unit.y() * inLength;
    m_nodeItem->setPathElements(elements);
    emit endEdit();
    emit statusHint(type == 1 ? QStringLiteral("Узел сглажен.")
                              : QStringLiteral("Узел симметричен."));
}

void CanvasView::alignSelectedNodes(int mode)
{
    if (!m_nodeItem || m_selectedNodeElements.isEmpty())
        return;
    QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
    QSet<int> anchors;
    for (int index : m_selectedNodeElements) {
        const int anchor = anchorForElement(elements, index);
        if (anchor >= 0)
            anchors.insert(anchor);
    }
    if (anchors.size() < 2) {
        emit statusHint(QStringLiteral("Shift+клик: выберите минимум два узла."));
        return;
    }

    qreal minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (int anchor : anchors) {
        minX = qMin(minX, elements.at(anchor).x);
        maxX = qMax(maxX, elements.at(anchor).x);
        minY = qMin(minY, elements.at(anchor).y);
        maxY = qMax(maxY, elements.at(anchor).y);
    }

    qreal target = 0.0;
    switch (mode) {
    case 0: target = minX; break;
    case 1: target = (minX + maxX) / 2.0; break;
    case 2: target = maxX; break;
    case 3: target = minY; break;
    case 4: target = (minY + maxY) / 2.0; break;
    case 5: target = maxY; break;
    default: return;
    }

    emit beginEdit();
    for (int anchor : anchors) {
        if (mode <= 2)
            elements[anchor].x = target;
        else
            elements[anchor].y = target;
    }
    m_nodeItem->setPathElements(elements);
    emit endEdit();
    emit statusHint(QStringLiteral("Узлы выровнены."));
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    const QPointF scenePos = mapToScene(event->pos());
    const bool panButton = event->button() == Qt::MiddleButton
                           || (event->button() == Qt::LeftButton
                               && (m_spaceHeld || m_tool == Tool::Pan));

    if (panButton) {
        m_panning = true;
        m_panAnchor = event->pos();
        updateCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool == Tool::Zoom) {
        zoomAt(m_zoom * 1.25, event->pos());
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && m_tool == Tool::Zoom) {
        zoomAt(m_zoom / 1.25, event->pos());
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (m_tool == Tool::Select && m_groupPivotValid
        && selectedVectorItems().size() > 1
        && (event->modifiers() & Qt::ControlModifier)) {
        const qreal tolerance = 10.0 / (m_zoom > 0.0 ? m_zoom : 1.0);
        if (QLineF(scenePos, m_groupPivot).length() <= tolerance) {
            m_groupPivotDragging = true;
            m_groupPivotCustom = true;
            event->accept();
            return;
        }
    }

    if (m_tool == Tool::Select && !m_groupPivotDragging
        && (event->modifiers() & Qt::ControlModifier)
        && !(event->modifiers() & Qt::AltModifier)) {
        VectorItem *hit = itemAtScene(scenePos);
        if (hit && hit->groupIndex() > 0 && hit->isVisible()
            && (hit->flags() & QGraphicsItem::ItemIsSelectable)) {
            m_insideGroup = true;
            m_syncingSelection = true;
            scene()->clearSelection();
            hit->setSelected(true);
            m_syncingSelection = false;
            emit selectionChanged(1);
            emit statusHint(QStringLiteral("Ctrl+клик: выбран объект внутри группы."));
            event->accept();
            return;
        }
    }

    if (m_tool == Tool::Select && !(event->modifiers() & Qt::ControlModifier))
        m_insideGroup = false;

    if (m_tool == Tool::Select && (event->modifiers() & Qt::AltModifier)) {
        QList<VectorItem *> candidates;
        for (QGraphicsItem *graphicsItem : scene()->items(scenePos)) {
            if (graphicsItem->type() != VectorItem::ItemType)
                continue;
            auto *candidate = static_cast<VectorItem *>(graphicsItem);
            if (candidate->isVisible()
                && (candidate->flags() & QGraphicsItem::ItemIsSelectable))
                candidates.append(candidate);
        }
        if (!candidates.isEmpty()) {
            const QList<VectorItem *> selected = selectedVectorItems();
            VectorItem *current = selected.isEmpty() ? nullptr : selected.last();
            const int index = candidates.indexOf(current);
            VectorItem *target = candidates.value(index + 1, candidates.first());
            scene()->clearSelection();
            target->setSelected(true);
            emit statusHint(QStringLiteral("Alt+клик: выбран объект ниже (%1)")
                                .arg(index + 2 <= candidates.size()
                                         ? QStringLiteral("уровень %1").arg(index + 2)
                                         : QStringLiteral("верхний")));
        }
        event->accept();
        return;
    }

    switch (m_tool) {
    case Tool::Select:
        QGraphicsView::mousePressEvent(event);
        break;
    case Tool::Node:
        if (!nodePress(scenePos))
            QGraphicsView::mousePressEvent(event);
        break;
    case Tool::Knife:
        m_knifeActive = true;
        m_knifeStart = scenePos;
        m_knifeCurrent = scenePos;
        viewport()->update();
        break;
    case Tool::Eraser:
        m_eraserActive = true;
        m_eraserPoints = {scenePos};
        viewport()->update();
        break;
    case Tool::ShapeBuilder:
        shapeBuilderPress(scenePos);
        break;
    case Tool::Envelope:
        if (envelopePress(scenePos)) {
            viewport()->update();
        } else if (m_envelopeItem) {
            viewport()->update();
        }
        break;
    case Tool::Rectangle:
        startDrawing(DrawMode::Rectangle, scenePos);
        break;
    case Tool::Ellipse:
        startDrawing(DrawMode::Ellipse, scenePos);
        break;
    case Tool::Polygon:
        startDrawing(DrawMode::Polygon, scenePos);
        break;
    case Tool::Star:
        startDrawing(DrawMode::Star, scenePos);
        break;
    case Tool::Spiral:
        startDrawing(DrawMode::Spiral, scenePos);
        break;
    case Tool::Line:
        startDrawing(DrawMode::Line, scenePos);
        break;
    case Tool::Freehand:
        startDrawing(DrawMode::Freehand, scenePos);
        break;
    case Tool::Artistic:
        startDrawing(DrawMode::Artistic, scenePos);
        break;
    case Tool::Bezier: {
        if (m_bezierActive && m_bezierNodes.size() >= 2) {
            const qreal closeDistance = 10.0 / (m_zoom > 0.0 ? m_zoom : 1.0);
            if (QLineF(scenePos, m_bezierNodes.first().anchor).length()
                <= closeDistance) {
                m_bezierClosed = true;
                finishBezier();
                break;
            }
        }
        addBezierNode(scenePos);
        break;
    }
    case Tool::Text:
        createTextAt(scenePos);
        break;
    case Tool::Eyedropper: {
        QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
        pixel.fill(Qt::white);
        QPainter painter(&pixel);
        scene()->render(&painter, QRectF(0, 0, 1, 1), QRectF(scenePos, QSizeF(1, 1)));
        painter.end();
        emit colorPicked(pixel.pixelColor(0, 0));
        break;
    }
    case Tool::Fill:
        applyFillAt(scenePos);
        break;
    case Tool::Outline:
        applyOutlineAt(scenePos);
        break;
    case Tool::Shadow:
        shadowPress(scenePos);
        break;
    case Tool::Blend:
        blendSelected();
        break;
    case Tool::Dimension:
        dimensionPress(scenePos);
        break;
    case Tool::Perspective:
        perspectivePress(scenePos);
        break;
    case Tool::Extrude:
        extrudePress(scenePos);
        break;
    case Tool::Transparency: {
        VectorItem *item = itemAtScene(scenePos);
        if (!item) {
            emit statusHint(QStringLiteral("Щёлкните по объекту и потяните влево, "
                                           "чтобы изменить прозрачность."));
            break;
        }
        m_transparencyItem = item;
        m_transparencyStartValue = item->object().opacity;
        m_transparencyStartX = scenePos.x();
        m_transparencyDragging = true;
        emit beginEdit();
        break;
    }
    case Tool::Crop:
        startDrawing(DrawMode::Crop, scenePos);
        break;
    case Tool::Pan:
    case Tool::Zoom:
        break;
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF scenePos = mapToScene(event->pos());

    if (m_tool == Tool::Dimension) {
        m_dimensionCursor = snapNodePoint(scenePos, nullptr);
        m_dimensionHasCursor = true;
        viewport()->update();
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_panAnchor;
        m_panAnchor = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_groupPivotDragging) {
        m_groupPivot = scenePos;
        viewport()->update();
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_perspectiveCorner >= 0) {
        perspectiveMove(scenePos);
        return;
    }

    if (m_extrudeDragging) {
        extrudeMove(scenePos);
        return;
    }

    if (m_shadowDragging) {
        shadowMove(scenePos);
        return;
    }

    if (m_tool == Tool::Select && m_groupPivotValid && !m_groupPivotCustom
        && selectedVectorItems().size() > 1
        && (event->buttons() & Qt::LeftButton)) {
        QRectF bounds;
        for (VectorItem *item : selectedVectorItems()) {
            const QRectF visual = item->mapRectToScene(item->localBounds());
            bounds = bounds.isNull() ? visual : bounds.united(visual);
        }
        if (!bounds.isNull())
            m_groupPivot = bounds.center();
        viewport()->update();
    }

    if (m_nodeDragging && m_nodeItem && !m_selectedNodeElements.isEmpty()) {
        const QPointF currentLocal = m_nodeItem->mapFromScene(scenePos);
        const QPointF lastLocal = m_nodeItem->mapFromScene(m_nodeLastScene);
        const QPointF delta = currentLocal - lastLocal;
        m_nodeLastScene = scenePos;
        QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
        QPointF adjustedDelta = delta;
        if (m_snapEnabled && !m_selectedNodeElements.isEmpty()) {
            const int primary = m_nodeElement >= 0 ? m_nodeElement
                                                   : *m_selectedNodeElements.begin();
            if (primary >= 0 && primary < elements.size()) {
                const QPointF proposed(elements.at(primary).x + delta.x(),
                                       elements.at(primary).y + delta.y());
                const QPointF scenePoint = m_nodeItem->mapToScene(proposed);
                const QPointF snapped = snapNodePoint(scenePoint, m_nodeItem);
                const QPointF adjustment =
                    m_nodeItem->mapFromScene(snapped + (scenePoint - scenePoint))
                    - m_nodeItem->mapFromScene(scenePoint);
                adjustedDelta += adjustment;
            }
        } else {
            setSmartGuides({});
        }
        for (int index : m_selectedNodeElements) {
            if (index >= 0 && index < elements.size()) {
                elements[index].x += adjustedDelta.x();
                elements[index].y += adjustedDelta.y();
            }
        }
        m_nodeItem->setPathElements(elements);
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_transparencyDragging && m_transparencyItem) {
        const qreal delta = (scenePos.x() - m_transparencyStartX) / 200.0;
        m_transparencyItem->setObjectOpacity(m_transparencyStartValue - delta);
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_tool == Tool::Bezier) {
        m_bezierCursor = scenePos;
        m_bezierHasCursor = true;

        if (m_bezierDragging && !m_bezierNodes.isEmpty()) {
            BezierNode &node = m_bezierNodes.last();
            QPointF outPoint = scenePos;

            if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) {
                const QLineF line(node.anchor, scenePos);
                const qreal step = kPi / 12.0;
                const qreal length = line.length();
                const qreal angle = std::atan2(-line.dy(), line.dx());
                const qreal snapped = qRound(angle / step) * step;
                outPoint = node.anchor + QPointF(std::cos(snapped) * length,
                                                 -std::sin(snapped) * length);
            }

            if (QLineF(node.anchor, outPoint).length() > 0.5)
                m_bezierDragMoved = true;

            if (QGuiApplication::keyboardModifiers() & Qt::AltModifier)
                node.mode = BezierNodeMode::Cusp;

            node.out = outPoint;
            switch (node.mode) {
            case BezierNodeMode::Symmetric:
                node.in = node.anchor * 2.0 - outPoint;
                break;
            case BezierNodeMode::Smooth: {
                const QPointF direction = outPoint - node.anchor;
                const qreal length = std::hypot(direction.x(), direction.y());
                if (length > 0.01) {
                    qreal inLength = QLineF(node.anchor, node.in).length();
                    if (inLength < 0.5)
                        inLength = length;
                    node.in = node.anchor - direction / length * inLength;
                }
                break;
            }
            case BezierNodeMode::Cusp:
                break;
            }
            viewport()->update();
        }
    }

    if (m_knifeActive) {
        m_knifeCurrent = scenePos;
        viewport()->update();
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_eraserActive) {
        if (m_eraserPoints.isEmpty()
            || QLineF(m_eraserPoints.last(), scenePos).length() > 1.0)
            m_eraserPoints.append(scenePos);
        viewport()->update();
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_shapeDragging) {
        shapeBuilderMove(scenePos);
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_envelopeDragIndex >= 0) {
        envelopeMove(scenePos);
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_drawing) {
        m_drawCurrent = scenePos;
        if ((m_drawMode == DrawMode::Freehand || m_drawMode == DrawMode::Artistic)
            && !m_freehandPoints.isEmpty()
            && QLineF(m_freehandPoints.last(), scenePos).length() > 1.5) {
            m_freehandPoints.append(scenePos);
        }
        viewport()->update();
    }

    emit scenePosChanged(scenePos);
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        updateCursor();
        event->accept();
        return;
    }

    if (m_groupPivotDragging) {
        m_groupPivotDragging = false;
        event->accept();
        return;
    }

    if (m_perspectiveCorner >= 0) {
        perspectiveRelease();
        event->accept();
        return;
    }

    if (m_extrudeDragging) {
        extrudeRelease();
        event->accept();
        return;
    }

    if (m_shadowDragging) {
        shadowRelease();
        event->accept();
        return;
    }

    if (m_nodeDragging) {
        m_nodeDragging = false;
        setSmartGuides({});
        emit endEdit();
        event->accept();
        return;
    }

    setSmartGuides({});

    if (m_transparencyDragging) {
        m_transparencyDragging = false;
        m_transparencyItem = nullptr;
        emit endEdit();
        emit statusHint(QStringLiteral("Прозрачность изменена."));
        event->accept();
        return;
    }

    if (m_bezierDragging) {
        if (!m_bezierDragMoved && !m_bezierNodes.isEmpty()) {
            BezierNode &node = m_bezierNodes.last();
            node.in = node.anchor;
            node.out = node.anchor;
            node.mode = BezierNodeMode::Cusp;
        }
        m_bezierDragging = false;
        m_bezierDragMoved = false;
        viewport()->update();
        event->accept();
        return;
    }

    if (m_knifeActive) {
        m_knifeCurrent = mapToScene(event->pos());
        m_knifeActive = false;
        applyKnifeCut(QLineF(m_knifeStart, m_knifeCurrent));
        viewport()->update();
        event->accept();
        return;
    }

    if (m_eraserActive) {
        const QPointF last = mapToScene(event->pos());
        if (m_eraserPoints.isEmpty()
            || QLineF(m_eraserPoints.last(), last).length() > 1.0)
            m_eraserPoints.append(last);
        m_eraserActive = false;
        applyEraserStroke(m_eraserPoints);
        m_eraserPoints.clear();
        viewport()->update();
        event->accept();
        return;
    }

    if (m_shapeDragging) {
        shapeBuilderRelease(mapToScene(event->pos()),
                            event->modifiers() & Qt::AltModifier);
        event->accept();
        return;
    }

    if (m_envelopeDragIndex >= 0) {
        m_envelopeDragIndex = -1;
        if (m_envelopeEditing) {
            m_envelopeEditing = false;
            emit endEdit();
        }
        viewport()->update();
        event->accept();
        return;
    }

    if (m_drawing) {
        finishDrawing(mapToScene(event->pos()));
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_tool == Tool::Select) {
        VectorItem *item = itemAtScene(mapToScene(event->pos()));
        if (item && item->object().type == ShapeType::Code) {
            emit codeEditRequested(item);
            event->accept();
            return;
        }
    }

    if (m_tool == Tool::Perspective) {
        const QPointF scenePos = mapToScene(event->pos());
        VectorItem *item = itemAtScene(scenePos);
        if (item && item->object().effects.size() > 0) {
            const int index = perspectiveEffectIndex(item);
            if (index >= 0) {
                QVector<Effect> effects = item->effects();
                effects[index].quad = identityQuad();
                emit beginEdit();
                item->setEffects(effects);
                emit endEdit();
                m_perspectiveItem = item;
                viewport()->update();
                emit statusHint(QStringLiteral("Перспектива сброшена."));
                event->accept();
                return;
            }
        }
    }

    if (m_tool == Tool::Envelope && m_envelopeItem) {
        resetEnvelope();
        emit statusHint(QStringLiteral("Конверт сброшен."));
        event->accept();
        return;
    }

    if (m_tool == Tool::Node) {
        const QPointF scenePos = mapToScene(event->pos());
        VectorItem *item = itemAtScene(scenePos);
        if (item && item->hasPath()) {
            scene()->clearSelection();
            item->setSelected(true);
            m_nodeItem = item;
            m_selectedNodeElements.clear();
            insertNodeAt(scenePos, true);
            event->accept();
            return;
        }
    }

    if (m_tool == Tool::Bezier && m_bezierActive) {
        if (m_bezierNodes.size() >= 2) {
            const BezierNode last = m_bezierNodes.last();
            const BezierNode previous = m_bezierNodes.at(m_bezierNodes.size() - 2);
            if (QLineF(last.anchor, previous.anchor).length() < 4.0)
                m_bezierNodes.removeLast();
        }
        finishBezier();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !m_spaceHeld) {
        m_spaceHeld = true;
        updateCursor();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && m_tool == Tool::Node && !m_selectedNodeElements.isEmpty()) {
        deleteSelectedNodes();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace && m_tool == Tool::Bezier
        && m_bezierActive) {
        deleteLastBezierNode();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_bezierActive) {
            finishBezier();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_tool == Tool::Dimension && !m_dimensionPoints.isEmpty()) {
            m_dimensionPoints.clear();
            m_dimensionRefs.clear();
            m_dimensionAnchors.clear();
            m_dimensionHasCursor = false;
            setSmartGuides({});
            viewport()->update();
            emit statusHint(QStringLiteral("Построение размера отменено."));
            event->accept();
            return;
        }
        if (m_bezierActive) {
            cancelBezier();
            event->accept();
            return;
        }
        if (m_drawing) {
            m_drawing = false;
            m_drawMode = DrawMode::None;
            m_freehandPoints.clear();
            viewport()->update();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        m_spaceHeld = false;
        updateCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}
