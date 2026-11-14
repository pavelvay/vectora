#include "ColorPalette.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

QVector<PaletteEntry> ColorPalette::defaultEntries()
{
    return {
        {QColor("#000000"), QStringLiteral("Чёрный")},
        {QColor("#1a1a1a"), QStringLiteral("90% чёрного")},
        {QColor("#333333"), QStringLiteral("80% чёрного")},
        {QColor("#4d4d4d"), QStringLiteral("70% чёрного")},
        {QColor("#666666"), QStringLiteral("60% чёрного")},
        {QColor("#808080"), QStringLiteral("50% чёрного")},
        {QColor("#999999"), QStringLiteral("40% чёрного")},
        {QColor("#b3b3b3"), QStringLiteral("30% чёрного")},
        {QColor("#cccccc"), QStringLiteral("20% чёрного")},
        {QColor("#e6e6e6"), QStringLiteral("10% чёрного")},
        {QColor("#ffffff"), QStringLiteral("Белый")},

        {QColor("#7f0000"), QStringLiteral("Тёмно-красный")},
        {QColor("#c00000"), QStringLiteral("Красный")},
        {QColor("#ff0000"), QStringLiteral("Чистый красный")},
        {QColor("#ff4d4d"), QStringLiteral("Светло-красный")},
        {QColor("#ff9999"), QStringLiteral("Розовый")},
        {QColor("#7f3f00"), QStringLiteral("Коричневый")},
        {QColor("#ff7f00"), QStringLiteral("Оранжевый")},
        {QColor("#ffb300"), QStringLiteral("Янтарный")},
        {QColor("#ffe066"), QStringLiteral("Песочный")},
        {QColor("#ffff00"), QStringLiteral("Жёлтый")},
        {QColor("#b3b300"), QStringLiteral("Оливковый")},

        {QColor("#004d00"), QStringLiteral("Тёмно-зелёный")},
        {QColor("#008000"), QStringLiteral("Зелёный")},
        {QColor("#4caf50"), QStringLiteral("Травяной")},
        {QColor("#80e080"), QStringLiteral("Салатовый")},
        {QColor("#00b3b3"), QStringLiteral("Бирюзовый")},
        {QColor("#008080"), QStringLiteral("Тиловый")},
        {QColor("#003366"), QStringLiteral("Тёмно-синий")},
        {QColor("#004c99"), QStringLiteral("Синий")},
        {QColor("#1a75ff"), QStringLiteral("Ярко-синий")},
        {QColor("#4f7cc0"), QStringLiteral("Акцент")},
        {QColor("#99ccff"), QStringLiteral("Голубой")},

        {QColor("#330066"), QStringLiteral("Индиго")},
        {QColor("#6600cc"), QStringLiteral("Фиолетовый")},
        {QColor("#9933ff"), QStringLiteral("Сиреневый")},
        {QColor("#cc99ff"), QStringLiteral("Лавандовый")},
        {QColor("#7f004d"), QStringLiteral("Сливовый")},
        {QColor("#e6007e"), QStringLiteral("Маджента")},
        {QColor("#ff66b3"), QStringLiteral("Фуксия")},
        {QColor("#5c4033"), QStringLiteral("Какао")},
        {QColor("#c2a37a"), QStringLiteral("Бежевый")},
        {QColor("#f2e6c9"), QStringLiteral("Кремовый")},
    };
}

ColorPalette::ColorPalette(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(34);
    setMinimumWidth(320);

    setEntries(defaultEntries());
}

void ColorPalette::setEntries(const QVector<PaletteEntry> &entries)
{
    m_entries = entries;
    m_hovered = -1;
    updateGeometry();
    update();
}

void ColorPalette::addEntry(const PaletteEntry &entry)
{
    m_entries.append(entry);
    updateGeometry();
    update();
}

QSize ColorPalette::sizeHint() const
{
    return QSize(m_entries.size() * 20, 34);
}

QRectF ColorPalette::swatchRect(int index) const
{
    const qreal spacing = 1.0;
    const qreal available = width() - 8.0;
    const qreal swatchWidth = qMin<qreal>(24.0, (available - spacing * m_entries.size())
                                                       / qMax(1, m_entries.size()));
    const qreal x = 4.0 + index * (swatchWidth + spacing);
    return QRectF(x, 5.0, swatchWidth, height() - 10.0);
}

int ColorPalette::indexAt(const QPoint &pos) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (swatchRect(i).adjusted(-1.0, 0.0, 1.0, 0.0).contains(pos))
            return i;
    }
    return -1;
}

void ColorPalette::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1b, 0x1d, 0x21));
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < m_entries.size(); ++i) {
        const QRectF r = swatchRect(i);
        p.setPen(Qt::NoPen);
        p.setBrush(m_entries.at(i).color);
        p.drawRoundedRect(r, 2.0, 2.0);

        if (m_entries.at(i).spot) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xff, 0xb3, 0x47));
            p.drawEllipse(r.topLeft() + QPointF(3.0, 3.0), 2.2, 2.2);
        } else if (m_entries.at(i).global) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x7f, 0xae, 0xf7));
            p.drawEllipse(r.topRight() + QPointF(-3.0, 3.0), 2.0, 2.0);
        }

        if (i == m_hovered) {
            p.setPen(QPen(QColor(0xf2, 0xf5, 0xfa), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-1.0, -1.0, 1.0, 1.0), 2.5, 2.5);
        }
    }
}

void ColorPalette::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->pos());
    if (index != m_hovered) {
        m_hovered = index;
        update();
        if (index >= 0) {
            const PaletteEntry &entry = m_entries.at(index);
            QString suffix;
            if (entry.spot)
                suffix = QStringLiteral("  [Spot]");
            else if (entry.global)
                suffix = QStringLiteral("  [Глобальный]");
            QToolTip::showText(event->globalPosition().toPoint(),
                               QStringLiteral("%1 — %2%3")
                                   .arg(entry.name, entry.color.name().toUpper(), suffix),
                               this);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ColorPalette::mousePressEvent(QMouseEvent *event)
{
    const int index = indexAt(event->pos());
    if (index >= 0 && event->button() == Qt::LeftButton) {
        const PaletteEntry &entry = m_entries.at(index);
        emit colorPicked(entry.color, entry.name);
        emit statusHint(QStringLiteral("Заливка: %1 (%2)")
                            .arg(entry.name, entry.color.name().toUpper()));
    } else if (index >= 0 && event->button() == Qt::RightButton) {
        const PaletteEntry &entry = m_entries.at(index);
        emit outlineColorPicked(entry.color, entry.name);
        emit statusHint(QStringLiteral("Обводка: %1 (%2)")
                            .arg(entry.name, entry.color.name().toUpper()));
    }
    QWidget::mousePressEvent(event);
}

void ColorPalette::leaveEvent(QEvent *event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}
