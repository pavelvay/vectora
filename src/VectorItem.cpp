#include "VectorItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
constexpr qreal kHandleSize = 9.0;
constexpr qreal kRotationOffset = 24.0;
constexpr qreal kBoundsMargin = 30.0;
constexpr qreal kPi = 3.14159265358979323846;
} // namespace

VectorItem::VectorItem(const VectorObject &object, QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_object(object)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setPos(m_object.pos);
    setRotation(m_object.rotation);
    updateOrigin();
}

QRectF VectorItem::boundingRect() const
{
    const QRectF bounds = m_object.localBounds();
    const qreal margin = kBoundsMargin + m_object.strokeWidth / 2.0;
    return bounds.adjusted(-margin, -margin, margin, margin);
}

QPainterPath VectorItem::shape() const
{
    QPainterPath result;
    if (!m_object.path.isEmpty()) {
        QPainterPathStroker stroker;
        stroker.setWidth(qMax<qreal>(m_object.strokeWidth, 8.0));
        result = stroker.createStroke(m_object.path);
        if (m_object.hasFill)
            result = result.united(m_object.path);
    } else {
        result.addRect(m_object.textBox);
    }

    // Handles live outside the geometry, so they must be clickable too.
    if (isSelected()) {
        const QVector<QPointF> points = handlePoints();
        for (int i = 0; i < 8; ++i) {
            result.addRect(QRectF(points.at(i).x() - kHandleSize,
                                  points.at(i).y() - kHandleSize,
                                  kHandleSize * 2.0, kHandleSize * 2.0));
        }
        result.addEllipse(points.at(8), 7.0, 7.0);
    }
    return result;
}

void VectorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *)
{
    Q_UNUSED(option)
    Document::paintObject(painter, m_object);
    if (isSelected())
        drawSelection(painter);
}

void VectorItem::drawSelection(QPainter *painter) const
{
    const QRectF bounds = m_object.localBounds();

    painter->save();
    QPen frame(QColor(0x6f, 0x9e, 0xe8));
    frame.setStyle(Qt::DashLine);
    frame.setWidthF(0.0);
    painter->setPen(frame);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bounds.adjusted(-2.0, -2.0, 2.0, 2.0));

    QPen handlePen(QColor(0x6f, 0x9e, 0xe8));
    handlePen.setWidthF(1.3);
    const QVector<QPointF> points = handlePoints();
    for (int i = 0; i < 8; ++i) {
        painter->setPen(handlePen);
        painter->setBrush(QColor(0xffffff));
        painter->drawRect(QRectF(points.at(i) - QPointF(kHandleSize / 2.0, kHandleSize / 2.0),
                                 QSizeF(kHandleSize, kHandleSize)));
    }

    painter->setPen(handlePen);
    painter->drawLine(QPointF(bounds.center().x(), bounds.top()),
                      points.at(8));
    painter->setBrush(QColor(0x4f, 0x7c, 0xc0));
    painter->drawEllipse(points.at(8), 5.0, 5.0);
    painter->restore();
}

QVector<QPointF> VectorItem::handlePoints() const
{
    const QRectF b = m_object.localBounds();
    return {
        b.topLeft(),
        b.topRight(),
        b.bottomRight(),
        b.bottomLeft(),
        QPointF(b.center().x(), b.top()),
        QPointF(b.right(), b.center().y()),
        QPointF(b.center().x(), b.bottom()),
        QPointF(b.left(), b.center().y()),
        QPointF(b.center().x(), b.top() - kRotationOffset),
    };
}

int VectorItem::handleAt(const QPointF &pos) const
{
    const QVector<QPointF> points = handlePoints();
    const qreal threshold = kHandleSize;
    for (int i = 0; i < points.size(); ++i) {
        if (QLineF(pos, points.at(i)).length() <= threshold)
            return i;
    }
    return -1;
}

VectorObject VectorItem::object() const
{
    VectorObject copy = m_object;
    copy.pos = pos();
    copy.rotation = rotation();
    return copy;
}

void VectorItem::setObject(const VectorObject &object)
{
    prepareGeometryChange();
    m_object = object;
    setPos(object.pos);
    setRotation(object.rotation);
    updateOrigin();
    update();
}

void VectorItem::setText(const QString &text)
{
    m_object.text = text;
    QFontMetricsF metrics(m_object.font);
    const qreal width = m_object.textBox.width() > 0 ? m_object.textBox.width() : 300.0;
    const QRectF needed = metrics.boundingRect(QRectF(0, 0, width, 10000),
                                               Qt::TextWordWrap | Qt::AlignLeft, text);
    prepareGeometryChange();
    m_object.textBox.setHeight(qMax(metrics.height(), needed.height() + 4.0));
    update();
}

void VectorItem::setFillColor(const QColor &color)
{
    m_object.fill = color;
    m_object.hasFill = color.alpha() > 0;
    update();
}

void VectorItem::setStrokeColor(const QColor &color)
{
    m_object.stroke = color;
    m_object.hasStroke = color.alpha() > 0;
    update();
}

void VectorItem::setStrokeWidth(qreal width)
{
    prepareGeometryChange();
    m_object.strokeWidth = qMax<qreal>(0.0, width);
    m_object.hasStroke = m_object.strokeWidth > 0.0;
    update();
}

void VectorItem::setObjectOpacity(qreal opacity)
{
    m_object.opacity = qBound(0.0, opacity, 1.0);
    update();
}

void VectorItem::setShadowEnabled(bool on)
{
    m_object.shadow = on;
    update();
}

