#include "Ruler.h"

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include "CanvasView.h"

namespace {
qreal pickStep(qreal pixelsPerUnit, const QString &unit)
{
    QVector<qreal> candidates;
    if (unit == QLatin1String("px"))
        candidates = {10, 20, 50, 100, 200, 500, 1000};
    else if (unit == QLatin1String("см"))
        candidates = {0.5, 1, 2, 5, 10, 20};
    else if (unit == QLatin1String("in"))
        candidates = {0.25, 0.5, 1, 2, 5, 10};
    else
        candidates = {1, 2, 5, 10, 20, 50, 100};

    for (qreal step : candidates) {
        if (step * pixelsPerUnit >= 44.0)
            return step;
    }
    return candidates.last() * 10.0;
}

QString formatTick(qreal value)
{
    if (qAbs(value) < 0.001)
        return QStringLiteral("0");
    if (qAbs(value - qRound(value)) < 0.001)
        return QString::number(qRound(value));
    return QString::number(value, 'f', 2);
}
} // namespace

Ruler::Ruler(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    if (orientation == Qt::Horizontal)
        setFixedHeight(22);
    else
        setFixedWidth(22);
}

void Ruler::setView(CanvasView *view)
{
    m_view = view;
    update();
}

void Ruler::setUnit(const QString &unit, qreal pixelsPerUnit)
{
    m_unit = unit;
    m_pixelsPerUnit = pixelsPerUnit > 0.0 ? pixelsPerUnit : 1.0;
    update();
}

qreal Ruler::sceneCoordinate() const
{
    if (!m_view)
        return 0.0;
    const QPoint viewportPos = m_view->viewport()->mapFromGlobal(QCursor::pos());
    const QPointF scene = m_view->mapToScene(viewportPos);
    return m_orientation == Qt::Vertical ? scene.x() : scene.y();
}

void Ruler::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_view) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_moved = false;
    m_pressGlobal = QCursor::pos();
    setCursor(m_orientation == Qt::Vertical ? Qt::SizeHorCursor
                                            : Qt::SizeVerCursor);
    m_view->beginGuideDrag(m_orientation);
    m_view->updateGuideDrag(m_orientation, sceneCoordinate());
    event->accept();
}

void Ruler::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && m_view) {
        if ((QCursor::pos() - m_pressGlobal).manhattanLength() > 3)
            m_moved = true;
        m_view->updateGuideDrag(m_orientation, sceneCoordinate());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Ruler::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && m_view && event->button() == Qt::LeftButton) {
        if (m_moved)
            m_view->endGuideDrag(m_orientation, sceneCoordinate());
        else
            m_view->cancelGuideDrag();
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x22, 0x26));

    p.setPen(QColor(0x2c, 0x2f, 0x35));
    if (m_orientation == Qt::Horizontal)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());

    if (!m_view)
        return;

    const qreal zoom = m_view->zoom();
    const qreal pxPerUnit = m_pixelsPerUnit * zoom;
    if (pxPerUnit <= 0.0)
        return;

    const QPoint origin = m_view->mapFromScene(QPointF(0.0, 0.0));
    const qreal major = pickStep(pxPerUnit, m_unit);
    const qreal minor = major / 5.0;

    QFont f = font();
    f.setPointSizeF(7.2);
    p.setFont(f);

    const QColor tickColor(0x78, 0x7f, 0x8b);
    const QColor labelColor(0x9b, 0xa2, 0xad);

    if (m_orientation == Qt::Horizontal) {
        const int first = int(std::floor((0.0 - origin.x()) / (minor * pxPerUnit))) - 1;
        for (int i = first;; ++i) {
            const qreal value = i * minor;
            const qreal x = origin.x() + value * pxPerUnit;
            if (x > width() + 2.0)
                break;
            if (x < -2.0)
                continue;
            const bool isMajor = (i % 5 == 0);
            const qreal len = isMajor ? 7.0 : 3.0;
            p.setPen(tickColor);
            p.drawLine(QPointF(x, height() - len), QPointF(x, height() - 1.0));
            if (isMajor) {
                p.setPen(labelColor);
                p.drawText(QRectF(x + 3.0, 0.0, 46.0, height() - 8.0),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           formatTick(value));
            }
        }
    } else {
        const int first = int(std::floor((0.0 - origin.y()) / (minor * pxPerUnit))) - 1;
        for (int i = first;; ++i) {
            const qreal value = i * minor;
            const qreal y = origin.y() + value * pxPerUnit;
            if (y > height() + 2.0)
                break;
            if (y < -2.0)
                continue;
            const bool isMajor = (i % 5 == 0);
            const qreal len = isMajor ? 7.0 : 3.0;
            p.setPen(tickColor);
            p.drawLine(QPointF(width() - len, y), QPointF(width() - 1.0, y));
            if (isMajor) {
                p.save();
                p.setPen(labelColor);
                p.translate(width() - 5.0, y - 3.0);
                p.rotate(-90.0);
                p.drawText(QPointF(0.0, 0.0), formatTick(value));
                p.restore();
            }
        }
    }
}
