#include "ToolPalette.h"

#include <QButtonGroup>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "Icons.h"
#include "Theme.h"

class FillStrokeIndicator : public QWidget
{
public:
    explicit FillStrokeIndicator(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(40, 42);
        setCursor(Qt::PointingHandCursor);
    }

    void setColors(const QColor &fill, const QColor &stroke)
    {
        m_fill = fill;
        m_stroke = stroke;
        update();
    }

    std::function<void()> onFillClicked;
    std::function<void()> onStrokeClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Outline swatch (back).
        p.setPen(QPen(QColor(0xd7, 0xdb, 0xe2), 1.4));
        p.setBrush(m_stroke);
        p.drawRect(QRectF(7.0, 5.0, 20.0, 20.0));

        // Fill swatch (front).
        p.setPen(QPen(QColor(0x10, 0x11, 0x13), 1.4));
        p.setBrush(m_fill);
        p.drawRect(QRectF(13.0, 13.0, 20.0, 20.0));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (QRectF(13.0, 13.0, 20.0, 20.0).contains(event->position())
                && onFillClicked) {
                onFillClicked();
            } else if (QRectF(7.0, 5.0, 20.0, 20.0).contains(event->position())
                       && onStrokeClicked) {
                onStrokeClicked();
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QColor m_fill = QColor("#4f7cc0");
    QColor m_stroke = QColor("#d7dbe2");
};

ToolPalette::ToolPalette(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(50);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 6, 5, 6);
    layout->setSpacing(2);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    ToolGroup previous = ToolGroup::Pick;
    bool first = true;

    for (const ToolInfo &info : toolInfos()) {
        if (!first && info.group != previous) {
            auto *line = new QFrame(this);
            line->setFrameShape(QFrame::HLine);
            line->setFixedHeight(1);
            line->setStyleSheet(QStringLiteral("background:#34383f; border:none;"));
            layout->addSpacing(3);
            layout->addWidget(line);
            layout->addSpacing(3);
        }
        previous = info.group;
        first = false;

        auto *button = new QToolButton(this);
        button->setIcon(Icons::icon(info.id));
        button->setIconSize(QSize(20, 20));
        button->setFixedSize(38, 32);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(QStringLiteral("%1 (%2)\n%3")
                               .arg(info.name, info.shortcut, info.hint));
        button->setProperty("toolId", int(info.tool));
        button->installEventFilter(this);

        m_group->addButton(button, int(info.tool));
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    layout->addStretch(1);

    m_indicator = new FillStrokeIndicator(this);
    m_indicator->onFillClicked = [this]() { emit fillColorClicked(); };
    m_indicator->onStrokeClicked = [this]() { emit strokeColorClicked(); };
    layout->addWidget(m_indicator, 0, Qt::AlignHCenter);

    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
        emit toolSelected(static_cast<Tool>(id));
    });

    if (auto *firstButton = m_group->button(int(Tool::Select)))
        firstButton->setChecked(true);
}

void ToolPalette::setCurrentTool(Tool tool)
{
    if (auto *button = m_group->button(int(tool)))
        button->setChecked(true);
}

bool ToolPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        const int id = watched->property("toolId").toInt();
        emit toolDoubleClicked(static_cast<Tool>(id));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ToolPalette::setFillColor(const QColor &color)
{
    m_fillColor = color;
    m_indicator->setColors(m_fillColor, m_strokeColor);
}

void ToolPalette::setStrokeColor(const QColor &color)
{
    m_strokeColor = color;
    m_indicator->setColors(m_fillColor, m_strokeColor);
}
