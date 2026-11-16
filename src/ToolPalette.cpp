#include "ToolPalette.h"

#include <QButtonGroup>
#include <QFrame>
#include <QMenu>
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
    bool shapeCreated = false;

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

        if (info.group == ToolGroup::Shape) {
            if (shapeCreated)
                continue;
            // Shape tools share one compact button. Left click uses the current
            // shape, the arrow or right click opens a small compact menu.
            m_shapeTool = Tool::Rectangle;
            m_shapeButton = new QToolButton(this);
            m_shapeButton->setIcon(Icons::icon(QStringLiteral("rect")));
            m_shapeButton->setIconSize(QSize(20, 20));
            m_shapeButton->setFixedSize(38, 32);
            m_shapeButton->setCheckable(true);
            m_shapeButton->setAutoRaise(true);
            m_shapeButton->setToolTip(QStringLiteral(
                "Фигуры (R, E, P, Shift+P, Shift+S, L)\n"
                "ЛКМ — текущая фигура, ПКМ или стрелка — выбор фигуры"));
            m_shapeButton->setProperty("toolId", int(Tool::Rectangle));

            m_shapeMenu = new QMenu(m_shapeButton);
            for (const ToolInfo &shape : toolInfos()) {
                if (shape.group != ToolGroup::Shape)
                    continue;
                QAction *action = m_shapeMenu->addAction(
                    Icons::icon(shape.id),
                    QStringLiteral("%1  ·  %2").arg(shape.name, shape.shortcut));
                action->setData(int(shape.tool));
                action->setToolTip(shape.hint);
                connect(action, &QAction::triggered, this, [this, tool = shape.tool]() {
                    setShapeTool(tool);
                });
            }
            m_shapeButton->setMenu(m_shapeMenu);
            m_shapeButton->setPopupMode(QToolButton::MenuButtonPopup);
            m_shapeButton->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(m_shapeButton, &QWidget::customContextMenuRequested, this,
                    [this](const QPoint &pos) {
                        if (m_shapeMenu)
                            m_shapeMenu->exec(m_shapeButton->mapToGlobal(pos));
                    });
            m_shapeButton->installEventFilter(this);

            m_group->addButton(m_shapeButton, int(Tool::Rectangle));
            layout->addWidget(m_shapeButton, 0, Qt::AlignHCenter);
            shapeCreated = true;
            continue;
        }

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
    const ToolInfo *info = toolInfo(tool);
    if (info && info->group == ToolGroup::Shape && m_shapeButton) {
        m_shapeTool = tool;
        m_shapeButton->setProperty("toolId", int(tool));
        m_shapeButton->setIcon(Icons::icon(info->id));
        m_shapeButton->setToolTip(QStringLiteral(
            "Фигуры — текущая: %1 (%2)\nЛКМ — рисовать, ПКМ — выбрать фигуру")
                                      .arg(info->name, info->shortcut));
        m_group->setId(m_shapeButton, int(tool));
        m_shapeButton->setChecked(true);
        return;
    }
    if (auto *button = m_group->button(int(tool)))
        button->setChecked(true);
}

void ToolPalette::setShapeTool(Tool tool)
{
    if (!m_shapeButton)
        return;
    const ToolInfo *info = toolInfo(tool);
    if (!info)
        return;
    m_shapeTool = tool;
    m_shapeButton->setProperty("toolId", int(tool));
    m_shapeButton->setIcon(Icons::icon(info->id));
    m_shapeButton->setToolTip(QStringLiteral(
        "Фигуры — текущая: %1 (%2)\nЛКМ — рисовать, ПКМ — выбрать фигуру")
                                  .arg(info->name, info->shortcut));
    m_group->setId(m_shapeButton, int(tool));
    m_shapeButton->setChecked(true);
    emit toolSelected(tool);
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
