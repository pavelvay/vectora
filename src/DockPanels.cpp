#include "DockPanels.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QAbstractItemView>

#include <algorithm>

#include "CanvasView.h"
#include "Icons.h"
#include "VectorItem.h"

namespace {

QLabel *fieldLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    return label;
}

QDoubleSpinBox *spinField(QWidget *parent, double min, double max, double value,
                          const QString &suffix, int decimals = 0, int width = 76)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setValue(value);
    spin->setSuffix(suffix);
    spin->setFixedWidth(width);
    spin->setKeyboardTracking(false);
    return spin;
}

QToolButton *iconButton(QWidget *parent, const QString &iconId, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(Icons::icon(iconId));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

void colorButtonIcon(QToolButton *button, const QColor &color)
{
    button->setIcon(Icons::icon(QStringLiteral("swatch"), color));
    button->setIconSize(QSize(18, 18));
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#34383f; border:none;"));
    return line;
}

QString vectorName(const VectorObject &object)
{
    switch (object.type) {
    case ShapeType::Rectangle: return QStringLiteral("Прямоугольник");
    case ShapeType::Ellipse: return QStringLiteral("Эллипс");
    case ShapeType::Polygon: return QStringLiteral("Многоугольник");
    case ShapeType::Star: return QStringLiteral("Звезда");
    case ShapeType::Spiral: return QStringLiteral("Спираль");
    case ShapeType::Line: return QStringLiteral("Линия");
    case ShapeType::Bezier: return QStringLiteral("Кривая Безье");
    case ShapeType::Freehand: return QStringLiteral("Свободная форма");
    case ShapeType::Artistic: return QStringLiteral("Мазок");
    case ShapeType::Image: return QStringLiteral("Изображение");
    case ShapeType::Text: {
        QString first = object.text.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (first.size() > 22)
            first = first.left(22) + QStringLiteral("…");
        return QStringLiteral("Текст: %1").arg(first);
    }
    }
    return QStringLiteral("Объект");
}

} // namespace

DockPanels::DockPanels(QMainWindow *host, CanvasView *view, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_view(view)
{
    buildObjectsDocker();
    buildPropertiesDocker();
    buildColorDocker();
    buildAlignDocker();
    buildEffectsDocker();
    buildOutlineDocker();
    buildTextDocker();

    connect(m_view, &CanvasView::selectionChanged, this, [this](int) { refresh(); });
}

QDockWidget *DockPanels::createDock(const QString &title, QWidget *content)
{
    auto *dock = new QDockWidget(title, m_host);
    dock->setObjectName(QStringLiteral("Dock_") + title);

    auto *scroll = new QScrollArea(m_host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    dock->setWidget(scroll);

    dock->setMinimumWidth(270);
    dock->setMaximumWidth(460);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_docks.append(dock);
    return dock;
}

QList<VectorItem *> DockPanels::selection() const
{
    return m_view->selectedVectorItems();
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

void DockPanels::buildObjectsDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *tools = new QWidget(content);
    auto *toolsLayout = new QHBoxLayout(tools);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(2);

    auto *up = new QToolButton(tools);
    up->setText(QStringLiteral("Выше"));
    up->setToolTip(QStringLiteral("Поднять объект на уровень выше"));
    up->setAutoRaise(true);
    auto *down = new QToolButton(tools);
    down->setText(QStringLiteral("Ниже"));
    down->setToolTip(QStringLiteral("Опустить объект на уровень ниже"));
    down->setAutoRaise(true);

    auto *duplicate = iconButton(tools, QStringLiteral("copy"),
                                 QStringLiteral("Дублировать объект"));
    auto *remove = iconButton(tools, QStringLiteral("delete"),
                              QStringLiteral("Удалить объект"));

    toolsLayout->addWidget(up);
    toolsLayout->addWidget(down);
    toolsLayout->addWidget(duplicate);
    toolsLayout->addWidget(remove);
    toolsLayout->addStretch(1);

    m_tree = new QTreeWidget(content);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(tools);
    layout->addWidget(m_tree, 1);

    connect(up, &QToolButton::clicked, this, [this]() {
        if (m_view->selectedVectorItems().isEmpty()) {
            emit status(QStringLiteral("Выберите объект, чтобы изменить порядок."));
            return;
        }
        m_view->raiseSelection();
        refresh();
    });
    connect(down, &QToolButton::clicked, this, [this]() {
        if (m_view->selectedVectorItems().isEmpty()) {
            emit status(QStringLiteral("Выберите объект, чтобы изменить порядок."));
            return;
        }
        m_view->lowerSelection();
        refresh();
    });
    connect(duplicate, &QToolButton::clicked, this, [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty()) {
            emit status(QStringLiteral("Нет объектов для дублирования."));
            return;
        }
        emit beginEdit();
        m_view->scene()->clearSelection();
        for (VectorItem *item : selected) {
            VectorObject copy = item->object();
            copy.pos += QPointF(16.0, 16.0);
            VectorItem *added = m_view->addVectorObject(copy, false, false);
            added->setSelected(true);
        }
        emit endEdit();
        refresh();
        emit status(QStringLiteral("Скопировано объектов: %1").arg(selected.size()));
    });
    connect(remove, &QToolButton::clicked, this, [this]() {
        m_view->deleteSelectedObjects();
        refresh();
    });

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_updating)
            return;
        const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
        if (selected.isEmpty())
            return;
        const int index = selected.first()->data(0, Qt::UserRole).toInt();
        const QList<VectorItem *> items = m_view->vectorItems();
        if (index < 0 || index >= items.size())
            return;
        m_updating = true;
        m_view->scene()->clearSelection();
        items.at(index)->setSelected(true);
        m_updating = false;
        refreshProperties();
        refreshEffects();
        refreshText();
    });

    createDock(QStringLiteral("Объекты"), content);
}

