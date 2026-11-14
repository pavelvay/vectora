#include "DockPanels.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QDropEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QAbstractItemView>
#include <QMessageBox>

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

constexpr int kLayerRole = Qt::UserRole + 1;
constexpr int kItemRole = Qt::UserRole + 2;
constexpr int kGroupRole = Qt::UserRole + 3;

class ObjectsTree : public QTreeWidget
{
public:
    explicit ObjectsTree(QWidget *parent = nullptr)
        : QTreeWidget(parent)
    {
    }

    std::function<void(VectorItem *, int, int)> dropObject;
    std::function<void(int, int)> dropLayer;
    std::function<void(int)> toggleVisible;
    std::function<void(int)> toggleLocked;
    std::function<void(VectorItem *)> toggleObjectLock;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QTreeWidgetItem *item = itemAt(event->pos());
        const int column = columnAt(int(event->position().x()));
        if (item && !item->data(0, kItemRole).isValid()) {
            const int layer = item->data(0, kLayerRole).toInt();
            if (column == 1 && toggleVisible) {
                toggleVisible(layer);
                return;
            }
            if (column == 2 && toggleLocked) {
                toggleLocked(layer);
                return;
            }
        }
        if (item && item->data(0, kItemRole).isValid() && column == 2
            && toggleObjectLock) {
            auto *vectorItem = reinterpret_cast<VectorItem *>(
                item->data(0, kItemRole).value<quintptr>());
            toggleObjectLock(vectorItem);
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        QTreeWidgetItem *source = selectedItems().value(0);
        if (!source) {
            event->ignore();
            return;
        }

        QTreeWidgetItem *target = itemAt(event->position().toPoint());
        const bool sourceIsLayer = !source->data(0, kItemRole).isValid();

        if (sourceIsLayer) {
            const int from = source->data(0, kLayerRole).toInt();
            int to = from;
            if (target) {
                QTreeWidgetItem *layerItem =
                    target->data(0, kItemRole).isValid() ? target->parent() : target;
                if (layerItem)
                    to = layerItem->data(0, kLayerRole).toInt();
            }
            if (dropLayer && to != from)
                dropLayer(from, to);
        } else {
            auto *item = reinterpret_cast<VectorItem *>(
                source->data(0, kItemRole).value<quintptr>());
            int layer = -1;
            int position = 0;
            if (target) {
                if (target->data(0, kItemRole).isValid()) {
                    layer = target->parent()->data(0, kLayerRole).toInt();
                    position = target->parent()->indexOfChild(target);
                    if (dropIndicatorPosition() == QAbstractItemView::BelowItem)
                        ++position;
                } else {
                    layer = target->data(0, kLayerRole).toInt();
                }
            }
            if (dropObject)
                dropObject(item, layer, position);
        }
        event->accept();
    }
};

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
    buildPagesDocker();

    m_opacityTimer = new QTimer(this);
    m_opacityTimer->setSingleShot(true);
    m_opacityTimer->setInterval(650);
    connect(m_opacityTimer, &QTimer::timeout, this, [this]() {
        finishOpacityEdit();
    });

    connect(m_view, &CanvasView::selectionChanged, this, [this](int) { refresh(); });
    connect(m_view, &CanvasView::pageChanged, this, [this](int) { refreshPages(); });
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

    auto *layerTools = new QWidget(content);
    auto *layerToolsLayout = new QHBoxLayout(layerTools);
    layerToolsLayout->setContentsMargins(0, 0, 0, 0);
    layerToolsLayout->setSpacing(2);

    auto *addLayerButton = new QToolButton(layerTools);
    addLayerButton->setText(QStringLiteral("+ Слой"));
    addLayerButton->setToolTip(QStringLiteral("Создать новый слой"));
    auto *removeLayerButton = new QToolButton(layerTools);
    removeLayerButton->setText(QStringLiteral("− Слой"));
    removeLayerButton->setToolTip(QStringLiteral("Удалить слой и его объекты"));
    auto *duplicate = iconButton(layerTools, QStringLiteral("copy"),
                                 QStringLiteral("Дублировать объект"));
    auto *remove = iconButton(layerTools, QStringLiteral("delete"),
                              QStringLiteral("Удалить объект"));
    layerToolsLayout->addWidget(addLayerButton);
    layerToolsLayout->addWidget(removeLayerButton);
    layerToolsLayout->addStretch(1);
    layerToolsLayout->addWidget(duplicate);
    layerToolsLayout->addWidget(remove);

    auto *orderTools = new QWidget(content);
    auto *orderToolsLayout = new QHBoxLayout(orderTools);
    orderToolsLayout->setContentsMargins(0, 0, 0, 0);
    orderToolsLayout->setSpacing(2);

    auto *up = new QToolButton(orderTools);
    up->setText(QStringLiteral("Выше"));
    up->setToolTip(QStringLiteral("Выше внутри слоя или слой наверх"));
    up->setAutoRaise(true);
    auto *down = new QToolButton(orderTools);
    down->setText(QStringLiteral("Ниже"));
    down->setToolTip(QStringLiteral("Ниже внутри слоя или слой вниз"));
    down->setAutoRaise(true);

    m_layerTarget = new QComboBox(orderTools);
    m_layerTarget->setMinimumWidth(110);
    m_layerTarget->setToolTip(QStringLiteral("Слой назначения"));
    auto *move = new QToolButton(orderTools);
    move->setText(QStringLiteral("Перенести"));
    move->setToolTip(QStringLiteral("Перенести выделенные объекты в выбранный слой"));

    orderToolsLayout->addWidget(up);
    orderToolsLayout->addWidget(down);
    orderToolsLayout->addSpacing(4);
    orderToolsLayout->addWidget(m_layerTarget, 1);
    orderToolsLayout->addWidget(move);

    auto *tree = new ObjectsTree(content);
    m_tree = tree;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(3);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setIndentation(14);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setColumnWidth(1, 24);
    m_tree->setColumnWidth(2, 24);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);

    layout->addWidget(layerTools);
    layout->addWidget(orderTools);
    layout->addWidget(m_tree, 1);

    const auto selectedLayer = [this]() -> int {
        QTreeWidgetItem *item = m_tree->selectedItems().value(0);
        if (!item)
            item = m_tree->currentItem();
        if (!item)
            return m_view->activeLayer();
        if (item->data(0, kItemRole).isValid() && item->parent())
            return item->parent()->data(0, kLayerRole).toInt();
        return item->data(0, kLayerRole).toInt();
    };

    connect(addLayerButton, &QToolButton::clicked, this, [this]() {
        m_view->addLayer();
        refresh();
        emit status(QStringLiteral("Создан новый слой."));
    });

    connect(removeLayerButton, &QToolButton::clicked, this, [this, selectedLayer]() {
        const int layer = selectedLayer();
        if (layer < 0) {
            emit status(QStringLiteral("Выберите слой в списке."));
            return;
        }
        if (m_view->layers().size() <= 1) {
            emit status(QStringLiteral("Нельзя удалить единственный слой."));
            return;
        }
        const int count = m_view->itemsInLayer(layer).size();
        if (count > 0) {
            const auto answer = QMessageBox::question(
                m_host, QStringLiteral("Удалить слой"),
                QStringLiteral("В слое «%1» объектов: %2. Удалить слой вместе с ними?")
                    .arg(m_view->layers().at(layer))
                    .arg(count));
            if (answer != QMessageBox::Yes)
                return;
        }
        m_view->removeLayer(layer);
        refresh();
    });

    connect(up, &QToolButton::clicked, this, [this, selectedLayer]() {
        const int layer = selectedLayer();
        QTreeWidgetItem *item = m_tree->currentItem();
        const bool layerSelected = item && !item->data(0, kItemRole).isValid();
        if (layerSelected) {
            if (layer + 1 < m_view->layers().size())
                m_view->moveLayer(layer, layer + 1);
        } else if (!m_view->selectedVectorItems().isEmpty()) {
            m_view->raiseSelection();
        }
        refresh();
    });

    connect(down, &QToolButton::clicked, this, [this, selectedLayer]() {
        const int layer = selectedLayer();
        QTreeWidgetItem *item = m_tree->currentItem();
        const bool layerSelected = item && !item->data(0, kItemRole).isValid();
        if (layerSelected) {
            if (layer > 0)
                m_view->moveLayer(layer, layer - 1);
        } else if (!m_view->selectedVectorItems().isEmpty()) {
            m_view->lowerSelection();
        }
        refresh();
    });

    connect(move, &QToolButton::clicked, this, [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty()) {
            emit status(QStringLiteral("Выберите объекты для переноса."));
            return;
        }
        if (m_layerTarget->currentIndex() < 0)
            return;
        const int layer = m_layerTarget->currentData().toInt();
        m_view->moveItemsToLayer(selected, layer);
        refresh();
        emit status(QStringLiteral("Объекты перенесены в слой «%1».")
                        .arg(m_view->layers().at(layer)));
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
            copy.group = 0;
            copy.id = 0;
            copy.masterId = 0;
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
        QTreeWidgetItem *item = m_tree->currentItem();
        if (!item)
            return;

        if (item->data(0, kGroupRole).isValid()) {
            if (item->parent())
                m_view->setActiveLayer(item->parent()->data(0, kLayerRole).toInt());
            m_view->selectGroupItems(item->data(0, kGroupRole).toInt());
            refreshProperties();
            refreshEffects();
            refreshText();
            return;
        }

        if (!item->data(0, kItemRole).isValid()) {
            const int layer = item->data(0, kLayerRole).toInt();
            m_view->setActiveLayer(layer);
            refreshTree();
            return;
        }

        const int parentLayer = item->parent()
                                    ? item->parent()->data(0, kLayerRole).toInt()
                                    : -1;
        if (parentLayer >= 0)
            m_view->setActiveLayer(parentLayer);
        if (parentLayer >= 0 && !m_view->layerVisible(parentLayer)) {
            emit status(QStringLiteral("Слой скрыт — объект нельзя выбрать."));
            return;
        }

        auto *vectorItem = reinterpret_cast<VectorItem *>(
            item->data(0, kItemRole).value<quintptr>());
        if (vectorItem && vectorItem->isLocked()) {
            emit status(QStringLiteral("Объект заблокирован — снимите замок в списке."));
            return;
        }
        if (vectorItem) {
            m_updating = true;
            m_view->scene()->clearSelection();
            vectorItem->setSelected(true);
            m_updating = false;
            refreshProperties();
            refreshEffects();
            refreshText();
            refreshTree();
        }
    });

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item,
                                                                  int) {
        if (!item || item->data(0, kItemRole).isValid())
            return;
        const int layer = item->data(0, kLayerRole).toInt();
        bool ok = false;
        const QString name = QInputDialog::getText(
            m_host, QStringLiteral("Имя слоя"), QStringLiteral("Название:"),
            QLineEdit::Normal, m_view->layers().at(layer), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            m_view->renameLayer(layer, name);
            refresh();
        }
    });

    auto *objectsTree = static_cast<ObjectsTree *>(m_tree);
    objectsTree->dropObject = [this](VectorItem *item, int layer, int position) {
        if (!item)
            return;
        if (layer < 0)
            layer = m_view->layers().size() - 1;
        m_view->setActiveLayer(layer);
        m_view->moveItemToLayer(item, layer, position);
        refresh();
        emit status(QStringLiteral("Объект перемещён в слой «%1».")
                        .arg(m_view->layers().at(layer)));
    };
    objectsTree->dropLayer = [this](int from, int to) {
        m_view->moveLayer(from, to);
        refresh();
        emit status(QStringLiteral("Порядок слоёв изменён."));
    };
    objectsTree->toggleVisible = [this](int layer) {
        m_view->setLayerVisible(layer, !m_view->layerVisible(layer));
        refresh();
        emit status(m_view->layerVisible(layer)
                        ? QStringLiteral("Слой «%1» показан.").arg(m_view->layers().at(layer))
                        : QStringLiteral("Слой «%1» скрыт.").arg(m_view->layers().at(layer)));
    };
    objectsTree->toggleLocked = [this](int layer) {
        m_view->setLayerLocked(layer, !m_view->layerLocked(layer));
        refresh();
        emit status(m_view->layerLocked(layer)
                        ? QStringLiteral("Слой «%1» заблокирован.").arg(m_view->layers().at(layer))
                        : QStringLiteral("Слой «%1» разблокирован.").arg(m_view->layers().at(layer)));
    };
    objectsTree->toggleObjectLock = [this](VectorItem *item) {
        if (!item)
            return;
        m_view->setItemLocked(item, !item->isLocked());
        refresh();
    };

    createDock(QStringLiteral("Объекты"), content);
}