void VectorItem::setRotationAngle(qreal degrees)
{
    setRotation(degrees);
    m_object.rotation = degrees;
}

void VectorItem::setPath(const QPainterPath &path)
{
    prepareGeometryChange();
    m_object.path = path;
    updateOrigin();
    update();
}

QVector<QPainterPath::Element> VectorItem::pathElements() const
{
    QVector<QPainterPath::Element> elements;
    elements.reserve(m_object.path.elementCount());
    for (int i = 0; i < m_object.path.elementCount(); ++i)
        elements.append(m_object.path.elementAt(i));
    return elements;
}

void VectorItem::setPathElements(const QVector<QPainterPath::Element> &elements)
{
    QPainterPath path;
    int i = 0;
    while (i < elements.size()) {
        const QPainterPath::Element e = elements.at(i);
        if (e.type == QPainterPath::MoveToElement) {
            path.moveTo(e.x, e.y);
            ++i;
        } else if (e.type == QPainterPath::LineToElement) {
            path.lineTo(e.x, e.y);
            ++i;
        } else if (e.type == QPainterPath::CurveToElement && i + 2 < elements.size()) {
            const auto c1 = elements.at(i);
            const auto c2 = elements.at(i + 1);
            const auto end = elements.at(i + 2);
            path.cubicTo(c1.x, c1.y, c2.x, c2.y, end.x, end.y);
            i += 3;
        } else {
            ++i;
        }
    }

    prepareGeometryChange();
    m_object.path = path;
    updateOrigin();
    update();
}

void VectorItem::updateOrigin()
{
    const QRectF bounds = m_object.localBounds();
    setTransformOriginPoint(bounds.center());
    m_object.rotation = rotation();
}

void VectorItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsObject::mousePressEvent(event);
        return;
    }

    m_handle = isSelected() ? handleAt(event->pos()) : -1;

    if (m_handle == 8) {
        m_interaction = Interaction::Rotate;
        m_rotateCenter = mapToScene(m_object.localBounds().center());
        m_startRotation = rotation();
        m_startAngle = std::atan2(event->scenePos().y() - m_rotateCenter.y(),
                                  event->scenePos().x() - m_rotateCenter.x());
        emit editStarted();
        event->accept();
        return;
    }

    if (m_handle >= 0) {
        m_interaction = Interaction::Resize;
        m_startPath = m_object.path;
        m_startBounds = m_object.localBounds();
        m_startFontSize = m_object.font.pixelSize() > 0 ? m_object.font.pixelSize() : 24.0;
        emit editStarted();
        event->accept();
        return;
    }

    m_interaction = Interaction::Move;
    m_pressScenePos = event->scenePos();
    emit editStarted();
    QGraphicsObject::mousePressEvent(event);
}

void VectorItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_interaction == Interaction::Resize) {
        applyResize(event->pos());
        event->accept();
        return;
    }

    if (m_interaction == Interaction::Rotate) {
        const qreal angle = std::atan2(event->scenePos().y() - m_rotateCenter.y(),
                                       event->scenePos().x() - m_rotateCenter.x());
        qreal degrees = m_startRotation + (angle - m_startAngle) * 180.0 / kPi;
        if (event->modifiers() & Qt::ShiftModifier)
            degrees = qRound(degrees / 15.0) * 15.0;
        setRotation(degrees);
        m_object.rotation = degrees;
        event->accept();
        return;
    }

    QGraphicsObject::mouseMoveEvent(event);
}

void VectorItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    const bool wasEditing = m_interaction != Interaction::None;
    m_interaction = Interaction::None;
    m_handle = -1;
    QGraphicsObject::mouseReleaseEvent(event);
    if (wasEditing)
        emit editFinished();
}

void VectorItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_object.type == ShapeType::Text) {
        emit textEditRequested(this);
        event->accept();
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(event);
}

void VectorItem::applyResize(const QPointF &pos)
{
    if (m_startBounds.width() < 0.5 || m_startBounds.height() < 0.5)
        return;

    qreal x1 = m_startBounds.left();
    qreal y1 = m_startBounds.top();
    qreal x2 = m_startBounds.right();
    qreal y2 = m_startBounds.bottom();

    switch (m_handle) {
    case 0: x1 = pos.x(); y1 = pos.y(); break;
    case 1: x2 = pos.x(); y1 = pos.y(); break;
    case 2: x2 = pos.x(); y2 = pos.y(); break;
    case 3: x1 = pos.x(); y2 = pos.y(); break;
    case 4: y1 = pos.y(); break;
    case 5: x2 = pos.x(); break;
    case 6: y2 = pos.y(); break;
    case 7: x1 = pos.x(); break;
    default: return;
    }

    QRectF target(QPointF(qMin(x1, x2), qMin(y1, y2)),
                  QPointF(qMax(x1, x2), qMax(y1, y2)));
    if (target.width() < 3.0 || target.height() < 3.0)
        return;

    prepareGeometryChange();

    if (m_object.type == ShapeType::Text) {
        const qreal factor = target.height() / m_startBounds.height();
        m_object.textBox = target;
        QFont font = m_object.font;
        font.setPixelSize(qMax(4, qRound(m_startFontSize * factor)));
        m_object.font = font;
    } else if (m_object.type == ShapeType::Image) {
        m_object.textBox = target;
    } else {
        QTransform transform;
        transform.translate(target.left(), target.top());
        transform.scale(target.width() / m_startBounds.width(),
                        target.height() / m_startBounds.height());
        transform.translate(-m_startBounds.left(), -m_startBounds.top());
        m_object.path = transform.map(m_startPath);
    }

    updateOrigin();
    update();
}
