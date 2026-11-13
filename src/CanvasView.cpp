#include "CanvasView.h"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

namespace {
constexpr qreal kPageWidth = 794.0;   // A4 at 96 dpi
constexpr qreal kPageHeight = 1123.0;
constexpr qreal kMargin = 400.0;
constexpr qreal kMinZoom = 0.02;
constexpr qreal kMaxZoom = 32.0;
} // namespace

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setSceneRect(-kMargin, -kMargin, kPageWidth + 2 * kMargin, kPageHeight + 2 * kMargin);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(0x17, 0x18, 0x1b));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    updateCursor();
}

QRectF CanvasView::pageRect() const
{
    return QRectF(0.0, 0.0, kPageWidth, kPageHeight);
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

    horizontalScrollBar()->setValue(
        horizontalScrollBar()->value() + qRound(delta.x() * m_zoom));
    verticalScrollBar()->setValue(
        verticalScrollBar()->value() + qRound(delta.y() * m_zoom));

    emit zoomChanged(m_zoom);
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

void CanvasView::setShowGrid(bool on)
{
    m_grid = on;
    viewport()->update();
}

void CanvasView::setActiveTool(Tool tool)
{
    m_tool = tool;
    updateCursor();
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

    // Soft drop shadow under the page.
    for (int i = 7; i >= 1; --i) {
        const QRectF shadow = page.adjusted(-i * 1.5, -i * 1.5 + 4.0, i * 1.5, i * 1.5 + 4.0);
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

    // Printable area hint, 10 mm from the page edge.
    constexpr qreal margin = 37.8;
    QPen marginPen(QColor(0x4f, 0x7c, 0xc0, 70), 1.0);
    marginPen.setStyle(Qt::DashLine);
    painter->setPen(marginPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(page.adjusted(margin, margin, -margin, -margin));

    painter->setPen(QPen(QColor(0xc8, 0xce, 0xd8), 1.0));
    painter->drawRect(page);

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

void CanvasView::mousePressEvent(QMouseEvent *event)
{
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

    if (event->button() == Qt::LeftButton && m_tool != Tool::Select
        && m_tool != Tool::Node) {
        const ToolInfo *info = toolInfo(m_tool);
        if (info) {
            emit statusHint(QStringLiteral("Инструмент «%1» выбран. Рисование появится "
                                          "на следующем этапе — сейчас собран интерфейс.")
                                .arg(info->name));
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_panAnchor;
        m_panAnchor = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    emit scenePosChanged(mapToScene(event->pos()));
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
    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !m_spaceHeld) {
        m_spaceHeld = true;
        updateCursor();
        event->accept();
        return;
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
