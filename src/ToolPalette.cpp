#include "ToolPalette.h"

#include <QButtonGroup>
#include <QFrame>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "Theme.h"

namespace {

class FillStrokeIndicator : public QWidget
{
public:
    explicit FillStrokeIndicator(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(40, 40);
        setToolTip(QStringLiteral("Заливка и обводка"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Outline swatch (back).
        p.setPen(QPen(QColor(0xd7, 0xdb, 0xe2), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(7.0, 7.0, 20.0, 20.0));

        // Fill swatch (front).
        p.setPen(QPen(QColor(0x10, 0x11, 0x13), 1.4));
        p.setBrush(QColor(0x4f, 0x7c, 0xc0));
        p.drawRect(QRectF(14.0, 14.0, 19.0, 19.0));
    }
};

} // namespace

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

        m_group->addButton(button, int(info.tool));
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    layout->addStretch(1);

    auto *indicator = new FillStrokeIndicator(this);
    layout->addWidget(indicator, 0, Qt::AlignHCenter);

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