void DockPanels::refreshTree()
{
    if (!m_tree)
        return;

    const QList<VectorItem *> items = m_view->vectorItems();
    const int scrollPosition = m_tree->verticalScrollBar()->value();
    m_updating = true;
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    auto *layer = new QTreeWidgetItem(m_tree, {QStringLiteral("Слой 1")});
    QFont layerFont = m_tree->font();
    layerFont.setBold(true);
    layer->setFont(0, layerFont);
    layer->setExpanded(true);

    for (int i = items.size() - 1; i >= 0; --i) {
        auto *entry = new QTreeWidgetItem(layer, {vectorName(items.at(i)->object())});
        entry->setData(0, Qt::UserRole, i);
        if (items.at(i)->isSelected())
            entry->setSelected(true);
    }
    m_tree->verticalScrollBar()->setValue(scrollPosition);
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

void DockPanels::buildPropertiesDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    m_selectionLabel = new QLabel(QStringLiteral("Ничего не выбрано"), content);
    m_selectionLabel->setStyleSheet(QStringLiteral("color:#9aa1ac; font-size:12px;"));
    layout->addWidget(m_selectionLabel);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(5);

    m_x = spinField(content, -10000, 10000, 0, QStringLiteral(" px"));
    m_y = spinField(content, -10000, 10000, 0, QStringLiteral(" px"));
    m_w = spinField(content, 1, 10000, 100, QStringLiteral(" px"));
    m_h = spinField(content, 1, 10000, 100, QStringLiteral(" px"));
    m_angle = spinField(content, -360, 360, 0, QStringLiteral(" °"), 1);

    form->addRow(fieldLabel(QStringLiteral("X")), m_x);
    form->addRow(fieldLabel(QStringLiteral("Y")), m_y);
    form->addRow(fieldLabel(QStringLiteral("Ширина")), m_w);
    form->addRow(fieldLabel(QStringLiteral("Высота")), m_h);
    form->addRow(fieldLabel(QStringLiteral("Угол")), m_angle);
    layout->addLayout(form);

    layout->addWidget(separator(content));

    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(fieldLabel(QStringLiteral("Прозрачность")));
    m_opacitySlider = new QSlider(Qt::Horizontal, content);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);
    opacityRow->addWidget(m_opacitySlider, 1);
    m_opacityValue = new QLabel(QStringLiteral("100 %"), content);
    m_opacityValue->setFixedWidth(42);
    m_opacityValue->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    opacityRow->addWidget(m_opacityValue);
    layout->addLayout(opacityRow);

    m_fillButton = new QToolButton(content);
    m_fillButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fillButton->setText(QStringLiteral(" Заливка"));
    m_fillButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_strokeButton = new QToolButton(content);
    m_strokeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_strokeButton->setText(QStringLiteral(" Обводка"));
    m_strokeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *widthRow = new QHBoxLayout;
    widthRow->addWidget(fieldLabel(QStringLiteral("Толщина")));
    m_strokeWidth = spinField(content, 0, 100, 2, QStringLiteral(" px"), 1);
    widthRow->addWidget(m_strokeWidth);
    widthRow->addStretch(1);
    layout->addWidget(m_fillButton);
    layout->addWidget(m_strokeButton);
    layout->addLayout(widthRow);

    auto *apply = new QToolButton(content);
    apply->setText(QStringLiteral("Применить"));
    apply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(apply);
    layout->addStretch(1);

    connect(m_x, &QDoubleSpinBox::editingFinished, this, &DockPanels::applyTransform);
    connect(m_y, &QDoubleSpinBox::editingFinished, this, &DockPanels::applyTransform);
    connect(m_w, &QDoubleSpinBox::editingFinished, this, &DockPanels::applyTransform);
    connect(m_h, &QDoubleSpinBox::editingFinished, this, &DockPanels::applyTransform);
    connect(m_angle, &QDoubleSpinBox::editingFinished, this,
            &DockPanels::applyTransform);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1 %").arg(value));
    });
    connect(m_opacitySlider, &QSlider::sliderReleased, this,
            &DockPanels::applyTransform);
    connect(apply, &QToolButton::clicked, this, &DockPanels::applyTransform);

    connect(m_fillButton, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(m_view->fillColor(), m_host,
                                                    QStringLiteral("Цвет заливки"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_view->setFillColor(color);
        updateColorButtons();
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty()) {
            emit status(QStringLiteral("Цвет заливки: %1").arg(color.name().toUpper()));
            return;
        }
        emit beginEdit();
        for (VectorItem *item : selected)
            item->setFillColor(color);
        emit endEdit();
        emit status(QStringLiteral("Заливка применена."));
    });

    connect(m_strokeButton, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(m_view->strokeColor(), m_host,
                                                    QStringLiteral("Цвет обводки"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_view->setStrokeColor(color);
        updateColorButtons();
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty())
            return;
        emit beginEdit();
        for (VectorItem *item : selected)
            item->setStrokeColor(color);
        emit endEdit();
    });

    connect(m_strokeWidth, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (m_updating)
            return;
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty())
            return;
        emit beginEdit();
        for (VectorItem *item : selected)
            item->setStrokeWidth(m_strokeWidth->value());
        emit endEdit();
    });

    m_propertiesContent = content;
    m_propertiesDock = createDock(QStringLiteral("Свойства"), content);
}

