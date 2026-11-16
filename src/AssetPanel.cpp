#include "AssetPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTabWidget>
#include <QListView>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include "AssetLibrary.h"
#include "CanvasView.h"
#include "Document.h"
#include "PatternStore.h"
#include "Patterns.h"

namespace {

const char *kAssetMime = "application/x-vectora-asset";

class AssetListWidget : public QListWidget
{
public:
    explicit AssetListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::CopyAction);

    }

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *item = currentItem();
        if (!item)
            return;
        const QString svg = item->data(kSvgRole).toString();
        if (svg.isEmpty())
            return;

        auto *mime = new QMimeData;
        mime->setData(QString::fromLatin1(kAssetMime), svg.toUtf8());
        mime->setText(item->text());

        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        const QIcon icon = item->icon();
        if (!icon.isNull())
            drag->setPixmap(icon.pixmap(64, 64));
        drag->exec(Qt::CopyAction);
    }

public:
    static int kSvgRole;
    static int kLibraryRole;
    static int kIndexRole;
};

int AssetListWidget::kSvgRole = Qt::UserRole + 11;
int AssetListWidget::kLibraryRole = Qt::UserRole + 12;
int AssetListWidget::kIndexRole = Qt::UserRole + 13;

QToolButton *makeButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

} // namespace

