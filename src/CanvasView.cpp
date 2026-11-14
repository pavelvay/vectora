#include "CanvasView.h"

#include <QColorDialog>
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
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QtMath>
#include <QSet>

#include <algorithm>

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

    horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                    + qRound(delta.x() * m_zoom));
    verticalScrollBar()->setValue(verticalScrollBar()->value()
                                  + qRound(delta.y() * m_zoom));

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
    painter->setBrush(Qt::white);
    painter->drawRect(page);

    if (m_grid) {
        painter->setPen(QPen(QColor(0x4f, 0x7c, 0xc0, 26), 1.0));
        const qreal step = qMax<qreal>(1.0, m_gridStep);
        for (qreal x = page.left() + step; x < page.right(); x += step)
            painter->drawLine(QPointF(x, page.top()), QPointF(x, page.bottom()));
        for (qreal y = page.top() + step; y < page.bottom(); y += step)
            painter->drawLine(QPointF(page.left(), y), QPointF(page.right(), y));
    }

    constexpr qreal margin = kPxPerMm * 10.0;
    QPen marginPen(QColor(0x4f, 0x7c, 0xc0, 70), 1.0);
    marginPen.setStyle(Qt::DashLine);
    painter->setPen(marginPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(page.adjusted(margin, margin, -margin, -margin));

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

        // Rubber band to the cursor, like CorelDRAW's rubber-band preview.
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

    setPageSize(document.pageSize);
    for (const VectorObject &object : document.objects)
        addVectorObject(object, false, false);
    normalizeZOrder();
    for (int i = 0; i < m_layers.size(); ++i)
        applyLayerState(i);
    updateSnapSettings();
    emit selectionChanged(0);
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
        item->setVisible(visible);
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

QPointF CanvasView::snapNodePoint(const QPointF &scenePos, VectorItem *exclude) const
{
    if (!m_snapEnabled)
        return scenePos;

    QVector<qreal> xs = m_guidesX;
    QVector<qreal> ys = m_guidesY;
    const QRectF page = scene()->property("pageRect").toRectF();
    xs << page.left() << page.center().x() << page.right();
    ys << page.top() << page.center().y() << page.bottom();

    for (VectorItem *item : vectorItems()) {
        if (item == exclude || !item->isVisible())
            continue;
        const QRectF bounds = item->mapRectToScene(item->localBounds());
        xs << bounds.left() << bounds.center().x() << bounds.right();
        ys << bounds.top() << bounds.center().y() << bounds.bottom();
        const QVector<QPainterPath::Element> elements = item->pathElements();
        for (const auto &element : elements) {
            const QPointF node = item->mapToScene(QPointF(element.x, element.y));
            xs << node.x();
            ys << node.y();
        }
    }

    qreal bestX = scenePos.x();
    qreal bestY = scenePos.y();
    qreal bestDx = m_snapThreshold + 1.0;
    qreal bestDy = m_snapThreshold + 1.0;
    const qreal grid = qMax<qreal>(1.0, m_gridStep);

    const qreal gridX = qRound(scenePos.x() / grid) * grid;
    if (qAbs(gridX - scenePos.x()) < bestDx) {
        bestDx = qAbs(gridX - scenePos.x());
        bestX = gridX;
    }
    const qreal gridY = qRound(scenePos.y() / grid) * grid;
    if (qAbs(gridY - scenePos.y()) < bestDy) {
        bestDy = qAbs(gridY - scenePos.y());
        bestY = gridY;
    }
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

    QVector<QLineF> lines;
    const QRectF area = sceneRect();
    if (bestDx <= m_snapThreshold)
        lines.append(QLineF(bestX, area.top(), bestX, area.bottom()));
    if (bestDy <= m_snapThreshold)
        lines.append(QLineF(area.left(), bestY, area.right(), bestY));
    const_cast<CanvasView *>(this)->setSmartGuides(lines);

    return QPointF(bestDx <= m_snapThreshold ? bestX : scenePos.x(),
                   bestDy <= m_snapThreshold ? bestY : scenePos.y());
}

void CanvasView::updateSnapSettings()
{
    for (VectorItem *item : vectorItems())
        item->setSnapSettings(m_snapEnabled, m_gridStep, m_guidesX, m_guidesY,
                              m_snapThreshold);
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
    if (selected.size() != 2) {
        emit statusHint(QStringLiteral("Для перетекания выделите ровно два объекта."));
        return;
    }

    VectorItem *from = selected.at(0);
    VectorItem *to = selected.at(1);
    const VectorObject base = from->object();
    const VectorObject target = to->object();
    const QRectF fromBounds = base.localBounds();
    const QRectF toBounds = target.localBounds();

    emit beginEdit();
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / qreal(steps + 1);
        VectorObject object = base;
        object.pos = base.pos + (target.pos - base.pos) * t;
        object.rotation = base.rotation + (target.rotation - base.rotation) * t;
        object.fill = interpolateColor(base.fill, target.fill, t);
        object.stroke = interpolateColor(base.stroke, target.stroke, t);
        object.opacity = base.opacity + (target.opacity - base.opacity) * t;
        object.shadow = false;

        if (fromBounds.width() > 1.0 && fromBounds.height() > 1.0
            && !object.path.isEmpty()) {
            const qreal scaleX = 1.0 + (toBounds.width() / fromBounds.width() - 1.0) * t;
            const qreal scaleY = 1.0 + (toBounds.height() / fromBounds.height() - 1.0) * t;
            QTransform transform;
            transform.translate(fromBounds.center().x(), fromBounds.center().y());
            transform.scale(scaleX, scaleY);
            transform.translate(-fromBounds.center().x(), -fromBounds.center().y());
            object.path = transform.map(base.path);
        }
        addVectorObject(object, false, false);
    }
    emit endEdit();
    emit statusHint(QStringLiteral("Создано %1 промежуточных объектов.").arg(steps));
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
        toggleShadowAt(scenePos);
        break;
    case Tool::Blend:
        blendSelected();
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

    if (m_drawing) {
        finishDrawing(mapToScene(event->pos()));
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
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