void DockPanels::refreshProperties()
{
    const QList<VectorItem *> items = selection();
    m_updating = true;

    if (items.isEmpty()) {
        m_selectionLabel->setText(QStringLiteral("Ничего не выбрано"));
        m_propertiesContent->setEnabled(false);
    } else {
        m_selectionLabel->setText(items.size() == 1
                                      ? QStringLiteral("Выбран 1 объект")
                                      : QStringLiteral("Выбрано объектов: %1")
                                            .arg(items.size()));
        m_propertiesContent->setEnabled(true);
        const VectorObject object = items.first()->object();
        const QRectF bounds = object.localBounds();
        m_x->setValue(object.pos.x());
        m_y->setValue(object.pos.y());
        m_w->setValue(bounds.width());
        m_h->setValue(bounds.height());
        m_angle->setValue(object.rotation);
        m_opacitySlider->setValue(qRound(object.opacity * 100.0));
        m_opacityValue->setText(QStringLiteral("%1 %").arg(m_opacitySlider->value()));
        m_strokeWidth->setValue(object.strokeWidth);
        m_outlineColor = object.hasStroke ? object.stroke : m_outlineColor;
        colorButtonIcon(m_outlineColorButton, m_outlineColor);
        m_outlineWidth->setValue(object.strokeWidth);
    }

    m_updating = false;
    updateColorButtons();
}

void DockPanels::applyTransform()
{
    if (m_updating)
        return;

    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty()) {
        emit status(QStringLiteral("Сначала выберите объект."));
        return;
    }

    const QPointF position(m_x->value(), m_y->value());
    const QSizeF size(m_w->value(), m_h->value());
    const qreal angle = m_angle->value();
    const qreal opacity = m_opacitySlider->value() / 100.0;

    emit beginEdit();
    if (selected.size() == 1) {
        VectorItem *item = selected.first();
        item->setFrameSize(size);
        item->setRotationAngle(angle);
        item->setPos(position);
        item->setObjectOpacity(opacity);
    } else {
        const QPointF delta = position - selected.first()->pos();
        for (VectorItem *item : selected) {
            item->setPos(item->pos() + delta);
            item->setObjectOpacity(opacity);
        }
    }
    emit endEdit();
    emit status(selected.size() == 1
                    ? QStringLiteral("Свойства объекта применены.")
                    : QStringLiteral("Для группы применены позиция и прозрачность."));
}

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