AssetPanel::AssetPanel(CanvasView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
{
    buildUi();
    connect(&AssetStore::instance(), &AssetStore::changed,
            this, &AssetPanel::refresh);
    if (m_view) {
        connect(m_view, &CanvasView::selectionChanged, this, [this](int) {
            refreshSymbols();
        });
        connect(m_view, &CanvasView::symbolsChanged, this, &AssetPanel::refreshSymbols);
    }
    refresh();
}

void AssetPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // --- library selector -------------------------------------------------
    auto *libraryRow = new QHBoxLayout;
    libraryRow->setSpacing(4);
    m_libraries = new QComboBox(this);
    m_libraries->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_libraries->setToolTip(QStringLiteral("Активная библиотека ресурсов"));
    libraryRow->addWidget(m_libraries, 1);

    auto *newLibraryButton = new QToolButton(this);
    newLibraryButton->setText(QStringLiteral("+"));
    newLibraryButton->setToolTip(QStringLiteral("Создать библиотеку"));
    libraryRow->addWidget(newLibraryButton);

    auto *importLibraryButton = new QToolButton(this);
    importLibraryButton->setText(QStringLiteral("⤓"));
    importLibraryButton->setToolTip(QStringLiteral("Импортировать библиотеку (.vlib)"));
    libraryRow->addWidget(importLibraryButton);

    auto *exportLibraryButton = new QToolButton(this);
    exportLibraryButton->setText(QStringLiteral("⤒"));
    exportLibraryButton->setToolTip(QStringLiteral("Экспортировать библиотеку (.vlib)"));
    libraryRow->addWidget(exportLibraryButton);

    auto *undoButton = new QToolButton(this);
    undoButton->setText(QStringLiteral("↶"));
    undoButton->setToolTip(QStringLiteral("Отменить изменение библиотеки"));
    libraryRow->addWidget(undoButton);

    auto *redoButton = new QToolButton(this);
    redoButton->setText(QStringLiteral("↷"));
    redoButton->setToolTip(QStringLiteral("Вернуть изменение библиотеки"));
    libraryRow->addWidget(redoButton);
    root->addLayout(libraryRow);

    // --- filters ----------------------------------------------------------
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(4);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Поиск по названию…"));
    m_search->setClearButtonEnabled(true);
    filterRow->addWidget(m_search, 1);

    m_categories = new QComboBox(this);
    m_categories->setToolTip(QStringLiteral("Категория ресурсов"));
    filterRow->addWidget(m_categories);
    root->addLayout(filterRow);

    m_favorites = new QCheckBox(QStringLiteral("Только избранные"), this);
    root->addWidget(m_favorites);

    auto *tabs = new QTabWidget(this);
    m_tabs = tabs;

    // --- assets page ------------------------------------------------------
    auto *assetsPage = new QWidget(tabs);
    auto *assetsLayout = new QVBoxLayout(assetsPage);
    assetsLayout->setContentsMargins(0, 6, 0, 0);
    assetsLayout->setSpacing(6);

    auto *assetList = new AssetListWidget(assetsPage);
    m_assets = assetList;
    m_assets->setViewMode(QListView::IconMode);
    m_assets->setIconSize(QSize(48, 48));
    m_assets->setGridSize(QSize(76, 78));
    m_assets->setMovement(QListView::Static);
    m_assets->setResizeMode(QListView::Adjust);
    m_assets->setWordWrap(true);
    m_assets->setUniformItemSizes(true);
    m_assets->setTextElideMode(Qt::ElideRight);
    m_assets->setContextMenuPolicy(Qt::CustomContextMenu);
    // QListView's Static movement resets the drag mode, so enable dragging
    // again after all view properties are applied.
    m_assets->setDragEnabled(true);
    m_assets->setDragDropMode(QAbstractItemView::DragOnly);
    m_assets->setDefaultDropAction(Qt::CopyAction);
    assetsLayout->addWidget(m_assets, 1);

    auto *assetButtons = new QHBoxLayout;
    assetButtons->setSpacing(4);
    auto *addSelection = makeButton(assetsPage, QStringLiteral("Из выделения"),
                                    QStringLiteral("Добавить выделенные объекты в библиотеку"));
    auto *importSvg = makeButton(assetsPage, QStringLiteral("Импорт SVG"),
                                 QStringLiteral("Добавить SVG-файл как ресурс"));
    auto *newCategory = makeButton(assetsPage, QStringLiteral("Категория"),
                                   QStringLiteral("Создать категорию"));
    assetButtons->addWidget(addSelection);
    assetButtons->addWidget(importSvg);
    assetButtons->addWidget(newCategory);
    assetsLayout->addLayout(assetButtons);

    // --- symbols page -----------------------------------------------------
    auto *symbolsPage = new QWidget(tabs);
    auto *symbolsLayout = new QVBoxLayout(symbolsPage);
    symbolsLayout->setContentsMargins(0, 6, 0, 0);
    symbolsLayout->setSpacing(6);

    m_symbols = new QListWidget(symbolsPage);
    m_symbols->setViewMode(QListView::IconMode);
    m_symbols->setIconSize(QSize(52, 52));
    m_symbols->setGridSize(QSize(82, 82));
    m_symbols->setMovement(QListView::Static);
    m_symbols->setResizeMode(QListView::Adjust);
    m_symbols->setWordWrap(true);
    m_symbols->setUniformItemSizes(true);
    m_symbols->setContextMenuPolicy(Qt::CustomContextMenu);
    symbolsLayout->addWidget(m_symbols, 1);

    auto *symbolButtons = new QHBoxLayout;
    symbolButtons->setSpacing(4);
    auto *createButton = makeButton(symbolsPage, QStringLiteral("Создать"),
                                    QStringLiteral("Создать символ из выделения"));
    auto *insertButton = makeButton(symbolsPage, QStringLiteral("Вставить"),
                                    QStringLiteral("Вставить экземпляр символа"));
    auto *updateButton = makeButton(symbolsPage, QStringLiteral("Обновить"),
                                    QStringLiteral("Обновить символ по выделенному экземпляру"));
    auto *detachButton = makeButton(symbolsPage, QStringLiteral("Отделить"),
                                    QStringLiteral("Отделить экземпляр от символа"));
    auto *renameButton = makeButton(symbolsPage, QStringLiteral("Имя"),
                                    QStringLiteral("Переименовать символ"));
    auto *deleteButton = makeButton(symbolsPage, QStringLiteral("Удалить"),
                                    QStringLiteral("Удалить символ из документа"));
    symbolButtons->addWidget(createButton);
    symbolButtons->addWidget(insertButton);
    symbolButtons->addWidget(updateButton);
    symbolButtons->addWidget(detachButton);
    symbolButtons->addWidget(renameButton);
    symbolButtons->addWidget(deleteButton);
    symbolsLayout->addLayout(symbolButtons);

    auto *patternsPage = new QWidget(tabs);
    auto *patternsLayout = new QVBoxLayout(patternsPage);
    patternsLayout->setContentsMargins(0, 6, 0, 0);
    patternsLayout->setSpacing(6);

    m_patterns = new QListWidget(patternsPage);
    m_patterns->setViewMode(QListView::IconMode);
    m_patterns->setIconSize(QSize(56, 56));
    m_patterns->setGridSize(QSize(88, 90));
    m_patterns->setMovement(QListView::Static);
    m_patterns->setResizeMode(QListView::Adjust);
    m_patterns->setWordWrap(true);
    m_patterns->setUniformItemSizes(true);
    m_patterns->setContextMenuPolicy(Qt::CustomContextMenu);
    patternsLayout->addWidget(m_patterns, 1);

    auto *patternButtons = new QHBoxLayout;
    patternButtons->setSpacing(4);
    auto *patternFromSelection = makeButton(
        patternsPage, QStringLiteral("Из выделения"),
        QStringLiteral("Создать узор из выделения и сохранить в библиотеку"));
    auto *patternApply = makeButton(patternsPage, QStringLiteral("Применить"),
                                    QStringLiteral("Залить выделение выбранным узором"));
    auto *patternImport = makeButton(patternsPage, QStringLiteral("Импорт"),
                                     QStringLiteral("Импортировать узор .vpat"));
    auto *patternExport = makeButton(patternsPage, QStringLiteral("Экспорт"),
                                     QStringLiteral("Экспортировать узор .vpat"));
    auto *patternRemove = makeButton(patternsPage, QStringLiteral("Удалить"),
                                     QStringLiteral("Удалить узор из библиотеки"));
    patternButtons->addWidget(patternFromSelection);
    patternButtons->addWidget(patternApply);
    patternButtons->addWidget(patternImport);
    patternButtons->addWidget(patternExport);
    patternButtons->addWidget(patternRemove);
    patternsLayout->addLayout(patternButtons);

    tabs->addTab(assetsPage, QStringLiteral("Ресурсы"));
    tabs->addTab(symbolsPage, QStringLiteral("Символы"));
    tabs->addTab(patternsPage, QStringLiteral("Узоры"));
    root->addWidget(tabs, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    m_status->setText(QStringLiteral(
        "Перетащите ресурс на холст или дважды щёлкните по нему. "
        "Двойной щелчок по символу вставляет экземпляр."));
    root->addWidget(m_status);

    // --- wiring -----------------------------------------------------------
    connect(m_libraries, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating)
            return;
        AssetStore::instance().setCurrentIndex(index);
        refresh();
    });
    connect(m_categories, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_updating)
            applyFilters();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) {
        applyFilters();
    });
    connect(m_favorites, &QCheckBox::toggled, this, [this](bool) {
        applyFilters();
    });

    connect(newLibraryButton, &QToolButton::clicked, this, &AssetPanel::newLibrary);
    connect(importLibraryButton, &QToolButton::clicked, this, &AssetPanel::importLibrary);
    connect(exportLibraryButton, &QToolButton::clicked, this, &AssetPanel::exportLibrary);
    connect(undoButton, &QToolButton::clicked, this, [this]() {
        if (AssetStore::instance().undo())
            m_status->setText(QStringLiteral("Изменение библиотеки отменено."));
        else
            m_status->setText(QStringLiteral("Нет действий для отмены."));
    });
    connect(redoButton, &QToolButton::clicked, this, [this]() {
        if (AssetStore::instance().redo())
            m_status->setText(QStringLiteral("Изменение библиотеки возвращено."));
        else
            m_status->setText(QStringLiteral("Нет действий для повтора."));
    });

    connect(addSelection, &QToolButton::clicked, this, &AssetPanel::addSelectionToLibrary);
    connect(importSvg, &QToolButton::clicked, this, &AssetPanel::importSvgAsset);
    connect(newCategory, &QToolButton::clicked, this, &AssetPanel::newCategory);

    connect(m_assets, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        insertAsset(item->data(AssetListWidget::kSvgRole).toString(), item->text());
    });
    connect(m_assets, &QListWidget::customContextMenuRequested,
            this, &AssetPanel::showAssetMenu);

    connect(createButton, &QToolButton::clicked, this, &AssetPanel::createSymbol);
    connect(insertButton, &QToolButton::clicked, this, &AssetPanel::insertCurrentSymbol);
    connect(updateButton, &QToolButton::clicked, this, &AssetPanel::updateSymbol);
    connect(detachButton, &QToolButton::clicked, this, &AssetPanel::detachSymbol);
    connect(renameButton, &QToolButton::clicked, this, &AssetPanel::renameSymbol);
    connect(deleteButton, &QToolButton::clicked, this, &AssetPanel::deleteSymbol);
    connect(m_symbols, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        insertCurrentSymbol();
    });
    connect(m_symbols, &QListWidget::customContextMenuRequested,
            this, &AssetPanel::showSymbolMenu);

    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1)
            refreshSymbols();
        else if (index == 2)
            refreshPatterns();
        else
            applyFilters();
    });
    connect(&PatternStore::instance(), &PatternStore::changed,
            this, &AssetPanel::refreshPatterns);
    connect(m_patterns, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) {
                PatternStore &store = PatternStore::instance();
                QListWidgetItem *item = m_patterns->currentItem();
                if (!item || !m_view)
                    return;
                PatternDefinition pattern = store.pattern(item->data(Qt::UserRole).toInt());
                pattern.id = 0;
                if (m_view->applyPatternDefinitionToSelection(pattern))
                    m_status->setText(QStringLiteral("Узор «%1» применён.").arg(pattern.name));
            });
    connect(patternFromSelection, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        if (m_view->selectedVectorItems().isEmpty()) {
            m_status->setText(QStringLiteral("Выделите объекты для узора."));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Новый узор"), QStringLiteral("Название узора:"),
            QLineEdit::Normal,
            QStringLiteral("Узор %1").arg(PatternStore::instance().count() + 1), &ok);
        if (!ok)
            return;
        const int id = m_view->createPatternFromSelection(name);
        if (id <= 0)
            return;
        PatternDefinition pattern = m_view->patternDefinition(id);
        pattern.id = 0;
        PatternStore::instance().add(pattern);
        refreshPatterns();
    });
    connect(patternApply, &QToolButton::clicked, this, [this]() {
        QListWidgetItem *item = m_patterns->currentItem();
        if (!item || !m_view)
            return;
        PatternDefinition pattern =
            PatternStore::instance().pattern(item->data(Qt::UserRole).toInt());
        pattern.id = 0;
        if (m_view->applyPatternDefinitionToSelection(pattern))
            m_status->setText(QStringLiteral("Узор «%1» применён.").arg(pattern.name));
    });
    connect(patternImport, &QToolButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Импорт узора"), QString(),
            QStringLiteral("Узор Vectora (*.vpat *.json)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!PatternStore::instance().importPattern(path, &error))
            QMessageBox::warning(this, QStringLiteral("Импорт узора"), error);
        else
            m_status->setText(QStringLiteral("Узор импортирован."));
    });
    connect(patternExport, &QToolButton::clicked, this, [this]() {
        QListWidgetItem *item = m_patterns->currentItem();
        if (!item)
            return;
        const int index = item->data(Qt::UserRole).toInt();
        QString suggested = PatternStore::instance().pattern(index).name;
        suggested.replace(QLatin1Char(' '), QLatin1Char('-'));
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Экспорт узора"),
            suggested + QStringLiteral(".vpat"),
            QStringLiteral("Узор Vectora (*.vpat)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!PatternStore::instance().exportPattern(index, path, &error))
            QMessageBox::warning(this, QStringLiteral("Экспорт узора"), error);
        else
            m_status->setText(QStringLiteral("Узор сохранён."));
    });
    connect(patternRemove, &QToolButton::clicked, this, [this]() {
        QListWidgetItem *item = m_patterns->currentItem();
        if (!item)
            return;
        const int index = item->data(Qt::UserRole).toInt();
        if (QMessageBox::question(
                this, QStringLiteral("Удалить узор"),
                QStringLiteral("Удалить «%1» из библиотеки?")
                    .arg(PatternStore::instance().pattern(index).name))
            != QMessageBox::Yes)
            return;
        PatternStore::instance().remove(index);
    });
}

