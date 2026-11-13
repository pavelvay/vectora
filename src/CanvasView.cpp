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
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

#include "VectorItem.h"

namespace {
constexpr qreal kMargin = 400.0;
constexpr qreal kMinZoom = 0.02;
constexpr qreal kMaxZoom = 32.0;
constexpr qreal kPxPerMm = 3.7795275591;
constexpr qreal kPi = 3.14159265358979323846;

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
        emit selectionChanged(selectedVectorItems().size());
    });

    updateCursor();
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
        constexpr qreal step = 20.0;
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
    drawPreview(painter);
    drawNodeOverlay(painter);
}

void CanvasView::drawPreview(QPainter *painter)
{
    if (m_bezierActive && m_bezierNodes.size() >= 1) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(previewPen());
        painter->setBrush(Qt::NoBrush);
        if (m_bezierNodes.size() >= 2)
            painter->drawPath(buildBezierPath());

        const qreal s = transform().m11() > 0 ? transform().m11() : 1.0;
        const qreal size = 7.0 / s;
        for (const BezierNode &node : m_bezierNodes) {
            painter->setPen(QPen(QColor(0x7f, 0xae, 0xf7), 1.2));
            painter->setBrush(QColor(0x1b, 0x1d, 0x21));
            painter->drawRect(QRectF(node.anchor - QPointF(size / 2, size / 2),
                                     QSizeF(size, size)));
            painter->setBrush(QColor(0xf2, 0xf5, 0xfa));
            painter->drawEllipse(node.out, size / 4.0, size / 4.0);
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
            if (m_drawMode == DrawMode::Polygon) {
                QPainterPath path;
                for (int i = 0; i < 6; ++i) {
                    const qreal angle = -kPi / 2.0 + i * 2.0 * kPi / 6.0;
                    const QPointF p(m_drawStart.x() + radius * std::cos(angle),
                                    m_drawStart.y() + radius * std::sin(angle));
                    if (i == 0)
                        path.moveTo(p);
                    else
                        path.lineTo(p);
                }
                path.closeSubpath();
                painter->drawPath(path);
            } else if (m_drawMode == DrawMode::Star) {
                QPainterPath path;
                const qreal inner = radius * 0.45;
                for (int i = 0; i < 10; ++i) {
                    const qreal r = (i % 2 == 0) ? radius : inner;
                    const qreal angle = -kPi / 2.0 + i * kPi / 5.0;
                    const QPointF p(m_drawStart.x() + r * std::cos(angle),
                                    m_drawStart.y() + r * std::sin(angle));
                    if (i == 0)
                        path.moveTo(p);
                    else
                        path.lineTo(p);
                }
                path.closeSubpath();
                painter->drawPath(path);
            } else {
                QPainterPath path;
                const int steps = 500;
                const qreal turns = 3.0;
                for (int i = 0; i <= steps; ++i) {
                    const qreal t = qreal(i) / steps;
                    const qreal angle = t * turns * 2.0 * kPi;
                    const qreal r = radius * t;
                    const QPointF p(m_drawStart.x() + r * std::cos(angle),
                                    m_drawStart.y() + r * std::sin(angle));
                    if (i == 0)
                        path.moveTo(p);
                    else
                        path.lineTo(p);
                }
                painter->drawPath(path);
            }
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
                painter->setBrush(QColor(0xffffff));
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
                painter->setBrush(QColor(0xffffff));
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
    if (record)
        emit beginEdit();

    auto *item = new VectorItem(object);
    scene()->addItem(item);
    item->setZValue(m_nextZ++);
    connect(item, &VectorItem::editStarted, this, &CanvasView::beginEdit);
    connect(item, &VectorItem::editFinished, this, &CanvasView::endEdit);
    connect(item, &VectorItem::textEditRequested, this, &CanvasView::editTextItem);

    if (select) {
        scene()->clearSelection();
        item->setSelected(true);
    }

    if (record)
        emit endEdit();

    emit selectionChanged(selectedVectorItems().size());
    return item;
}

void CanvasView::setDocument(const Document &document)
{
    scene()->clear();
    m_nextZ = 1.0;
    m_bezierActive = false;
    m_bezierNodes.clear();
    setPageSize(document.pageSize);
    for (const VectorObject &object : document.objects)
        addVectorObject(object, false, false);
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
    const QList<VectorItem *> items = selectedVectorItems();
    if (items.isEmpty()) {
        emit statusHint(QStringLiteral("Нет выделенных объектов."));
        return;
    }
    emit beginEdit();
    for (VectorItem *item : items) {
        scene()->removeItem(item);
        delete item;
    }
    emit endEdit();
    emit selectionChanged(0);
    viewport()->update();
}

void CanvasView::selectAllObjects()
{
    const QList<VectorItem *> items = vectorItems();
    for (VectorItem *item : items)
        item->setSelected(true);
    emit selectionChanged(items.size());
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
        if (m_drawMode == DrawMode::Rectangle)
            object.path.addRect(QRectF(0.0, 0.0, rect.width(), rect.height()));
        else
            object.path.addEllipse(QRectF(0.0, 0.0, rect.width(), rect.height()));
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

        if (m_drawMode == DrawMode::Polygon) {
            VectorObject object = baseObject(ShapeType::Polygon);
            for (int i = 0; i < 6; ++i) {
                const qreal angle = -kPi / 2.0 + i * 2.0 * kPi / 6.0;
                const QPointF p(radius * std::cos(angle), radius * std::sin(angle));
                if (i == 0)
                    object.path.moveTo(p);
                else
                    object.path.lineTo(p);
            }
            object.path.closeSubpath();
            object.pos = start;
            addVectorObject(object);
        } else if (m_drawMode == DrawMode::Star) {
            VectorObject object = baseObject(ShapeType::Star);
            const qreal inner = radius * 0.45;
            for (int i = 0; i < 10; ++i) {
                const qreal r = (i % 2 == 0) ? radius : inner;
                const qreal angle = -kPi / 2.0 + i * kPi / 5.0;
                const QPointF p(r * std::cos(angle), r * std::sin(angle));
                if (i == 0)
                    object.path.moveTo(p);
                else
                    object.path.lineTo(p);
            }
            object.path.closeSubpath();
            object.pos = start;
            addVectorObject(object);
        } else {
            VectorObject object = baseObject(ShapeType::Spiral);
            object.hasFill = false;
            const int steps = 720;
            const qreal turns = 3.0;
            for (int i = 0; i <= steps; ++i) {
                const qreal t = qreal(i) / steps;
                const qreal angle = t * turns * 2.0 * kPi;
                const qreal r = radius * t;
                const QPointF p(r * std::cos(angle), r * std::sin(angle));
                if (i == 0)
                    object.path.moveTo(p);
                else
                    object.path.lineTo(p);
            }
            object.pos = start;
            object.strokeWidth = qMax<qreal>(m_strokeWidth, 1.5);
            addVectorObject(object);
        }
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
    m_bezierNodes.append({scenePos, scenePos, scenePos});
    viewport()->update();
}

void CanvasView::finishBezier()
{
    if (!m_bezierActive)
        return;

    m_bezierActive = false;
    m_bezierDragging = false;

    if (m_bezierNodes.size() >= 2) {
        VectorObject object;
        object.type = ShapeType::Bezier;
        object.path = buildBezierPath();
        object.pos = QPointF(0.0, 0.0);
        object.hasFill = false;
        object.stroke = m_stroke;
        object.strokeWidth = qMax<qreal>(m_strokeWidth, 1.0);
        object.hasStroke = true;
        addVectorObject(object);
    }

    m_bezierNodes.clear();
    viewport()->update();
}

void CanvasView::cancelBezier()
{
    m_bezierActive = false;
    m_bezierDragging = false;
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

    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, QStringLiteral("Текст"), QStringLiteral("Введите текст:"),
        item->object().text, &ok);
    if (!ok)
        return;

    emit beginEdit();
    item->setText(text);
    emit endEdit();
    emit statusHint(QStringLiteral("Текст обновлён."));
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

void CanvasView::blendSelected()
{
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
    for (int i = 1; i < 8; ++i) {
        const qreal t = i / 8.0;
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
    emit statusHint(QStringLiteral("Создано 7 промежуточных объектов."));
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
                emit beginEdit();
                m_nodeDragging = true;
                m_nodeItem = item;
                m_nodeElement = i;
                return true;
            }
        }
    }
    return false;
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
    case Tool::Bezier:
        addBezierNode(scenePos);
        break;
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

    if (m_nodeDragging && m_nodeItem && m_nodeElement >= 0) {
        const QPointF local = m_nodeItem->mapFromScene(scenePos);
        QVector<QPainterPath::Element> elements = m_nodeItem->pathElements();
        if (m_nodeElement < elements.size()) {
            elements[m_nodeElement].x = local.x();
            elements[m_nodeElement].y = local.y();
            m_nodeItem->setPathElements(elements);
        }
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_transparencyDragging && m_transparencyItem) {
        const qreal delta = (scenePos.x() - m_transparencyStartX) / 200.0;
        m_transparencyItem->setObjectOpacity(m_transparencyStartValue - delta);
        emit scenePosChanged(scenePos);
        return;
    }

    if (m_bezierDragging && !m_bezierNodes.isEmpty()) {
        BezierNode &node = m_bezierNodes.last();
        node.out = scenePos;
        node.in = node.anchor * 2.0 - scenePos;
        viewport()->update();
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

    if (m_nodeDragging) {
        m_nodeDragging = false;
        m_nodeItem = nullptr;
        m_nodeElement = -1;
        emit endEdit();
        event->accept();
        return;
    }

    if (m_transparencyDragging) {
        m_transparencyDragging = false;
        m_transparencyItem = nullptr;
        emit endEdit();
        emit statusHint(QStringLiteral("Прозрачность изменена."));
        event->accept();
        return;
    }

    if (m_bezierDragging) {
        m_bezierDragging = false;
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