void DockPanels::buildColorDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(7);

    m_colorPreview = new QLabel(content);
    m_colorPreview->setFixedHeight(46);
    m_colorPreview->setStyleSheet(
        QStringLiteral("background:#4f7cc0; border:1px solid #3a3f47; border-radius:6px;"));
    layout->addWidget(m_colorPreview);

    m_hue = new QSlider(Qt::Horizontal, content);
    m_hue->setRange(0, 359);
    m_hue->setValue(215);
    m_saturation = new QSlider(Qt::Horizontal, content);
    m_saturation->setRange(0, 255);
    m_saturation->setValue(150);
    m_value = new QSlider(Qt::Horizontal, content);
    m_value->setRange(0, 255);
    m_value->setValue(190);
    m_alpha = new QSlider(Qt::Horizontal, content);
    m_alpha->setRange(0, 255);
    m_alpha->setValue(255);

    const auto addSlider = [&](const QString &name, QSlider *slider) {
        auto *row = new QHBoxLayout;
        auto *label = fieldLabel(name, content);
        label->setFixedWidth(58);
        row->addWidget(label);
        row->addWidget(slider, 1);
        layout->addLayout(row);
    };
    addSlider(QStringLiteral("Оттенок"), m_hue);
    addSlider(QStringLiteral("Насыщ."), m_saturation);
    addSlider(QStringLiteral("Яркость"), m_value);
    addSlider(QStringLiteral("Альфа"), m_alpha);

    auto *hexRow = new QHBoxLayout;
    hexRow->addWidget(fieldLabel(QStringLiteral("HTML"), content));
    m_hex = new QLineEdit(content);
    m_hex->setMaxLength(9);
    m_hex->setText(QStringLiteral("#4F7CC0"));
    hexRow->addWidget(m_hex, 1);
    layout->addLayout(hexRow);

    layout->addWidget(separator(content));

    auto *fill = new QToolButton(content);
    fill->setText(QStringLiteral("Залить выделение"));
    fill->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *stroke = new QToolButton(content);
    stroke->setText(QStringLiteral("Обвести выделение"));
    stroke->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(fill);
    layout->addWidget(stroke);
    layout->addStretch(1);

    const auto refreshColor = [this]() {
        m_color = QColor::fromHsv(m_hue->value(), m_saturation->value(),
                                  m_value->value(), m_alpha->value());
        updateColorPreview();
    };
    connect(m_hue, &QSlider::valueChanged, this, refreshColor);
    connect(m_saturation, &QSlider::valueChanged, this, refreshColor);
    connect(m_value, &QSlider::valueChanged, this, refreshColor);
    connect(m_alpha, &QSlider::valueChanged, this, refreshColor);

    connect(m_hex, &QLineEdit::editingFinished, this, [this]() {
        QColor color(m_hex->text().trimmed());
        if (!color.isValid())
            return;
        m_color = color;
        const QSignalBlocker hueBlocker(m_hue);
        const QSignalBlocker saturationBlocker(m_saturation);
        const QSignalBlocker valueBlocker(m_value);
        const QSignalBlocker alphaBlocker(m_alpha);
        m_hue->setValue(color.hsvHue() < 0 ? 0 : color.hsvHue());
        m_saturation->setValue(color.hsvSaturation());
        m_value->setValue(color.value());
        m_alpha->setValue(color.alpha());
        updateColorPreview();
    });

    connect(fill, &QToolButton::clicked, this, [this]() {
        applyColor(m_color, true);
    });
    connect(stroke, &QToolButton::clicked, this, [this]() {
        applyColor(m_color, false);
    });

    m_color = QColor::fromHsv(m_hue->value(), m_saturation->value(),
                              m_value->value(), m_alpha->value());
    updateColorPreview();
    createDock(QStringLiteral("Цвет"), content);
}

void DockPanels::updateColorPreview()
{
    m_colorPreview->setStyleSheet(
        QStringLiteral("background:rgba(%1,%2,%3,%4); border:1px solid #3a3f47; "
                       "border-radius:6px;")
            .arg(m_color.red())
            .arg(m_color.green())
            .arg(m_color.blue())
            .arg(m_color.alpha()));
    const QSignalBlocker blocker(m_hex);
    m_hex->setText(m_color.name(QColor::HexRgb).toUpper());
}

void DockPanels::applyColor(const QColor &color, bool forFill)
{
    if (forFill) {
        m_view->setFillColor(color);
    } else {
        m_view->setStrokeColor(color);
    }
    updateColorButtons();

    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty()) {
        emit status(QStringLiteral("Цвет %1 выбран по умолчанию.")
                        .arg(color.name().toUpper()));
        return;
    }

    emit beginEdit();
    for (VectorItem *item : selected) {
        if (forFill)
            item->setFillColor(color);
        else
            item->setStrokeColor(color);
    }
    emit endEdit();

    emit status(QStringLiteral("%1 применён к объектам: %2")
                    .arg(forFill ? QStringLiteral("Цвет заливки")
                                 : QStringLiteral("Цвет обводки"))
                    .arg(selected.size()));
}

// ---------------------------------------------------------------------------
// Align
// ---------------------------------------------------------------------------

void DockPanels::buildAlignDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *targetRow = new QHBoxLayout;
    targetRow->addWidget(fieldLabel(QStringLiteral("Относительно"), content));
    m_alignTarget = new QComboBox(content);
    m_alignTarget->addItems({QStringLiteral("Выделения"),
                             QStringLiteral("Страницы")});
    targetRow->addWidget(m_alignTarget, 1);
    layout->addLayout(targetRow);

    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    const struct {
        const char *icon;
        const char *tip;
        int mode;
    } alignButtons[] = {
        {"align-left", "По левому краю", 0},
        {"align-hcenter", "По центру по горизонтали", 1},
        {"align-right", "По правому краю", 2},
        {"align-top", "По верхнему краю", 3},
        {"align-vcenter", "По центру по вертикали", 4},
        {"align-bottom", "По нижнему краю", 5},
    };
    for (int i = 0; i < 6; ++i) {
        auto *button = new QToolButton(content);
        button->setIcon(Icons::icon(QString::fromLatin1(alignButtons[i].icon)));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(QString::fromUtf8(alignButtons[i].tip));
        button->setFixedSize(38, 32);
        const int mode = alignButtons[i].mode;
        connect(button, &QToolButton::clicked, this, [this, mode]() {
            applyAlign(mode);
        });
        grid->addWidget(button, i / 3, i % 3);
    }
    layout->addLayout(grid);

    layout->addWidget(separator(content));

    auto *distributeRow = new QHBoxLayout;
    auto *horizontal = new QToolButton(content);
    horizontal->setText(QStringLiteral("Распределить ↔"));
    auto *vertical = new QToolButton(content);
    vertical->setText(QStringLiteral("Распределить ↕"));
    distributeRow->addWidget(horizontal);
    distributeRow->addWidget(vertical);
    layout->addLayout(distributeRow);

    auto *hint = new QLabel(QStringLiteral(
        "Для распределения нужно минимум три объекта."),
        content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    layout->addWidget(hint);
    layout->addStretch(1);

    connect(horizontal, &QToolButton::clicked, this, [this]() { distribute(true); });
    connect(vertical, &QToolButton::clicked, this, [this]() { distribute(false); });

    createDock(QStringLiteral("Выравнивание"), content);
}