void AssetPanel::refresh()
{
    AssetStore &store = AssetStore::instance();
    m_updating = true;

    const int current = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    const QString previousCategory = m_categories->currentData().toString();
    m_libraries->clear();
    for (int i = 0; i < store.count(); ++i)
        m_libraries->addItem(store.library(i).name, i);
    if (m_libraries->count() > 0)
        m_libraries->setCurrentIndex(current);

    m_categories->clear();
    m_categories->addItem(QStringLiteral("Все категории"), QString());
    for (const QString &category : store.categories(current))
        m_categories->addItem(category, category);
    const int previousIndex = m_categories->findData(previousCategory);
    if (previousIndex > 0)
        m_categories->setCurrentIndex(previousIndex);

    m_updating = false;
    applyFilters();
    refreshSymbols();
    refreshPatterns();
}

void AssetPanel::applyFilters()
{
    if (!m_assets)
        return;
    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    const QString search = m_search->text().trimmed();
    const QString category = m_categories->currentData().toString();
    const bool onlyFavorites = m_favorites->isChecked();

    m_assets->clear();
    const AssetLibrary &source = store.library(library);
    for (int i = 0; i < source.assets.size(); ++i) {
        const Asset &asset = source.assets.at(i);
        if (onlyFavorites && !asset.favorite)
            continue;
        if (!category.isEmpty() && asset.category != category)
            continue;
        if (!search.isEmpty()
            && !asset.name.contains(search, Qt::CaseInsensitive))
            continue;

        auto *item = new QListWidgetItem(m_assets);
        item->setText(asset.favorite ? QStringLiteral("★ %1").arg(asset.name)
                                     : asset.name);
        if (!m_iconCache.contains(asset.id)) {
            m_iconCache.insert(
                asset.id,
                QIcon(QPixmap::fromImage(
                    AssetStore::renderPreview(asset.svg, QSize(48, 48)))));
        }
        item->setIcon(m_iconCache.value(asset.id));
        item->setToolTip(QStringLiteral("%1\nКатегория: %2")
                             .arg(asset.name, asset.category));
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled
                       | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setData(AssetListWidget::kSvgRole, asset.svg);
        item->setData(AssetListWidget::kLibraryRole, library);
        item->setData(AssetListWidget::kIndexRole, i);
    }

    m_status->setText(QStringLiteral("Ресурсов: %1 · перетащите на холст или дважды щёлкните.")
                          .arg(m_assets->count()));
}