void DockPanels::refreshTree()
{
    if (!m_tree)
        return;

    const QStringList layers = m_view->layers();
    const int scrollPosition = m_tree->verticalScrollBar()->value();

    m_updating = true;
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    QVector<QTreeWidgetItem *> layerItems(layers.size(), nullptr);
    bool objectSelected = false;

    for (int layer = layers.size() - 1; layer >= 0; --layer) {
        const bool active = layer == m_view->activeLayer();
        auto *layerItem = new QTreeWidgetItem(
            m_tree, {QStringLiteral("%1%2")
                         .arg(active ? QStringLiteral("● ") : QString())
                         .arg(layers.at(layer))});
        layerItem->setData(0, kLayerRole, layer);
        layerItems[layer] = layerItem;
        QFont layerFont = m_tree->font();
        layerFont.setBold(true);
        layerItem->setFont(0, layerFont);
        layerItem->setFlags(layerItem->flags() | Qt::ItemIsDragEnabled
                            | Qt::ItemIsDropEnabled);

        const bool visible = m_view->layerVisible(layer);
        const bool locked = m_view->layerLocked(layer);
        layerItem->setIcon(1, Icons::icon(visible ? QStringLiteral("eye")
                                                  : QStringLiteral("eye-off")));
        layerItem->setIcon(2, Icons::icon(locked ? QStringLiteral("lock")
                                                 : QStringLiteral("unlock")));
        layerItem->setToolTip(1, visible ? QStringLiteral("Скрыть слой")
                                         : QStringLiteral("Показать слой"));
        layerItem->setToolTip(2, locked ? QStringLiteral("Разблокировать слой")
                                        : QStringLiteral("Заблокировать слой"));
        if (!visible)
            layerItem->setForeground(0, QBrush(QColor(0x6f, 0x76, 0x81)));

        std::function<void(QTreeWidgetItem *, int)> addGroup =
            [this, layer, &addGroup, &objectSelected](QTreeWidgetItem *parent,
                                                       int groupId) {
                QTreeWidgetItem *groupItem =
                    new QTreeWidgetItem(parent, {QStringLiteral("Группа %1").arg(groupId)});
                groupItem->setData(0, kGroupRole, groupId);
                groupItem->setIcon(0, Icons::icon(QStringLiteral("group")));
                groupItem->setFlags(groupItem->flags() | Qt::ItemIsDropEnabled);
                QFont groupFont = m_tree->font();
                groupFont.setBold(true);
                groupItem->setFont(0, groupFont);

                const QList<VectorItem *> direct = m_view->itemsDirectlyInGroup(groupId);
                QList<VectorItem *> inLayer;
                for (VectorItem *item : direct) {
                    if (item->object().layer == layer)
                        inLayer.append(item);
                }
                for (int i = inLayer.size() - 1; i >= 0; --i) {
                    VectorItem *vectorItem = inLayer.at(i);
                    auto *entry = new QTreeWidgetItem(
                        groupItem, {vectorName(vectorItem->object())});
                    entry->setData(0, kItemRole,
                                   QVariant::fromValue<quintptr>(
                                       reinterpret_cast<quintptr>(vectorItem)));
                    entry->setFlags(entry->flags() | Qt::ItemIsDragEnabled
                                    | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                    if (!m_view->layerVisible(layer))
                        entry->setForeground(0, QBrush(QColor(0x6f, 0x76, 0x81)));
                    entry->setIcon(2, Icons::icon(vectorItem->isLocked()
                                                      ? QStringLiteral("lock")
                                                      : QStringLiteral("unlock")));
                    entry->setToolTip(2, vectorItem->isLocked()
                                              ? QStringLiteral("Разблокировать объект")
                                              : QStringLiteral("Заблокировать объект"));
                    if (vectorItem->isLocked())
                        entry->setForeground(0, QBrush(QColor(0x7c, 0x83, 0x8e)));
                    if (vectorItem->isSelected()) {
                        entry->setSelected(true);
                        m_tree->setCurrentItem(entry);
                        objectSelected = true;
                    }
                }

                for (int child : m_view->childGroupsOf(groupId))
                    addGroup(groupItem, child);
                groupItem->setExpanded(true);
            };

        QSet<int> layerGroups;
        for (VectorItem *item : m_view->itemsInLayer(layer)) {
            if (item->groupIndex() > 0) {
                int top = m_view->topGroupOf(item->groupIndex());
                layerGroups.insert(top);
            }
        }
        for (int group : layerGroups)
            addGroup(layerItem, group);

        const QList<VectorItem *> layerItems2 = m_view->itemsInLayer(layer);
        for (int i = layerItems2.size() - 1; i >= 0; --i) {
            VectorItem *vectorItem = layerItems2.at(i);
            if (vectorItem->groupIndex() > 0)
                continue;
            auto *entry = new QTreeWidgetItem(layerItem,
                                              {vectorName(vectorItem->object())});
            entry->setData(0, kItemRole,
                           QVariant::fromValue<quintptr>(
                               reinterpret_cast<quintptr>(vectorItem)));
            entry->setFlags(entry->flags() | Qt::ItemIsDragEnabled
                            | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            if (!m_view->layerVisible(layer))
                entry->setForeground(0, QBrush(QColor(0x6f, 0x76, 0x81)));
            entry->setIcon(2, Icons::icon(vectorItem->isLocked()
                                              ? QStringLiteral("lock")
                                              : QStringLiteral("unlock")));
            entry->setToolTip(2, vectorItem->isLocked()
                                     ? QStringLiteral("Разблокировать объект")
                                     : QStringLiteral("Заблокировать объект"));
            if (vectorItem->isLocked())
                entry->setForeground(0, QBrush(QColor(0x7c, 0x83, 0x8e)));
            if (vectorItem->isSelected()) {
                entry->setSelected(true);
                m_tree->setCurrentItem(entry);
                objectSelected = true;
            }
        }
        layerItem->setExpanded(true);
    }

    if (!objectSelected) {
        QTreeWidgetItem *activeItem = layerItems.value(m_view->activeLayer(), nullptr);
        if (activeItem) {
            activeItem->setSelected(true);
            m_tree->setCurrentItem(activeItem);
        }
    }

    if (m_layerTarget) {
        const QSignalBlocker comboBlocker(m_layerTarget);
        m_layerTarget->clear();
        for (int layer = layers.size() - 1; layer >= 0; --layer)
            m_layerTarget->addItem(layers.at(layer), layer);
        const int index = m_layerTarget->findData(m_view->activeLayer());
        if (index >= 0)
            m_layerTarget->setCurrentIndex(index);
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

    auto *gradientButton = new QToolButton(content);
    gradientButton->setText(QStringLiteral(" Градиент…"));
    gradientButton->setIcon(Icons::icon(QStringLiteral("swatch"), QColor("#8a5cf6")));
    gradientButton->setIconSize(QSize(18, 18));
    gradientButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    gradientButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(gradientButton);
    layout->addLayout(widthRow);

    connect(gradientButton, &QToolButton::clicked, this,
            &DockPanels::openGradientDialog);

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
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            &DockPanels::applyOpacityLive);
    connect(m_opacitySlider, &QSlider::sliderPressed, this,
            [this]() { beginOpacityEdit(); });
    connect(m_opacitySlider, &QSlider::sliderReleased, this,
            [this]() { finishOpacityEdit(); });
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
        const VectorObject firstObject = items.first()->object();
        if (items.size() > 1) {
            QRectF groupBounds;
            for (VectorItem *item : items) {
                const QRectF visual = item->mapRectToScene(item->localBounds());
                groupBounds = groupBounds.isNull()
                                  ? visual
                                  : groupBounds.united(visual);
            }
            m_x->setValue(groupBounds.x());
            m_y->setValue(groupBounds.y());
            m_w->setValue(groupBounds.width());
            m_h->setValue(groupBounds.height());
            m_angle->setValue(items.first()->object().rotation);
        } else {
            const QRectF bounds = firstObject.localBounds();
            m_x->setValue(firstObject.pos.x());
            m_y->setValue(firstObject.pos.y());
            m_w->setValue(bounds.width());
            m_h->setValue(bounds.height());
            m_angle->setValue(firstObject.rotation);
        }
        m_opacitySlider->setValue(qRound(firstObject.opacity * 100.0));
        m_opacityValue->setText(QStringLiteral("%1 %").arg(m_opacitySlider->value()));
        if (m_effectOpacity) {
            const QSignalBlocker blocker(m_effectOpacity);
            m_effectOpacity->setValue(m_opacitySlider->value());
            m_effectOpacityValue->setText(
                QStringLiteral("%1 %").arg(m_opacitySlider->value()));
        }
        m_strokeWidth->setValue(firstObject.strokeWidth);
        m_outlineColor = firstObject.hasStroke ? firstObject.stroke : m_outlineColor;
        colorButtonIcon(m_outlineColorButton, m_outlineColor);
        m_outlineWidth->setValue(firstObject.strokeWidth);
        m_capCombo->setCurrentIndex(qBound(0, firstObject.capStyle, 2));
        m_joinCombo->setCurrentIndex(qBound(0, firstObject.joinStyle, 2));
        m_miterSpin->setValue(firstObject.miterLimit);
        QStringList dashParts;
        for (qreal dash : firstObject.dashPattern)
            dashParts << QString::number(dash, 'g', 4);
        m_dashEdit->setText(dashParts.join(QLatin1Char(' ')));
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

    if (selected.size() > 1) {
        m_view->scaleSelectionTo(QRectF(position, size), opacity);
    } else {
        emit beginEdit();
        VectorItem *item = selected.first();
        item->setFrameSize(size);
        item->setRotationAngle(angle);
        item->setPos(position);
        item->setObjectOpacity(opacity);
        emit endEdit();
    }
    emit status(selected.size() == 1
                    ? QStringLiteral("Свойства объекта применены.")
                    : QStringLiteral("Для группы применены позиция и прозрачность."));
}

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------


void DockPanels::openGradientDialog()
{
    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty()) {
        emit status(QStringLiteral("Сначала выберите объекты для градиента."));
        return;
    }

    const VectorObject first = selected.first()->object();
    QVector<GradientStop> stops = first.gradientStops;
    if (stops.size() < 2) {
        stops = {GradientStop{0.0, first.fill, 1.0},
                 GradientStop{1.0, first.fill2, 1.0}};
    }
    QColor meshTopRight = first.fill2;
    QColor meshBottomLeft = first.meshColor3;
    QColor meshBottomRight = first.meshColor4;

    QDialog dialog(m_host);
    dialog.setWindowTitle(QStringLiteral("Заливка и градиент"));
    dialog.resize(420, 560);
    auto *form = new QFormLayout(&dialog);

    auto *type = new QComboBox(&dialog);
    type->addItems({QStringLiteral("Сплошная"), QStringLiteral("Линейный градиент"),
                    QStringLiteral("Радиальный градиент"),
                    QStringLiteral("Конический градиент"),
                    QStringLiteral("Многоточечный градиент"),
                    QStringLiteral("Свободный градиент"),
                    QStringLiteral("Mesh (4 угла)")});
    type->setCurrentIndex(qBound(0, first.fillStyle, 6));
    form->addRow(QStringLiteral("Тип заливки"), type);

    auto *list = new QListWidget(&dialog);
    list->setMinimumHeight(140);
    form->addRow(QStringLiteral("Цветовые точки"), list);

    auto *stopRow = new QHBoxLayout;
    auto *addStop = new QPushButton(QStringLiteral("Добавить"), &dialog);
    auto *removeStop = new QPushButton(QStringLiteral("Удалить"), &dialog);
    auto *stopColor = new QPushButton(&dialog);
    auto *position = spinField(&dialog, 0, 100, 100, QStringLiteral(" %"), 0, 60);
    auto *opacity = spinField(&dialog, 0, 100, 100, QStringLiteral(" %"), 0, 60);
    stopRow->addWidget(addStop);
    stopRow->addWidget(removeStop);
    stopRow->addWidget(stopColor, 1);
    stopRow->addWidget(position);
    stopRow->addWidget(opacity);
    form->addRow(QStringLiteral("Точка"), stopRow);

    auto *angle = spinField(&dialog, 0, 359, qRound(first.gradientAngle),
                            QStringLiteral(" °"), 0, 76);
    auto *centerX = spinField(&dialog, 0, 100,
                              qRound(first.gradientCenter.x() * 100),
                              QStringLiteral(" %"), 0, 60);
    auto *centerY = spinField(&dialog, 0, 100,
                              qRound(first.gradientCenter.y() * 100),
                              QStringLiteral(" %"), 0, 60);
    auto *radius = spinField(&dialog, 1, 400,
                             qRound(first.gradientRadius * 100),
                             QStringLiteral(" %"), 0, 60);
    form->addRow(QStringLiteral("Угол"), angle);
    auto *centerRow = new QHBoxLayout;
    centerRow->addWidget(centerX);
    centerRow->addWidget(centerY);
    form->addRow(QStringLiteral("Центр X / Y"), centerRow);
    form->addRow(QStringLiteral("Радиус"), radius);

    auto *meshRow = new QHBoxLayout;
    auto *meshTR = new QPushButton(&dialog);
    auto *meshBL = new QPushButton(&dialog);
    auto *meshBR = new QPushButton(&dialog);
    const auto labelButton = [](QPushButton *button, const QColor &color) {
        button->setText(color.name(QColor::HexArgb).toUpper());
        button->setStyleSheet(QStringLiteral("text-align:left; padding:4px;"));
    };
    labelButton(meshTR, meshTopRight);
    labelButton(meshBL, meshBottomLeft);
    labelButton(meshBR, meshBottomRight);
    meshRow->addWidget(meshTR, 1);
    meshRow->addWidget(meshBL, 1);
    meshRow->addWidget(meshBR, 1);
    form->addRow(QStringLiteral("Mesh TR / BL / BR"), meshRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    int current = 0;
    const auto refreshStops = [&]() {
        const QSignalBlocker blocker(list);
        list->clear();
        for (const GradientStop &stop : stops) {
            auto *entry = new QListWidgetItem(
                QStringLiteral("%1   %2   %3%")
                    .arg(stop.position * 100.0, 0, 'f', 1)
                    .arg(stop.color.name(QColor::HexRgb).toUpper())
                    .arg(stop.opacity * 100.0, 0, 'f', 0));
            QPixmap swatch(18, 18);
            swatch.fill(stop.color);
            entry->setIcon(QIcon(swatch));
            list->addItem(entry);
        }
        current = qBound(0, current, qMax(0, stops.size() - 1));
        if (list->count() > 0)
            list->setCurrentRow(current);
    };
    const auto loadCurrent = [&]() {
        if (stops.isEmpty())
            return;
        current = qBound(0, current, stops.size() - 1);
        const GradientStop &stop = stops.at(current);
        QSignalBlocker positionBlocker(position);
        QSignalBlocker opacityBlocker(opacity);
        position->setValue(qRound(stop.position * 100.0));
        opacity->setValue(qRound(stop.opacity * 100.0));
        labelButton(stopColor, stop.color);
    };
    refreshStops();
    loadCurrent();

    connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        current = row;
        loadCurrent();
    });
    connect(addStop, &QPushButton::clicked, &dialog, [&]() {
        qreal at = 0.5;
        if (!stops.isEmpty()) {
            std::sort(stops.begin(), stops.end(),
                      [](const GradientStop &a, const GradientStop &b) {
                          return a.position < b.position;
                      });
            at = qBound(0.0, (stops.first().position + stops.last().position) / 2.0,
                        1.0);
        }
        stops.append(GradientStop{at, stops.isEmpty() ? QColor(Qt::white)
                                                      : stops.first().color,
                                  1.0});
        current = stops.size() - 1;
        refreshStops();
        loadCurrent();
    });
    connect(removeStop, &QPushButton::clicked, &dialog, [&]() {
        if (stops.size() <= 2 || current < 0 || current >= stops.size())
            return;
        stops.removeAt(current);
        refreshStops();
        loadCurrent();
    });
    connect(position, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        if (current < 0 || current >= stops.size())
            return;
        stops[current].position = qBound(0.0, value / 100.0, 1.0);
    });
    connect(opacity, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        if (current < 0 || current >= stops.size())
            return;
        stops[current].opacity = qBound(0.0, value / 100.0, 1.0);
    });
    connect(stopColor, &QPushButton::clicked, &dialog, [&]() {
        if (current < 0 || current >= stops.size())
            return;
        const QColor picked = QColorDialog::getColor(
            stops.at(current).color, m_host, QStringLiteral("Цвет точки"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return;
        stops[current].color = picked;
        labelButton(stopColor, picked);
        refreshStops();
    });
    connect(meshTR, &QPushButton::clicked, &dialog, [&]() {
        const QColor picked = QColorDialog::getColor(meshTopRight, m_host,
                                                     QStringLiteral("Правый верхний угол"));
        if (picked.isValid()) {
            meshTopRight = picked;
            labelButton(meshTR, picked);
        }
    });
    connect(meshBL, &QPushButton::clicked, &dialog, [&]() {
        const QColor picked = QColorDialog::getColor(meshBottomLeft, m_host,
                                                     QStringLiteral("Левый нижний угол"));
        if (picked.isValid()) {
            meshBottomLeft = picked;
            labelButton(meshBL, picked);
        }
    });
    connect(meshBR, &QPushButton::clicked, &dialog, [&]() {
        const QColor picked = QColorDialog::getColor(meshBottomRight, m_host,
                                                     QStringLiteral("Правый нижний угол"));
        if (picked.isValid()) {
            meshBottomRight = picked;
            labelButton(meshBR, picked);
        }
    });

    if (dialog.exec() != QDialog::Accepted)
        return;

    std::sort(stops.begin(), stops.end(),
              [](const GradientStop &a, const GradientStop &b) {
                  return a.position < b.position;
              });
    const int style = type->currentIndex();
    emit beginEdit();
    for (VectorItem *item : selected) {
        if (style == 0) {
            item->setFillColor(stops.isEmpty() ? item->object().fill
                                               : stops.first().color);
            continue;
        }
        item->setGradientDefinition(style, stops, angle->value(),
                                    QPointF(centerX->value() / 100.0,
                                            centerY->value() / 100.0),
                                    radius->value() / 100.0);
        if (style == 6)
            item->setMeshColors(meshTopRight, meshBottomLeft, meshBottomRight);
    }
    emit endEdit();
    emit status(style == 0
                    ? QStringLiteral("Заливка снова сплошная.")
                    : QStringLiteral("Градиент применён к объектам: %1")
                          .arg(selected.size()));
}

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

    auto *gradient = new QToolButton(content);
    gradient->setText(QStringLiteral("Градиент…"));
    gradient->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(gradient, &QToolButton::clicked, this,
            &DockPanels::openGradientDialog);
    layout->addWidget(gradient);

    auto *hint = new QLabel(QStringLiteral("ЛКМ по образцу — заливка\n"
                                           "ПКМ по образцу — обводка"),
                            content);
    hint->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    layout->addWidget(hint);
    layout->addStretch(1);

    m_colorPreview->installEventFilter(this);
    m_colorPreview->setCursor(Qt::PointingHandCursor);

    m_colorTimer = new QTimer(this);
    m_colorTimer->setSingleShot(true);
    m_colorTimer->setInterval(650);
    connect(m_colorTimer, &QTimer::timeout, this, [this]() {
        if (m_colorEditActive) {
            m_colorEditActive = false;
            emit endEdit();
        }
    });

    const auto refreshColor = [this]() {
        if (m_updating)
            return;
        m_color = QColor::fromHsv(m_hue->value(), m_saturation->value(),
                                  m_value->value(), m_alpha->value());
        updateColorPreview();

        m_view->setFillColor(m_color);
        updateColorButtons();

        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty())
            return;

        if (!m_colorEditActive) {
            m_colorEditActive = true;
            emit beginEdit();
        }
        for (VectorItem *item : selected)
            item->setFillColor(m_color);
        m_colorTimer->start();
    };
    connect(m_hue, &QSlider::valueChanged, this, refreshColor);
    connect(m_saturation, &QSlider::valueChanged, this, refreshColor);
    connect(m_value, &QSlider::valueChanged, this, refreshColor);
    connect(m_alpha, &QSlider::valueChanged, this, refreshColor);

    const auto beginColorEdit = [this]() {
        if (!m_colorEditActive) {
            m_colorEditActive = true;
            emit beginEdit();
        }
        m_colorTimer->stop();
    };
    for (QSlider *slider : {m_hue, m_saturation, m_value, m_alpha}) {
        connect(slider, &QSlider::sliderPressed, this, beginColorEdit);
        connect(slider, &QSlider::sliderReleased, this, [this]() {
            m_colorTimer->stop();
            if (m_colorEditActive) {
                m_colorEditActive = false;
                emit endEdit();
            }
        });
    }

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
        applyColor(m_color, true);
    });

    m_color = QColor::fromHsv(m_hue->value(), m_saturation->value(),
                              m_value->value(), m_alpha->value());
    updateColorPreview();
    createDock(QStringLiteral("Цвет"), content);
}