void DockPanels::applyAlign(int mode)
{
    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty()) {
        emit status(QStringLiteral("Сначала выберите объекты."));
        return;
    }

    QRectF target;
    const bool toPage = m_alignTarget->currentIndex() == 1;
    if (toPage) {
        target = QRectF(QPointF(0, 0), m_view->pageSize());
    } else {
        if (selected.size() < 2) {
            emit status(QStringLiteral("Для выравнивания относительно выделения "
                                       "нужно два объекта или выберите страницу."));
            return;
        }
        for (VectorItem *item : selected) {
            target = target.isNull() ? item->sceneBoundingRect()
                                     : target.united(item->sceneBoundingRect());
        }
    }

    emit beginEdit();
    for (VectorItem *item : selected) {
        const QRectF bounds = item->sceneBoundingRect();
        qreal dx = 0.0;
        qreal dy = 0.0;
        switch (mode) {
        case 0: dx = target.left() - bounds.left(); break;
        case 1: dx = target.center().x() - bounds.center().x(); break;
        case 2: dx = target.right() - bounds.right(); break;
        case 3: dy = target.top() - bounds.top(); break;
        case 4: dy = target.center().y() - bounds.center().y(); break;
        case 5: dy = target.bottom() - bounds.bottom(); break;
        default: break;
        }
        if (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy))
            item->moveBy(dx, dy);
    }
    emit endEdit();
    emit status(QStringLiteral("Выравнивание выполнено."));
}