void AssetPanel::addSelectionToLibrary()
{
    if (!m_view)
        return;
    const QString svg = m_view->selectionToSvg();
    if (svg.isEmpty()) {
        m_status->setText(QStringLiteral("Выделите объекты для добавления в библиотеку."));
        return;
    }

    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новый ресурс"), QStringLiteral("Название:"),
        QLineEdit::Normal,
        store.uniqueAssetName(library, QStringLiteral("Ресурс")), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString category = promptCategory();
    if (category.isEmpty())
        return;

    Asset asset;
    asset.name = name.trimmed();
    asset.category = category;
    asset.svg = svg;
    if (store.addAsset(library, asset))
        m_status->setText(QStringLiteral("Ресурс «%1» добавлен в библиотеку.")
                              .arg(asset.name));
}

void AssetPanel::importSvgAsset()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Импорт SVG как ресурса"), QString(),
        QStringLiteral("SVG (*.svg)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_status->setText(QStringLiteral("Не удалось открыть файл."));
        return;
    }
    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    Asset asset;
    asset.name = QFileInfo(path).completeBaseName();
    const QString category = promptCategory();
    if (category.isEmpty())
        return;
    asset.category = category;
    asset.svg = QString::fromUtf8(file.readAll());
    if (store.addAsset(library, asset))
        m_status->setText(QStringLiteral("Ресурс «%1» импортирован.").arg(asset.name));
}