bool DockPanels::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_colorPreview && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            applyColor(m_color, true);
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton) {
            applyColor(m_color, false);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
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

    const auto opacityRowFor = [&](const QString &name, QSlider *&slider) {
        auto *row = new QHBoxLayout;
        row->addWidget(fieldLabel(name, transparencyTab));
        slider = new QSlider(Qt::Horizontal, transparencyTab);
        slider->setRange(0, 100);
        slider->setValue(100);
        row->addWidget(slider, 1);
        transparencyLayout->addLayout(row);
    };
    opacityRowFor(QStringLiteral("Заливка"), m_fillOpacity);
    opacityRowFor(QStringLiteral("Обводка"), m_strokeOpacity);

    auto *blendRow = new QHBoxLayout;
    blendRow->addWidget(fieldLabel(QStringLiteral("Режим"), transparencyTab));
    m_blendMode = new QComboBox(transparencyTab);
    m_blendMode->addItems({QStringLiteral("Обычный"), QStringLiteral("Умножение"),
                           QStringLiteral("Экран"), QStringLiteral("Перекрытие"),
                           QStringLiteral("Замена тёмным"), QStringLiteral("Замена светлым"),
                           QStringLiteral("Осветление"), QStringLiteral("Затемнение"),
                           QStringLiteral("Жёсткий свет"), QStringLiteral("Мягкий свет"),
                           QStringLiteral("Разница"), QStringLiteral("Исключение")});
    blendRow->addWidget(m_blendMode, 1);
    transparencyLayout->addLayout(blendRow);

    const auto applyOpacityParts = [this]() {
        const QList<VectorItem *> selected = selection();
        if (selected.isEmpty())
            return;
        emit beginEdit();
        for (VectorItem *item : selected) {
            item->setFillOpacity(m_fillOpacity->value() / 100.0);
            item->setStrokeOpacity(m_strokeOpacity->value() / 100.0);
            item->setBlendMode(m_blendMode->currentIndex());
        }
        emit endEdit();
    };
    connect(m_fillOpacity, &QSlider::valueChanged, this,
            [applyOpacityParts](int) { applyOpacityParts(); });
    connect(m_strokeOpacity, &QSlider::valueChanged, this,
            [applyOpacityParts](int) { applyOpacityParts(); });
    connect(m_blendMode, &QComboBox::currentIndexChanged, this,
            [applyOpacityParts](int) { applyOpacityParts(); });

    auto *transparencyHint = new QLabel(
        QStringLiteral("Прозрачность заливки и обводки независима; режим "
                       "наложения смешивает объект с тем, что под ним."),
        transparencyTab);
    transparencyHint->setWordWrap(true);
    transparencyHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    transparencyLayout->addWidget(transparencyHint);
    transparencyLayout->addStretch(1);

    connect(m_effectOpacity, &QSlider::valueChanged, this,
            &DockPanels::applyOpacityLive);
    connect(m_effectOpacity, &QSlider::sliderPressed, this,
            [this]() { beginOpacityEdit(); });
    connect(m_effectOpacity, &QSlider::sliderReleased, this,
            [this]() { finishOpacityEdit(); });

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

void DockPanels::beginOpacityEdit()
{
    if (!m_opacityEditActive) {
        m_opacityEditActive = true;
        emit beginEdit();
    }
    if (m_opacityTimer)
        m_opacityTimer->stop();
}

void DockPanels::finishOpacityEdit()
{
    if (m_opacityTimer)
        m_opacityTimer->stop();
    if (m_opacityEditActive) {
        m_opacityEditActive = false;
        emit endEdit();
    }
}

void DockPanels::applyOpacityLive(int percent)
{
    if (m_updating)
        return;

    if (m_opacityValue)
        m_opacityValue->setText(QStringLiteral("%1 %").arg(percent));
    if (m_effectOpacityValue)
        m_effectOpacityValue->setText(QStringLiteral("%1 %").arg(percent));

    if (m_opacitySlider && m_opacitySlider->value() != percent) {
        const QSignalBlocker blocker(m_opacitySlider);
        m_opacitySlider->setValue(percent);
    }
    if (m_effectOpacity && m_effectOpacity->value() != percent) {
        const QSignalBlocker blocker(m_effectOpacity);
        m_effectOpacity->setValue(percent);
    }

    const QList<VectorItem *> selected = selection();
    if (selected.isEmpty())
        return;

    if (!m_opacityEditActive) {
        m_opacityEditActive = true;
        emit beginEdit();
    }
    for (VectorItem *item : selected)
        item->setObjectOpacity(percent / 100.0);
    if (m_opacityTimer)
        m_opacityTimer->start();
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
        if (m_opacitySlider) {
            const QSignalBlocker blocker(m_opacitySlider);
            m_opacitySlider->setValue(m_effectOpacity->value());
            m_opacityValue->setText(
                QStringLiteral("%1 %").arg(m_effectOpacity->value()));
        }
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

    auto *capRow = new QHBoxLayout;
    capRow->addWidget(fieldLabel(QStringLiteral("Окончание"), content));
    m_capCombo = new QComboBox(content);
    m_capCombo->addItems({QStringLiteral("Плоское"), QStringLiteral("Квадратное"),
                          QStringLiteral("Круглое")});
    capRow->addWidget(m_capCombo, 1);
    layout->addLayout(capRow);

    auto *joinRow = new QHBoxLayout;
    joinRow->addWidget(fieldLabel(QStringLiteral("Соединение"), content));
    m_joinCombo = new QComboBox(content);
    m_joinCombo->addItems({QStringLiteral("Угол"), QStringLiteral("Срез"),
                           QStringLiteral("Круг")});
    joinRow->addWidget(m_joinCombo, 1);
    layout->addLayout(joinRow);

    auto *miterRow = new QHBoxLayout;
    miterRow->addWidget(fieldLabel(QStringLiteral("Предел среза"), content));
    m_miterSpin = spinField(content, 1, 20, 2, QString(), 1, 70);
    miterRow->addWidget(m_miterSpin);
    miterRow->addStretch(1);
    layout->addLayout(miterRow);

    auto *dashRow = new QHBoxLayout;
    dashRow->addWidget(fieldLabel(QStringLiteral("Штрихи"), content));
    m_dashEdit = new QLineEdit(content);
    m_dashEdit->setPlaceholderText(QStringLiteral("например 6 3 (пусто — сплошная)"));
    dashRow->addWidget(m_dashEdit, 1);
    layout->addLayout(dashRow);

    auto *apply = new QToolButton(content);
    apply->setText(QStringLiteral("Применить обводку"));
    apply->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(apply);

    auto *toCurve = new QToolButton(content);
    toCurve->setText(QStringLiteral("Обводка в кривую"));
    toCurve->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(toCurve);

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
        QVector<qreal> dashes;
        const QString dashText = m_dashEdit->text();
        static const QRegularExpression dashNumber(
            QStringLiteral("[-+]?(?:\\d*\\.\\d+|\\d+\\.?)"));
        auto it = dashNumber.globalMatch(dashText);
        while (it.hasNext())
            dashes.append(it.next().captured(0).toDouble());

        emit beginEdit();
        for (VectorItem *item : selected) {
            item->setStrokeColor(m_outlineColor);
            item->setStrokeWidth(m_outlineWidth->value());
            item->setStrokeStyle(m_capCombo->currentIndex(),
                                 m_joinCombo->currentIndex(),
                                 m_miterSpin->value(), dashes);
        }
        emit endEdit();
        refreshProperties();
        emit status(QStringLiteral("Обводка применена."));
    });

    connect(toCurve, &QToolButton::clicked, this, [this]() {
        m_view->convertStrokeToPath();
        refresh();
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

    auto *spacingRow = new QHBoxLayout;
    spacingRow->addWidget(fieldLabel(QStringLiteral("Межбукв."), m_textContent));
    m_letterSpacing = spinField(m_textContent, -20, 100, 0, QStringLiteral(" px"), 1, 64);
    spacingRow->addWidget(m_letterSpacing);
    spacingRow->addSpacing(6);
    spacingRow->addWidget(fieldLabel(QStringLiteral("Межстроч."), m_textContent));
    m_lineSpacing = spinField(m_textContent, 0.5, 5.0, 1.0, QString(), 2, 64);
    m_lineSpacing->setSingleStep(0.1);
    spacingRow->addWidget(m_lineSpacing);
    spacingRow->addStretch(1);
    layout->addLayout(spacingRow);

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
    connect(m_letterSpacing, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (!m_updating)
            applyText();
    });
    connect(m_lineSpacing, &QDoubleSpinBox::editingFinished, this, [this]() {
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
        m_letterSpacing->setValue(object.letterSpacing);
        m_lineSpacing->setValue(object.lineSpacing);
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
        item->setLetterSpacing(m_letterSpacing->value());
        item->setLineSpacing(m_lineSpacing->value());
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

// ---------------------------------------------------------------------------
// Pages and master pages
// ---------------------------------------------------------------------------

void DockPanels::buildPagesDocker()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_pagesList = new QListWidget(content);
    m_pagesList->setViewMode(QListView::IconMode);
    m_pagesList->setResizeMode(QListView::Adjust);
    m_pagesList->setMovement(QListView::Static);
    m_pagesList->setIconSize(QSize(92, 122));
    m_pagesList->setGridSize(QSize(108, 156));
    m_pagesList->setSpacing(4);
    m_pagesList->setWordWrap(true);
    layout->addWidget(m_pagesList, 1);

    auto *buttons = new QHBoxLayout;
    const auto makeButton = [&](const QString &text, const QString &tip) {
        auto *button = new QToolButton(content);
        button->setText(text);
        button->setToolTip(tip);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        buttons->addWidget(button);
        return button;
    };
    auto *addButton = makeButton(QStringLiteral("+"),
                                 QStringLiteral("Добавить страницу"));
    auto *duplicateButton = makeButton(QStringLiteral("⧉"),
                                       QStringLiteral("Дублировать страницу"));
    auto *removeButton = makeButton(QStringLiteral("−"),
                                    QStringLiteral("Удалить страницу"));
    auto *upButton = makeButton(QStringLiteral("↑"),
                                QStringLiteral("Переместить выше"));
    auto *downButton = makeButton(QStringLiteral("↓"),
                                  QStringLiteral("Переместить ниже"));
    layout->addLayout(buttons);

    layout->addWidget(separator(content));

    auto *masterRow = new QHBoxLayout;
    masterRow->addWidget(fieldLabel(QStringLiteral("Мастер"), content));
    m_masterCombo = new QComboBox(content);
    masterRow->addWidget(m_masterCombo, 1);
    layout->addLayout(masterRow);

    auto *masterButtons = new QHBoxLayout;
    auto *createMaster = new QToolButton(content);
    createMaster->setText(QStringLiteral("Создать мастер"));
    createMaster->setToolTip(QStringLiteral("Скопировать текущую страницу в мастер"));
    auto *openMaster = new QToolButton(content);
    openMaster->setText(QStringLiteral("Открыть"));
    openMaster->setToolTip(QStringLiteral("Редактировать мастер-страницу"));
    masterButtons->addWidget(createMaster, 1);
    masterButtons->addWidget(openMaster);
    layout->addLayout(masterButtons);

    auto *headerHint = new QLabel(QStringLiteral(
        "Колонтитулы и нумерация мастер-страницы видны на всех "
        "страницах, к которым она применена."), content);
    headerHint->setWordWrap(true);
    headerHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    layout->addWidget(headerHint);

    connect(m_pagesList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0)
            return;
        const int pages = m_view->pageList().size();
        m_view->setCurrentPage(row < pages ? row : -1 - (row - pages));
    });
    connect(m_pagesList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (m_updating || !item)
                    return;
                const int row = m_pagesList->row(item);
                const int pages = m_view->pageList().size();
                bool ok = false;
                if (row < pages) {
                    const QString name = QInputDialog::getText(
                        m_host, QStringLiteral("Имя страницы"),
                        QStringLiteral("Название:"), QLineEdit::Normal,
                        m_view->pageLabel(row), &ok);
                    if (ok)
                        m_view->renamePage(row, name);
                } else {
                    const int master = row - pages;
                    const QString name = QInputDialog::getText(
                        m_host, QStringLiteral("Имя мастера"),
                        QStringLiteral("Название:"), QLineEdit::Normal,
                        m_view->pageLabel(-1 - master), &ok);
                    if (ok)
                        m_view->renameMaster(master, name);
                }
                refreshPages();
            });

    connect(addButton, &QToolButton::clicked, this, [this]() {
        m_view->addPage();
        refreshPages();
    });
    connect(duplicateButton, &QToolButton::clicked, this, [this]() {
        m_view->duplicatePage(qMax(0, m_view->currentPage()));
        refreshPages();
    });
    connect(removeButton, &QToolButton::clicked, this, [this]() {
        m_view->removePage(qMax(0, m_view->currentPage()));
        refreshPages();
    });
    connect(upButton, &QToolButton::clicked, this, [this]() {
        const int page = m_view->currentPage();
        if (page > 0) {
            m_view->movePage(page, page - 1);
            refreshPages();
        }
    });
    connect(downButton, &QToolButton::clicked, this, [this]() {
        const int page = m_view->currentPage();
        if (page >= 0 && page + 1 < m_view->pageList().size()) {
            m_view->movePage(page, page + 1);
            refreshPages();
        }
    });
    connect(createMaster, &QToolButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            m_host, QStringLiteral("Новая мастер-страница"),
            QStringLiteral("Название:"), QLineEdit::Normal,
            QStringLiteral("Мастер %1").arg(m_view->masterList().size() + 1), &ok);
        if (!ok)
            return;
        const int master = m_view->addMaster(name);
        if (m_view->currentPage() >= 0) {
            m_view->applyMasterToPage(m_view->currentPage(), master);
            // Copy the current page artwork into the new master page.
            const QVector<VectorObject> objects = m_view->objects();
            const int page = m_view->currentPage();
            m_view->setCurrentPage(-1 - master);
            for (const VectorObject &object : objects) {
                if (object.page != page)
                    continue;
                VectorObject copy = object;
                copy.page = -1 - master;
                copy.id = 0;
                copy.group = 0;
                m_view->addVectorObject(copy, false, false);
            }
            m_view->setCurrentPage(page);
        }
        refreshPages();
        emit status(QStringLiteral("Мастер-страница создана."));
    });
    connect(openMaster, &QToolButton::clicked, this, [this]() {
        const int current = m_view->currentPage();
        if (current < 0)
            return;
        int master = m_masterCombo->currentIndex() - 1;
        if (master < 0)
            master = 0;
        if (m_view->masterList().isEmpty())
            return;
        m_view->setCurrentPage(-1 - qBound(0, master, m_view->masterList().size() - 1));
        refreshPages();
    });
    connect(m_masterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating)
            return;
        const int page = m_view->currentPage();
        if (page < 0)
            return;
        m_view->applyMasterToPage(page, index - 1);
        refreshPages();
    });

    createDock(QStringLiteral("Страницы"), content);
    refreshPages();
}