void DockPanels::distribute(bool horizontal)
{
    QList<VectorItem *> selected = selection();
    if (selected.size() < 3) {
        emit status(QStringLiteral("Для распределения нужно минимум три объекта."));
        return;
    }

    std::sort(selected.begin(), selected.end(), [horizontal](VectorItem *a,
                                                             VectorItem *b) {
        const QPointF ca = a->sceneBoundingRect().center();
        const QPointF cb = b->sceneBoundingRect().center();
        return horizontal ? ca.x() < cb.x() : ca.y() < cb.y();
    });

    const QRectF first = selected.first()->sceneBoundingRect();
    const QRectF last = selected.last()->sceneBoundingRect();
    const qreal start = horizontal ? first.center().x() : first.center().y();
    const qreal end = horizontal ? last.center().x() : last.center().y();
    const qreal step = (end - start) / qreal(selected.size() - 1);

    emit beginEdit();
    for (int i = 1; i < selected.size() - 1; ++i) {
        const QPointF center = selected.at(i)->sceneBoundingRect().center();
        const qreal target = start + step * i;
        if (horizontal)
            selected.at(i)->moveBy(target - center.x(), 0.0);
        else
            selected.at(i)->moveBy(0.0, target - center.y());
    }
    emit endEdit();
    emit status(QStringLiteral("Объекты распределены."));
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

void DockPanels::buildEffectsDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *tabs = new QTabWidget(content);

    // Shadow
    auto *shadowTab = new QWidget(tabs);
    auto *shadowLayout = new QVBoxLayout(shadowTab);
    shadowLayout->setContentsMargins(8, 10, 8, 8);
    shadowLayout->setSpacing(6);

    m_shadowEnabled = new QCheckBox(QStringLiteral("Включить тень"), shadowTab);
    shadowLayout->addWidget(m_shadowEnabled);

    auto *shadowForm = new QFormLayout;
    shadowForm->setHorizontalSpacing(10);
    shadowForm->setVerticalSpacing(5);
    m_shadowX = spinField(shadowTab, -200, 200, 8, QStringLiteral(" px"));
    m_shadowY = spinField(shadowTab, -200, 200, 8, QStringLiteral(" px"));
    m_shadowOpacity = spinField(shadowTab, 0, 100, 28, QStringLiteral(" %"));

    m_shadowColorButton = new QToolButton(shadowTab);
    m_shadowColorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_shadowColorButton->setText(QStringLiteral(" Цвет тени"));
    colorButtonIcon(m_shadowColorButton, m_shadowColor);

    shadowForm->addRow(fieldLabel(QStringLiteral("Сдвиг X")), m_shadowX);
    shadowForm->addRow(fieldLabel(QStringLiteral("Сдвиг Y")), m_shadowY);
    shadowForm->addRow(fieldLabel(QStringLiteral("Непрозр.")), m_shadowOpacity);
    shadowLayout->addLayout(shadowForm);
    shadowLayout->addWidget(m_shadowColorButton);

    auto *shadowApply = new QToolButton(shadowTab);
    shadowApply->setText(QStringLiteral("Применить тень"));
    shadowApply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    shadowLayout->addWidget(shadowApply);
    shadowLayout->addStretch(1);

    connect(m_shadowColorButton, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(
            m_shadowColor, m_host, QStringLiteral("Цвет тени"),
            QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_shadowColor = color;
        colorButtonIcon(m_shadowColorButton, m_shadowColor);
    });
    connect(shadowApply, &QToolButton::clicked, this, &DockPanels::applyShadow);

    // Transparency
    auto *transparencyTab = new QWidget(tabs);
    auto *transparencyLayout = new QVBoxLayout(transparencyTab);
    transparencyLayout->setContentsMargins(8, 10, 8, 8);
    transparencyLayout->setSpacing(8);

    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(fieldLabel(QStringLiteral("Непрозрачность"),
                                     transparencyTab));
    m_effectOpacity = new QSlider(Qt::Horizontal, transparencyTab);
    m_effectOpacity->setRange(0, 100);
    m_effectOpacity->setValue(100);
    opacityRow->addWidget(m_effectOpacity, 1);
    m_effectOpacityValue = new QLabel(QStringLiteral("100 %"), transparencyTab);
    m_effectOpacityValue->setFixedWidth(42);
    m_effectOpacityValue->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    opacityRow->addWidget(m_effectOpacityValue);
    transparencyLayout->addLayout(opacityRow);

    auto *transparencyApply = new QToolButton(transparencyTab);
    transparencyApply->setText(QStringLiteral("Применить прозрачность"));
    transparencyApply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    transparencyLayout->addWidget(transparencyApply);
    transparencyLayout->addStretch(1);

    connect(m_effectOpacity, &QSlider::valueChanged, this, [this](int value) {
        m_effectOpacityValue->setText(QStringLiteral("%1 %").arg(value));
    });
    connect(transparencyApply, &QToolButton::clicked, this, [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty()) {
            emit status(QStringLiteral("Выберите объекты."));
            return;
        }
        emit beginEdit();
        for (VectorItem *item : selected)
            item->setObjectOpacity(m_effectOpacity->value() / 100.0);
        emit endEdit();
        refreshProperties();
        emit status(QStringLiteral("Прозрачность применена."));
    });

    // Blend
    auto *blendTab = new QWidget(tabs);
    auto *blendLayout = new QVBoxLayout(blendTab);
    blendLayout->setContentsMargins(8, 10, 8, 8);
    blendLayout->setSpacing(8);

    auto *stepsRow = new QHBoxLayout;
    stepsRow->addWidget(fieldLabel(QStringLiteral("Шагов"), blendTab));
    m_blendSteps = new QSpinBox(blendTab);
    m_blendSteps->setRange(2, 40);
    m_blendSteps->setValue(7);
    stepsRow->addWidget(m_blendSteps);
    stepsRow->addStretch(1);
    blendLayout->addLayout(stepsRow);

    auto *blendApply = new QToolButton(blendTab);
    blendApply->setText(QStringLiteral("Создать перетекание"));
    blendApply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    blendLayout->addWidget(blendApply);

    auto *blendHint = new QLabel(QStringLiteral(
        "Выделите два объекта. Промежуточные копии получат "
        "позицию, размер и цвет между крайними."),
        blendTab);
    blendHint->setWordWrap(true);
    blendHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    blendLayout->addWidget(blendHint);
    blendLayout->addStretch(1);

    connect(blendApply, &QToolButton::clicked, this, [this]() {
        m_view->blendSelected(m_blendSteps->value());
        refresh();
    });

    tabs->addTab(shadowTab, QStringLiteral("Тень"));
    tabs->addTab(transparencyTab, QStringLiteral("Прозрачность"));
    tabs->addTab(blendTab, QStringLiteral("Перетекание"));
    layout->addWidget(tabs);

    createDock(QStringLiteral("Эффекты"), content);
}

void DockPanels::applyShadow()
{
    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty()) {
        emit status(QStringLiteral("Сначала выберите объекты."));
        return;
    }

    emit beginEdit();
    for (VectorItem *item : selected) {
        item->setShadowEnabled(m_shadowEnabled->isChecked());
        item->setShadowOffset(QPointF(m_shadowX->value(), m_shadowY->value()));
        item->setShadowOpacity(m_shadowOpacity->value() / 100.0);
        item->setShadowColor(m_shadowColor);
    }
    emit endEdit();
    emit status(QStringLiteral("Тень применена к объектам: %1").arg(selected.size()));
}