QString AssetPanel::promptCategory() const
{
    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    QStringList categories = store.categories(library);
    if (categories.isEmpty())
        categories.append(QStringLiteral("Пользовательские"));
    bool ok = false;
    const QString category = QInputDialog::getItem(
        const_cast<AssetPanel *>(this), QStringLiteral("Категория ресурса"),
        QStringLiteral("Категория:"), categories,
        categories.size() - 1, true, &ok);
    if (!ok || category.trimmed().isEmpty())
        return QString();
    store.addCategory(library, category.trimmed());
    return category.trimmed();
}

void AssetPanel::insertAsset(const QString &svg, const QString &name)
{
    if (!m_view || svg.isEmpty())
        return;
    const QPointF centre(m_view->pageSize().width() / 2.0,
                         m_view->pageSize().height() / 2.0);
    if (m_view->insertSvgAsset(svg, centre) > 0)
        m_status->setText(QStringLiteral("Вставлен ресурс «%1».").arg(name));
}

void AssetPanel::newLibrary()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новая библиотека"), QStringLiteral("Название:"),
        QLineEdit::Normal, QStringLiteral("Моя библиотека"), &ok);
    if (ok && !name.trimmed().isEmpty())
        AssetStore::instance().addLibrary(name.trimmed());
}

void AssetPanel::importLibrary()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Импорт библиотеки"), QString(),
        QStringLiteral("Библиотека Vectora (*.vlib *.json)"));
    if (path.isEmpty())
        return;
    QString error;
    if (AssetStore::instance().importLibrary(path, &error) < 0) {
        QMessageBox::warning(this, QStringLiteral("Импорт библиотеки"), error);
        return;
    }
    m_status->setText(QStringLiteral("Библиотека импортирована."));
}