void DockPanels::refreshPages()
{
    if (!m_pagesList)
        return;
    m_updating = true;
    const QSignalBlocker blocker(m_pagesList);
    m_pagesList->clear();

    const QVector<PageInfo> pages = m_view->pageList();
    for (int i = 0; i < pages.size(); ++i) {
        const QImage preview = m_view->renderPageImage(i, QSize(184, 244));
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(preview)),
                                         pages.at(i).name, m_pagesList);
        item->setToolTip(QStringLiteral("%1\n%2 × %3 px\nМастер: %4")
                             .arg(pages.at(i).name)
                             .arg(qRound(pages.at(i).size.width()))
                             .arg(qRound(pages.at(i).size.height()))
                             .arg(pages.at(i).master >= 0
                                      ? m_view->pageLabel(-1 - pages.at(i).master)
                                      : QStringLiteral("нет")));
    }
    const QVector<MasterPage> masters = m_view->masterList();
    for (int i = 0; i < masters.size(); ++i) {
        const QImage preview = m_view->renderPageImage(-1 - i, QSize(184, 244));
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(preview)),
                                         QStringLiteral("★ %1").arg(masters.at(i).name),
                                         m_pagesList);
        item->setToolTip(QStringLiteral("Мастер-страница «%1»").arg(masters.at(i).name));
    }

    const int current = m_view->currentPage();
    if (current >= 0)
        m_pagesList->setCurrentRow(current);
    else
        m_pagesList->setCurrentRow(pages.size() + (-1 - current));

    const QSignalBlocker comboBlocker(m_masterCombo);
    m_masterCombo->clear();
    m_masterCombo->addItem(QStringLiteral("Без мастера"));
    for (const MasterPage &master : masters)
        m_masterCombo->addItem(master.name);
    if (current >= 0 && current < pages.size())
        m_masterCombo->setCurrentIndex(pages.at(current).master + 1);
    else
        m_masterCombo->setCurrentIndex(0);

    m_updating = false;
}