void DockPanels::refreshEffects()
{
    const QList<VectorItem *> selected = selection();
    m_updating = true;

    if (selected.isEmpty()) {
        m_shadowEnabled->setChecked(false);
        m_shadowEnabled->setEnabled(false);
        m_shadowX->setEnabled(false);
        m_shadowY->setEnabled(false);
        m_shadowOpacity->setEnabled(false);
        m_shadowColorButton->setEnabled(false);
        m_effectOpacity->setEnabled(false);
        m_blendSteps->setEnabled(false);
    } else {
        const VectorObject object = selected.first()->object();
        m_shadowEnabled->setEnabled(true);
        m_shadowX->setEnabled(true);
        m_shadowY->setEnabled(true);
        m_shadowOpacity->setEnabled(true);
        m_shadowColorButton->setEnabled(true);
        m_effectOpacity->setEnabled(true);
        m_blendSteps->setEnabled(true);

        m_shadowEnabled->setChecked(object.shadow);
        m_shadowX->setValue(object.shadowOffset.x());
        m_shadowY->setValue(object.shadowOffset.y());
        m_shadowOpacity->setValue(qRound(object.shadowOpacity * 100.0));
        m_shadowColor = object.shadowColor;
        colorButtonIcon(m_shadowColorButton, m_shadowColor);
        m_effectOpacity->setValue(qRound(object.opacity * 100.0));
        m_effectOpacityValue->setText(
            QStringLiteral("%1 %").arg(m_effectOpacity->value()));
    }

    m_updating = false;
}

// ---------------------------------------------------------------------------
// Outline
// ---------------------------------------------------------------------------

void DockPanels::buildOutlineDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    m_outlineColorButton = new QToolButton(content);
    m_outlineColorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_outlineColorButton->setText(QStringLiteral(" Цвет обводки"));
    m_outlineColorButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    colorButtonIcon(m_outlineColorButton, m_outlineColor);
    layout->addWidget(m_outlineColorButton);

    auto *widthRow = new QHBoxLayout;
    widthRow->addWidget(fieldLabel(QStringLiteral("Толщина"), content));
    m_outlineWidth = spinField(content, 0, 100, 2, QStringLiteral(" px"), 1);
    widthRow->addWidget(m_outlineWidth);
    widthRow->addStretch(1);
    layout->addLayout(widthRow);

    auto *apply = new QToolButton(content);
    apply->setText(QStringLiteral("Применить обводку"));
    apply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(apply);

    auto *remove = new QToolButton(content);
    remove->setText(QStringLiteral("Убрать обводку"));
    remove->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(remove);
    layout->addStretch(1);

    connect(m_outlineColorButton, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(
            m_outlineColor, m_host, QStringLiteral("Цвет обводки"),
            QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_outlineColor = color;
        colorButtonIcon(m_outlineColorButton, m_outlineColor);
    });

    connect(apply, &QToolButton::clicked, this, [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty()) {
            emit status(QStringLiteral("Сначала выберите объекты."));
            return;
        }
        emit beginEdit();
        for (VectorItem *item : selected) {
            item->setStrokeColor(m_outlineColor);
            item->setStrokeWidth(m_outlineWidth->value());
        }
        emit endEdit();
        refreshProperties();
        emit status(QStringLiteral("Обводка применена."));
    });

    connect(remove, &QToolButton::clicked, this, [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty())
            return;
        emit beginEdit();
        for (VectorItem *item : selected)
            item->setStrokeWidth(0.0);
        emit endEdit();
        refreshProperties();
        emit status(QStringLiteral("Обводка убрана."));
    });

    createDock(QStringLiteral("Обводка"), content);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void DockPanels::buildTextDocker()
{
    m_textContent = new QWidget;
    auto *layout = new QVBoxLayout(m_textContent);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(fieldLabel(QStringLiteral("Шрифт"), m_textContent));
    m_fontCombo = new QFontComboBox(m_textContent);
    m_fontCombo->setMaxVisibleItems(12);
    fontRow->addWidget(m_fontCombo, 1);
    layout->addLayout(fontRow);

    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(fieldLabel(QStringLiteral("Кегль"), m_textContent));
    m_fontSize = spinField(m_textContent, 4, 400, 24, QStringLiteral(" px"), 0);
    sizeRow->addWidget(m_fontSize);
    sizeRow->addStretch(1);
    layout->addLayout(sizeRow);

    auto *styleRow = new QHBoxLayout;
    QFont boldFont = m_host->font();
    boldFont.setBold(true);
    QFont italicFont = m_host->font();
    italicFont.setItalic(true);
    QFont underlineFont = m_host->font();
    underlineFont.setUnderline(true);

    m_boldButton = new QToolButton(m_textContent);
    m_boldButton->setText(QStringLiteral("Ж"));
    m_boldButton->setFont(boldFont);
    m_boldButton->setCheckable(true);
    m_boldButton->setToolTip(QStringLiteral("Полужирный"));
    m_boldButton->setFixedSize(28, 26);

    m_italicButton = new QToolButton(m_textContent);
    m_italicButton->setText(QStringLiteral("К"));
    m_italicButton->setFont(italicFont);
    m_italicButton->setCheckable(true);
    m_italicButton->setToolTip(QStringLiteral("Курсив"));
    m_italicButton->setFixedSize(28, 26);

    m_underlineButton = new QToolButton(m_textContent);
    m_underlineButton->setText(QStringLiteral("Ч"));
    m_underlineButton->setFont(underlineFont);
    m_underlineButton->setCheckable(true);
    m_underlineButton->setToolTip(QStringLiteral("Подчёркнутый"));
    m_underlineButton->setFixedSize(28, 26);

    styleRow->addWidget(m_boldButton);
    styleRow->addWidget(m_italicButton);
    styleRow->addWidget(m_underlineButton);
    styleRow->addStretch(1);
    layout->addLayout(styleRow);

    auto *alignRow = new QHBoxLayout;
    auto *alignGroup = new QButtonGroup(this);
    alignGroup->setExclusive(true);
    const char *alignIcons[] = {"align-left", "align-hcenter", "align-right"};
    const char *alignTips[] = {"По левому краю", "По центру", "По правому краю"};
    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(m_textContent);
        button->setIcon(Icons::icon(QString::fromLatin1(alignIcons[i])));
        button->setIconSize(QSize(18, 18));
        button->setCheckable(true);
        button->setToolTip(QString::fromUtf8(alignTips[i]));
        alignGroup->addButton(button, i);
        m_alignButtons[i] = button;
        alignRow->addWidget(button);
    }
    alignRow->addStretch(1);
    layout->addLayout(alignRow);
    m_alignButtons[0]->setChecked(true);
    layout->addStretch(1);

    connect(alignGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_updating)
            return;
        m_textAlign = id;
        applyText();
    });
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this]() {
        if (!m_updating)
            applyText();
    });
    connect(m_fontSize, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (!m_updating)
            applyText();
    });
    connect(m_boldButton, &QToolButton::clicked, this, [this]() {
        if (!m_updating)
            applyText();
    });
    connect(m_italicButton, &QToolButton::clicked, this, [this]() {
        if (!m_updating)
            applyText();
    });
    connect(m_underlineButton, &QToolButton::clicked, this, [this]() {
        if (!m_updating)
            applyText();
    });

    createDock(QStringLiteral("Текст"), m_textContent);
}