void AssetPanel::exportLibrary()
{
    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    QString suggested = store.library(library).name;
    suggested.replace(QLatin1Char(' '), QLatin1Char('-'));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Экспорт библиотеки"),
        suggested + QStringLiteral(".vlib"),
        QStringLiteral("Библиотека Vectora (*.vlib)"));
    if (path.isEmpty())
        return;
    QString error;
    if (store.exportLibrary(library, path, &error))
        m_status->setText(QStringLiteral("Библиотека сохранена."));
    else
        QMessageBox::warning(this, QStringLiteral("Экспорт библиотеки"), error);
}

void AssetPanel::newCategory()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новая категория"), QStringLiteral("Название категории:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    AssetStore &store = AssetStore::instance();
    const int library = qBound(0, store.currentIndex(), qMax(0, store.count() - 1));
    store.addCategory(library, name.trimmed());
    m_status->setText(QStringLiteral("Категория «%1» создана.").arg(name.trimmed()));
}

void AssetPanel::showAssetMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_assets->itemAt(pos);
    if (!item)
        return;
    const int library = item->data(AssetListWidget::kLibraryRole).toInt();
    const int index = item->data(AssetListWidget::kIndexRole).toInt();
    const QString svg = item->data(AssetListWidget::kSvgRole).toString();
    const QString name = item->text();

    AssetStore &store = AssetStore::instance();
    const Asset *asset = store.assetAt(library, index);
    if (!asset)
        return;

    QMenu menu(this);
    QAction *insert = menu.addAction(QStringLiteral("Вставить на страницу"));
    QAction *favorite = menu.addAction(asset->favorite
                                          ? QStringLiteral("Убрать из избранного")
                                          : QStringLiteral("В избранное"));
    menu.addSeparator();
    QAction *rename = menu.addAction(QStringLiteral("Переименовать…"));
    QAction *recategorize = menu.addAction(QStringLiteral("Сменить категорию…"));
    QMenu *copyMenu = menu.addMenu(QStringLiteral("Копировать в библиотеку"));
    QVector<int> copyTargets;
    for (int i = 0; i < store.count(); ++i) {
        if (i == library)
            continue;
        copyTargets.append(i);
        copyMenu->addAction(store.library(i).name)->setData(i);
    }
    copyMenu->setEnabled(!copyTargets.isEmpty());
    menu.addSeparator();
    QAction *remove = menu.addAction(QStringLiteral("Удалить ресурс"));

    QAction *chosen = menu.exec(m_assets->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == insert) {
        insertAsset(svg, name);
    } else if (chosen == favorite) {
        store.toggleFavorite(library, index);
    } else if (chosen == rename) {
        bool ok = false;
        const QString value = QInputDialog::getText(
            this, QStringLiteral("Переименовать ресурс"), QStringLiteral("Название:"),
            QLineEdit::Normal, asset->name, &ok);
        if (ok && !value.trimmed().isEmpty())
            store.renameAsset(library, index, value.trimmed());
    } else if (chosen == recategorize) {
        QStringList categories = store.categories(library);
        bool ok = false;
        const QString value = QInputDialog::getItem(
            this, QStringLiteral("Категория"), QStringLiteral("Категория:"),
            categories, categories.indexOf(asset->category), true, &ok);
        if (ok && !value.trimmed().isEmpty())
            store.setAssetCategory(library, index, value.trimmed());
    } else if (chosen == remove) {
        if (QMessageBox::question(this, QStringLiteral("Удалить ресурс"),
                                  QStringLiteral("Удалить «%1» из библиотеки?").arg(name))
            == QMessageBox::Yes) {
            store.removeAsset(library, index);
            m_status->setText(QStringLiteral("Ресурс «%1» удалён.").arg(name));
        }
    } else if (chosen->parent() == copyMenu) {
        const int target = chosen->data().toInt();
        if (store.copyAsset(library, index, target))
            m_status->setText(QStringLiteral("Ресурс скопирован в «%1».")
                                  .arg(store.library(target).name));
    }
}

void AssetPanel::showSymbolMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_symbols->itemAt(pos);
    if (!item)
        return;
    m_symbols->setCurrentItem(item);

    QMenu menu(this);
    QAction *insert = menu.addAction(QStringLiteral("Вставить экземпляр"));
    QAction *update = menu.addAction(QStringLiteral("Обновить по экземпляру"));
    menu.addSeparator();
    QAction *rename = menu.addAction(QStringLiteral("Переименовать…"));
    QAction *remove = menu.addAction(QStringLiteral("Удалить символ"));

    QAction *chosen = menu.exec(m_symbols->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == insert)
        insertCurrentSymbol();
    else if (chosen == update)
        updateSymbol();
    else if (chosen == rename)
        renameSymbol();
    else if (chosen == remove)
        deleteSymbol();
}

void AssetPanel::refreshSymbols()
{
    if (!m_symbols || !m_view)
        return;
    const int selectedId = m_symbols->currentItem()
                               ? m_symbols->currentItem()->data(Qt::UserRole).toInt()
                               : 0;
    m_symbols->clear();
    const QVector<SymbolDefinition> symbols = m_view->symbols();
    const QVector<SymbolInstance> instances = m_view->instances();
    for (const SymbolDefinition &symbol : symbols) {
        int placed = 0;
        for (const SymbolInstance &instance : instances) {
            if (instance.symbolId == symbol.id)
                ++placed;
        }
        auto *item = new QListWidgetItem(m_symbols);
        item->setText(QStringLiteral("%1\n%2 экз.").arg(symbol.name).arg(placed));
        item->setToolTip(QStringLiteral("%1 · объектов: %2 · экземпляров: %3")
                             .arg(symbol.name)
                             .arg(symbol.objects.size())
                             .arg(placed));
        item->setIcon(QIcon(QPixmap::fromImage(
            m_view->renderSymbolImage(symbol.id, QSize(52, 52)))));
        item->setData(Qt::UserRole, symbol.id);
        if (symbol.id == selectedId)
            m_symbols->setCurrentItem(item);
    }
    if (m_tabs && m_tabs->currentIndex() == 1) {
        m_status->setText(symbols.isEmpty()
                              ? QStringLiteral("Символов пока нет: выделите объекты "
                                               "и нажмите «Создать».")
                              : QStringLiteral("Символов: %1 · двойной щелчок "
                                               "вставляет экземпляр.")
                                    .arg(symbols.size()));
    }
}

void AssetPanel::showAssetsTab()
{
    if (m_tabs)
        m_tabs->setCurrentIndex(0);
}

void AssetPanel::showSymbolsTab()
{
    if (m_tabs)
        m_tabs->setCurrentIndex(1);
}

void AssetPanel::showPatternsTab()
{
    if (m_tabs)
        m_tabs->setCurrentIndex(2);
}