void DockPanels::refreshText()
{
    VectorItem *textItem = nullptr;
    for (VectorItem *item : selection()) {
        if (item->object().type == ShapeType::Text) {
            textItem = item;
            break;
        }
    }

    m_updating = true;
    m_textContent->setEnabled(textItem != nullptr);
    if (textItem) {
        const VectorObject object = textItem->object();
        m_fontCombo->setCurrentFont(QFont(object.font.family()));
        m_fontSize->setValue(object.font.pixelSize() > 0 ? object.font.pixelSize()
                                                         : 24);
        m_boldButton->setChecked(object.font.bold());
        m_italicButton->setChecked(object.font.italic());
        m_underlineButton->setChecked(object.font.underline());
        m_textAlign = qBound(0, object.textAlign, 2);
        m_alignButtons[m_textAlign]->setChecked(true);
    }
    m_updating = false;
}

void DockPanels::applyText()
{
    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty())
        return;

    QFont font = m_fontCombo->currentFont();
    font.setPixelSize(qMax(4, qRound(m_fontSize->value())));
    font.setBold(m_boldButton->isChecked());
    font.setItalic(m_italicButton->isChecked());
    font.setUnderline(m_underlineButton->isChecked());

    bool any = false;
    emit beginEdit();
    for (VectorItem *item : selected) {
        if (item->object().type != ShapeType::Text)
            continue;
        item->setTextFont(font);
        item->setTextAlignment(m_textAlign);
        any = true;
    }
    emit endEdit();

    if (any)
        emit status(QStringLiteral("Параметры текста обновлены."));
}

// ---------------------------------------------------------------------------
// Shared refresh
// ---------------------------------------------------------------------------

void DockPanels::updateColorButtons()
{
    if (m_fillButton)
        colorButtonIcon(m_fillButton, m_view->fillColor());
    if (m_strokeButton)
        colorButtonIcon(m_strokeButton, m_view->strokeColor());
}

void DockPanels::refresh()
{
    refreshTree();
    refreshProperties();
    refreshEffects();
    refreshText();
    updateColorButtons();

    // Keep the colour mixer in sync with the selected object.
    const QList<VectorItem *> selected = selection();
    if (!selected.isEmpty()) {
        const QColor color = selected.first()->object().fill;
        if (color.isValid()) {
            m_color = color;
            const QSignalBlocker hueBlocker(m_hue);
            const QSignalBlocker saturationBlocker(m_saturation);
            const QSignalBlocker valueBlocker(m_value);
            const QSignalBlocker alphaBlocker(m_alpha);
            m_hue->setValue(color.hsvHue() < 0 ? 0 : color.hsvHue());
            m_saturation->setValue(color.hsvSaturation());
            m_value->setValue(color.value());
            m_alpha->setValue(color.alpha());
            updateColorPreview();
        }
    }
}