void AssetPanel::refreshPatterns()
{
    if (!m_patterns)
        return;
    PatternStore &store = PatternStore::instance();
    store.load();
    const int selected = m_patterns->currentItem()
                             ? m_patterns->currentItem()->data(Qt::UserRole).toInt()
                             : -1;
    m_patterns->clear();
    for (int i = 0; i < store.count(); ++i) {
        const PatternDefinition &pattern = store.pattern(i);
        auto *item = new QListWidgetItem(m_patterns);
        item->setText(QStringLiteral("%1\n%2").arg(
            pattern.name, patternTypeName(pattern.settings.type)));
        item->setToolTip(QStringLiteral("%1 · %2 · ячейка %3×%4")
                             .arg(pattern.name,
                                  patternTypeName(pattern.settings.type))
                             .arg(pattern.settings.tile.width(), 0, 'f', 0)
                             .arg(pattern.settings.tile.height(), 0, 'f', 0));
        const QVector<PatternShape> shapes = bakePatternShapes(pattern);
        item->setIcon(QIcon(QPixmap::fromImage(
            renderPatternPreview(pattern.settings, shapes, QSize(56, 56)))));
        item->setData(Qt::UserRole, i);
        if (i == selected)
            m_patterns->setCurrentItem(item);
    }
    if (m_tabs && m_tabs->currentIndex() == 2) {
        m_status->setText(store.count() == 0
                              ? QStringLiteral("Узоров нет: выделите объекты и "
                                               "нажмите «Из выделения».")
                              : QStringLiteral("Узоров: %1 · двойной щелчок "
                                               "применяет к выделению.")
                                    .arg(store.count()));
    }
}

void AssetPanel::createSymbol()
{
    if (!m_view)
        return;
    if (m_view->selectedVectorItems().isEmpty()) {
        m_status->setText(QStringLiteral("Выделите объекты для символа."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новый символ"), QStringLiteral("Название символа:"),
        QLineEdit::Normal,
        QStringLiteral("Символ %1").arg(m_view->symbols().size() + 1), &ok);
    if (!ok)
        return;
    if (m_view->createSymbolFromSelection(name) > 0) {
        refreshSymbols();
        m_status->setText(QStringLiteral("Символ создан из выделения."));
    }
}

void AssetPanel::insertCurrentSymbol()
{
    if (!m_view)
        return;
    QListWidgetItem *item = m_symbols->currentItem();
    if (!item) {
        m_status->setText(QStringLiteral("Выберите символ в списке."));
        return;
    }
    if (m_view->insertSymbol(item->data(Qt::UserRole).toInt()) > 0)
        refreshSymbols();
}

void AssetPanel::updateSymbol()
{
    if (!m_view)
        return;
    if (m_view->updateSymbolFromSelection()) {
        refreshSymbols();
        m_status->setText(QStringLiteral(
            "Символ обновлён; остальные экземпляры пересобраны."));
    }
}

void AssetPanel::detachSymbol()
{
    if (!m_view)
        return;
    m_view->detachSelectedSymbolInstances();
    refreshSymbols();
}

void AssetPanel::renameSymbol()
{
    if (!m_view)
        return;
    QListWidgetItem *item = m_symbols->currentItem();
    if (!item)
        return;
    bool ok = false;
    const QString value = QInputDialog::getText(
        this, QStringLiteral("Переименовать символ"), QStringLiteral("Название:"),
        QLineEdit::Normal, item->text().section(QLatin1Char('\n'), 0, 0), &ok);
    if (!ok || value.trimmed().isEmpty())
        return;
    m_view->renameSymbol(item->data(Qt::UserRole).toInt(), value.trimmed());
    refreshSymbols();
}

void AssetPanel::deleteSymbol()
{
    if (!m_view)
        return;
    QListWidgetItem *item = m_symbols->currentItem();
    if (!item)
        return;
    if (QMessageBox::question(
            this, QStringLiteral("Удалить символ"),
            QStringLiteral("Удалить «%1»? Экземпляры останутся обычными объектами.")
                .arg(item->text().section(QLatin1Char('\n'), 0, 0)))
        != QMessageBox::Yes)
        return;
    m_view->deleteSymbol(item->data(Qt::UserRole).toInt());
    refreshSymbols();
}
