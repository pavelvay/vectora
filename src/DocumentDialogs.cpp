#include "DocumentDialogs.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QListView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "CanvasView.h"
#include "Document.h"
#include "TemplateStore.h"
#include "VectorItem.h"

namespace {

QString objectTypeName(const VectorObject &object)
{
    switch (object.type) {
    case ShapeType::Rectangle: return QStringLiteral("Прямоугольник");
    case ShapeType::Ellipse: return QStringLiteral("Эллипс");
    case ShapeType::Polygon: return QStringLiteral("Многоугольник");
    case ShapeType::Star: return QStringLiteral("Звезда");
    case ShapeType::Spiral: return QStringLiteral("Спираль");
    case ShapeType::Line: return QStringLiteral("Линия");
    case ShapeType::Bezier: return QStringLiteral("Кривая");
    case ShapeType::Freehand: return QStringLiteral("Свободная форма");
    case ShapeType::Artistic: return QStringLiteral("Мазок");
    case ShapeType::Text: return QStringLiteral("Текст");
    case ShapeType::Image: return QStringLiteral("Изображение");
    case ShapeType::Dimension: return QStringLiteral("Размер");
    case ShapeType::Code: return QStringLiteral("Код");
    }
    return QStringLiteral("Объект");
}

QString objectLabel(const VectorObject &object)
{
    if (!object.name.trimmed().isEmpty())
        return object.name.trimmed();
    if (object.type == ShapeType::Text) {
        const QString first = object.text.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!first.isEmpty())
            return QStringLiteral("Текст: %1").arg(first.left(36));
    }
    return objectTypeName(object);
}

QImage renderTemplate(const QJsonObject &snapshot, const QSize &target)
{
    QString error;
    Document document = Document::fromJson(snapshot, &error);
    document.currentPage = 0;
    const QSizeF page = document.pageSize;
    if (page.width() < 1.0 || page.height() < 1.0 || target.isEmpty())
        return QImage();
    const qreal scale = qMin(target.width() / page.width(),
                             target.height() / page.height());
    const QSize size(qMax(1, qRound(page.width() * scale)),
                     qMax(1, qRound(page.height() * scale)));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(scale, scale);
    document.render(&painter);
    painter.end();
    return image;
}

} // namespace

HistoryDialog::HistoryDialog(const QStringList &labels,
                             const QVector<QDateTime> &times, int currentIndex,
                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("История изменений"));
    resize(560, 460);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral(
        "Выберите состояние, чтобы вернуться к нему. Более новые состояния "
        "останутся доступны через «Повторить»."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    layout->addWidget(hint);

    auto *list = new QListWidget(this);
    for (int index = currentIndex; index >= 0; --index) {
        QString label;
        QDateTime time;
        if (index == currentIndex) {
            label = QStringLiteral("Текущее состояние");
            time = QDateTime::currentDateTime();
        } else {
            label = labels.value(index, QStringLiteral("Действие %1").arg(index + 1));
            time = times.value(index);
        }
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2")
                .arg(time.isValid() ? time.toString(QStringLiteral("hh:mm:ss"))
                                    : QStringLiteral("—"),
                     label),
            list);
        item->setData(Qt::UserRole, index);
        if (index == currentIndex)
            item->setSelected(true);
    }
    layout->addWidget(list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *restore = buttons->addButton(QStringLiteral("Восстановить"),
                                       QDialogButtonBox::AcceptRole);
    restore->setEnabled(false);
    layout->addWidget(buttons);

    connect(list, &QListWidget::itemSelectionChanged, this, [this, list, restore]() {
        const QListWidgetItem *item = list->currentItem();
        restore->setEnabled(item && item->data(Qt::UserRole).toInt() != -1);
    });
    connect(restore, &QPushButton::clicked, this, [this, list]() {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        m_selected = item->data(Qt::UserRole).toInt();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

VersionDialog::VersionDialog(QVector<DocumentVersion> *versions,
                             const QJsonObject &currentSnapshot,
                             const QString &currentName, QWidget *parent)
    : QDialog(parent)
    , m_versions(versions)
    , m_current(currentSnapshot)
    , m_currentName(currentName)
{
    setWindowTitle(QStringLiteral("Версии документа"));
    resize(780, 520);

    auto *root = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    columns->addWidget(m_list, 2);

    m_details = new QTextBrowser(this);
    columns->addWidget(m_details, 3);
    root->addLayout(columns, 1);

    auto *buttons = new QHBoxLayout;
    auto *create = new QPushButton(QStringLiteral("Создать версию…"), this);
    auto *restore = new QPushButton(QStringLiteral("Восстановить"), this);
    auto *remove = new QPushButton(QStringLiteral("Удалить"), this);
    auto *compare = new QPushButton(QStringLiteral("Сравнить"), this);
    buttons->addWidget(create);
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addWidget(compare);
    buttons->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Закрыть"), this);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(create, &QPushButton::clicked, this, &VersionDialog::createVersion);
    connect(restore, &QPushButton::clicked, this, &VersionDialog::restoreVersion);
    connect(remove, &QPushButton::clicked, this, &VersionDialog::deleteVersion);
    connect(compare, &QPushButton::clicked, this, &VersionDialog::compareVersions);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        if (selected.isEmpty()) {
            m_details->setPlainText(QStringLiteral("Выберите версию."));
            return;
        }
        const int index = selected.first()->data(Qt::UserRole).toInt();
        if (index < 0 || index >= m_versions->size())
            return;
        const DocumentVersion &version = m_versions->at(index);
        m_details->setPlainText(QStringLiteral("%1\n%2\n\n%3")
                                    .arg(version.name,
                                         version.created.toString(
                                             QStringLiteral("dd.MM.yyyy hh:mm")),
                                         version.description.isEmpty()
                                             ? QStringLiteral("Без описания.")
                                             : version.description));
    });
    refresh();
}

void VersionDialog::refresh()
{
    m_list->clear();
    for (int i = m_versions->size() - 1; i >= 0; --i) {
        const DocumentVersion &version = m_versions->at(i);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(version.name,
                     version.created.toString(QStringLiteral("dd.MM.yyyy hh:mm"))),
            m_list);
        item->setData(Qt::UserRole, i);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        m_details->setPlainText(QStringLiteral("Версий пока нет."));
}

void VersionDialog::createVersion()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новая версия"), QStringLiteral("Название версии:"),
        QLineEdit::Normal,
        QStringLiteral("Версия %1").arg(m_versions->size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const QString description = QInputDialog::getMultiLineText(
        this, QStringLiteral("Описание версии"),
        QStringLiteral("Что изменилось в этой версии?"), QString(), &ok);
    if (!ok)
        return;
    DocumentVersion version;
    version.name = name.trimmed();
    version.description = description.trimmed();
    version.created = QDateTime::currentDateTime();
    version.snapshot = m_current;
    m_versions->append(version);
    while (m_versions->size() > 50)
        m_versions->removeFirst();
    m_changed = true;
    refresh();
}

void VersionDialog::restoreVersion()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty())
        return;
    const int index = selected.first()->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_versions->size())
        return;
    const DocumentVersion &version = m_versions->at(index);
    if (QMessageBox::question(
            this, QStringLiteral("Восстановить версию"),
            QStringLiteral("Восстановить «%1»? Текущее состояние будет заменено.")
                .arg(version.name))
        != QMessageBox::Yes)
        return;
    m_restoreSnapshot = version.snapshot;
    m_restore = true;
    accept();
}

void VersionDialog::deleteVersion()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty())
        return;
    const int index = selected.first()->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_versions->size())
        return;
    if (QMessageBox::question(this, QStringLiteral("Удалить версию"),
                              QStringLiteral("Удалить «%1»?")
                                  .arg(m_versions->at(index).name))
        != QMessageBox::Yes)
        return;
    m_versions->removeAt(index);
    m_changed = true;
    refresh();
}

void VersionDialog::compareVersions()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty()) {
        m_details->setPlainText(compareDocuments(m_current, m_current,
                                                 m_currentName,
                                                 QStringLiteral("текущее")));
        return;
    }
    if (selected.size() >= 2) {
        const int first = selected.at(0)->data(Qt::UserRole).toInt();
        const int second = selected.at(1)->data(Qt::UserRole).toInt();
        if (first < 0 || second < 0 || first >= m_versions->size()
            || second >= m_versions->size())
            return;
        m_details->setPlainText(compareDocuments(m_versions->at(first).snapshot,
                                                 m_versions->at(second).snapshot,
                                                 m_versions->at(first).name,
                                                 m_versions->at(second).name));
        return;
    }
    const int index = selected.first()->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_versions->size())
        return;
    m_details->setPlainText(compareDocuments(m_versions->at(index).snapshot,
                                             m_current, m_versions->at(index).name,
                                             QStringLiteral("текущее")));
}

TemplateDialog::TemplateDialog(const QJsonObject &currentSnapshot, QWidget *parent)
    : QDialog(parent)
    , m_current(currentSnapshot)
{
    setWindowTitle(QStringLiteral("Шаблоны документов"));
    resize(940, 620);
    TemplateStore::instance().load();

    auto *root = new QVBoxLayout(this);

    auto *filters = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Поиск шаблонов…"));
    m_search->setClearButtonEnabled(true);
    filters->addWidget(m_search, 1);
    m_category = new QComboBox(this);
    m_category->addItem(QStringLiteral("Все категории"), QString());
    for (const QString &category : TemplateStore::instance().categories())
        m_category->addItem(category, category);
    filters->addWidget(m_category);
    m_favorites = new QCheckBox(QStringLiteral("Избранные"), this);
    filters->addWidget(m_favorites);
    root->addLayout(filters);

    auto *content = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(120, 84));
    m_list->setGridSize(QSize(150, 130));
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWordWrap(true);
    content->addWidget(m_list, 1);

    auto *side = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(300, 330);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#1b1d21; border:1px solid #34383f;"));
    side->addWidget(m_preview);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    side->addWidget(m_summary);
    content->addLayout(side);
    root->addLayout(content, 1);

    auto *buttons = new QHBoxLayout;
    auto *create = new QPushButton(QStringLiteral("Создать документ"), this);
    auto *saveCurrent = new QPushButton(QStringLiteral("Сохранить текущий…"), this);
    auto *favorite = new QPushButton(QStringLiteral("В избранное"), this);
    auto *remove = new QPushButton(QStringLiteral("Удалить"), this);
    auto *import = new QPushButton(QStringLiteral("Импорт…"), this);
    auto *exportButton = new QPushButton(QStringLiteral("Экспорт…"), this);
    buttons->addWidget(create);
    buttons->addWidget(saveCurrent);
    buttons->addWidget(favorite);
    buttons->addWidget(remove);
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    buttons->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Закрыть"), this);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { refresh(); });
    connect(m_category, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_favorites, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { updatePreview(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { createFromTemplate(); });
    connect(create, &QPushButton::clicked, this, &TemplateDialog::createFromTemplate);
    connect(saveCurrent, &QPushButton::clicked, this, &TemplateDialog::saveCurrent);
    connect(favorite, &QPushButton::clicked, this, &TemplateDialog::toggleFavorite);
    connect(remove, &QPushButton::clicked, this, &TemplateDialog::removeTemplate);
    connect(import, &QPushButton::clicked, this, &TemplateDialog::importTemplate);
    connect(exportButton, &QPushButton::clicked, this, &TemplateDialog::exportTemplate);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    refresh();
}

void TemplateDialog::refresh()
{
    TemplateStore &store = TemplateStore::instance();
    const QString search = m_search->text().trimmed();
    const QString category = m_category->currentData().toString();
    const bool onlyFavorites = m_favorites->isChecked();
    const QString selectedId = m_list->currentItem()
                                   ? m_list->currentItem()->data(Qt::UserRole).toString()
                                   : QString();
    m_list->clear();
    for (int i = 0; i < store.count(); ++i) {
        const DocumentTemplate &documentTemplate = store.templates().at(i);
        if (onlyFavorites && !documentTemplate.favorite)
            continue;
        if (!category.isEmpty() && documentTemplate.category != category)
            continue;
        if (!search.isEmpty()
            && !documentTemplate.name.contains(search, Qt::CaseInsensitive)
            && !documentTemplate.description.contains(search, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(m_list);
        item->setText(QStringLiteral("%1\n%2")
                          .arg(documentTemplate.name, documentTemplate.category));
        item->setToolTip(documentTemplate.description);
        const QImage preview = renderTemplate(documentTemplate.document, QSize(120, 84));
        item->setIcon(QIcon(QPixmap::fromImage(preview)));
        item->setData(Qt::UserRole, documentTemplate.id);
        if (documentTemplate.id == selectedId)
            m_list->setCurrentItem(item);
    }
    m_summary->setText(QStringLiteral("Шаблонов: %1").arg(m_list->count()));
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    updatePreview();
}

void TemplateDialog::updatePreview()
{
    QListWidgetItem *item = m_list->currentItem();
    TemplateStore &store = TemplateStore::instance();
    const int index = item ? store.indexOf(item->data(Qt::UserRole).toString()) : -1;
    if (index < 0) {
        m_preview->setPixmap(QPixmap());
        return;
    }
    const DocumentTemplate &documentTemplate = store.templates().at(index);
    const QImage image = renderTemplate(documentTemplate.document, QSize(296, 326));
    m_preview->setPixmap(QPixmap::fromImage(image));
    m_summary->setText(QStringLiteral("%1\n%2\n\n%3")
                           .arg(documentTemplate.name, documentTemplate.category,
                                documentTemplate.description));
}

void TemplateDialog::createFromTemplate()
{
    QListWidgetItem *item = m_list->currentItem();
    TemplateStore &store = TemplateStore::instance();
    const int index = item ? store.indexOf(item->data(Qt::UserRole).toString()) : -1;
    if (index < 0)
        return;
    m_selected = store.templates().at(index).document;
    accept();
}

void TemplateDialog::saveCurrent()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Шаблон из документа"), QStringLiteral("Название шаблона:"),
        QLineEdit::Normal, QStringLiteral("Мой шаблон"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QStringList categories = TemplateStore::instance().categories();
    const QString category = QInputDialog::getItem(
        this, QStringLiteral("Категория"), QStringLiteral("Категория шаблона:"),
        categories, categories.size() - 1, true, &ok);
    if (!ok || category.trimmed().isEmpty())
        return;
    const QString description = QInputDialog::getMultiLineText(
        this, QStringLiteral("Описание"), QStringLiteral("Краткое описание:"),
        QString(), &ok);
    if (!ok)
        return;
    DocumentTemplate documentTemplate;
    documentTemplate.name = name.trimmed();
    documentTemplate.category = category.trimmed();
    documentTemplate.description = description.trimmed();
    documentTemplate.document = m_current;
    if (TemplateStore::instance().add(documentTemplate) >= 0) {
        if (!m_category->findData(documentTemplate.category))
            m_category->addItem(documentTemplate.category, documentTemplate.category);
        refresh();
    }
}

void TemplateDialog::removeTemplate()
{
    QListWidgetItem *item = m_list->currentItem();
    TemplateStore &store = TemplateStore::instance();
    const int index = item ? store.indexOf(item->data(Qt::UserRole).toString()) : -1;
    if (index < 0)
        return;
    if (store.templates().at(index).builtIn) {
        QMessageBox::information(this, QStringLiteral("Шаблоны"),
                                 QStringLiteral("Встроенный шаблон нельзя удалить."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Удалить шаблон"),
                              QStringLiteral("Удалить «%1»?")
                                  .arg(store.templates().at(index).name))
        == QMessageBox::Yes) {
        store.remove(index);
        refresh();
    }
}

void TemplateDialog::toggleFavorite()
{
    QListWidgetItem *item = m_list->currentItem();
    TemplateStore &store = TemplateStore::instance();
    const int index = item ? store.indexOf(item->data(Qt::UserRole).toString()) : -1;
    if (index >= 0) {
        store.toggleFavorite(index);
        refresh();
    }
}

void TemplateDialog::importTemplate()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Импорт шаблона"), QString(),
        QStringLiteral("Шаблон Vectora (*.vtpl *.json)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!TemplateStore::instance().importTemplate(path, &error))
        QMessageBox::warning(this, QStringLiteral("Импорт шаблона"), error);
    else
        refresh();
}

void TemplateDialog::exportTemplate()
{
    QListWidgetItem *item = m_list->currentItem();
    TemplateStore &store = TemplateStore::instance();
    const int index = item ? store.indexOf(item->data(Qt::UserRole).toString()) : -1;
    if (index < 0)
        return;
    QString suggested = store.templates().at(index).name;
    suggested.replace(QLatin1Char(' '), QLatin1Char('-'));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Экспорт шаблона"),
        suggested + QStringLiteral(".vtpl"),
        QStringLiteral("Шаблон Vectora (*.vtpl)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!TemplateStore::instance().exportTemplate(index, path, &error))
        QMessageBox::warning(this, QStringLiteral("Экспорт шаблона"), error);
}

ResourceSearchDialog::ResourceSearchDialog(CanvasView *view, QWidget *parent)
    : QDialog(parent)
    , m_view(view)
{
    setWindowTitle(QStringLiteral("Поиск объектов и ресурсов"));
    resize(820, 560);

    auto *root = new QVBoxLayout(this);
    auto *filters = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Имя, текст или тип…"));
    m_search->setClearButtonEnabled(true);
    filters->addWidget(m_search, 1);

    m_type = new QComboBox(this);
    m_type->addItem(QStringLiteral("Все типы"), 0);
    m_type->addItem(QStringLiteral("Фигуры"), 1);
    m_type->addItem(QStringLiteral("Текст"), 2);
    m_type->addItem(QStringLiteral("Изображения"), 3);
    m_type->addItem(QStringLiteral("Размеры"), 4);
    m_type->addItem(QStringLiteral("Коды"), 5);
    filters->addWidget(m_type);

    m_layer = new QComboBox(this);
    m_layer->addItem(QStringLiteral("Все слои"), -1);
    if (m_view) {
        for (int i = 0; i < m_view->layers().size(); ++i)
            m_layer->addItem(m_view->layers().at(i), i);
    }
    filters->addWidget(m_layer);

    m_page = new QSpinBox(this);
    m_page->setRange(0, m_view ? qMax(1, m_view->pageList().size()) : 1);
    m_page->setSpecialValueText(QStringLiteral("Все страницы"));
    m_page->setPrefix(QStringLiteral("Стр. "));
    filters->addWidget(m_page);

    m_missingFonts = new QCheckBox(QStringLiteral("Только с отсутствующими шрифтами"),
                                   this);
    filters->addWidget(m_missingFonts);
    root->addLayout(filters);

    m_results = new QTreeWidget(this);
    m_results->setColumnCount(4);
    m_results->setHeaderLabels({QStringLiteral("Имя"), QStringLiteral("Тип"),
                                QStringLiteral("Страница"), QStringLiteral("Слой")});
    m_results->header()->setStretchLastSection(false);
    m_results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    root->addWidget(m_results, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    root->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    auto *focus = new QPushButton(QStringLiteral("Показать на холсте"), this);
    auto *rename = new QPushButton(QStringLiteral("Переименовать…"), this);
    auto *close = new QPushButton(QStringLiteral("Закрыть"), this);
    buttons->addWidget(focus);
    buttons->addWidget(rename);
    buttons->addStretch(1);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { refresh(); });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_layer, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_page, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(m_missingFonts, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    connect(m_results, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { focusSelected(); });
    connect(focus, &QPushButton::clicked, this, &ResourceSearchDialog::focusSelected);
    connect(rename, &QPushButton::clicked, this, &ResourceSearchDialog::renameSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    refresh();
}

void ResourceSearchDialog::refresh()
{
    if (!m_view)
        return;
    const QString search = m_search->text().trimmed();
    const int typeFilter = m_type->currentData().toInt();
    const int layerFilter = m_layer->currentData().toInt();
    const int pageFilter = m_page->value();
    const bool onlyMissingFonts = m_missingFonts->isChecked();

    const QStringList families = QFontDatabase::families();
    m_results->clear();
    int images = 0;
    int missingImages = 0;
    QStringList missingFamilies;
    int matched = 0;
    for (VectorItem *item : m_view->vectorItems()) {
        const VectorObject object = item->object();
        if (object.type == ShapeType::Image) {
            ++images;
            if (object.image.isNull())
                ++missingImages;
        }
        if (object.type == ShapeType::Text
            && !families.contains(object.font.family(), Qt::CaseInsensitive)
            && !missingFamilies.contains(object.font.family())) {
            missingFamilies.append(object.font.family());
        }

        bool typeOk = typeFilter == 0;
        if (typeFilter == 1)
            typeOk = object.type != ShapeType::Text && object.type != ShapeType::Image
                     && object.type != ShapeType::Dimension
                     && object.type != ShapeType::Code;
        else if (typeFilter == 2)
            typeOk = object.type == ShapeType::Text;
        else if (typeFilter == 3)
            typeOk = object.type == ShapeType::Image;
        else if (typeFilter == 4)
            typeOk = object.type == ShapeType::Dimension;
        else if (typeFilter == 5)
            typeOk = object.type == ShapeType::Code;
        if (!typeOk)
            continue;
        if (layerFilter >= 0 && object.layer != layerFilter)
            continue;
        if (pageFilter > 0 && object.page != pageFilter - 1)
            continue;
        const bool fontMissing = object.type == ShapeType::Text
                                 && !families.contains(object.font.family(),
                                                       Qt::CaseInsensitive);
        if (onlyMissingFonts && !fontMissing)
            continue;
        const QString label = objectLabel(object);
        if (!search.isEmpty()
            && !label.contains(search, Qt::CaseInsensitive)
            && !object.text.contains(search, Qt::CaseInsensitive)
            && !objectTypeName(object).contains(search, Qt::CaseInsensitive))
            continue;

        QString pageName = QStringLiteral("Мастер");
        if (object.page >= 0)
            pageName = QStringLiteral("%1").arg(object.page + 1);
        else if (object.page == -9999)
            pageName = QStringLiteral("Текущая");
        const QString layerName = object.layer >= 0
                                      ? m_view->layers().value(object.layer)
                                      : QStringLiteral("—");
        auto *entry = new QTreeWidgetItem(
            m_results, {label, objectTypeName(object), pageName, layerName});
        entry->setData(0, Qt::UserRole,
                       QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(item)));
        entry->setToolTip(0, fontMissing
                                 ? QStringLiteral("Отсутствует шрифт: %1")
                                       .arg(object.font.family())
                                 : object.name);
        ++matched;
    }
    m_summary->setText(
        QStringLiteral("Найдено: %1 · изображений: %2 · отсутствующих "
                       "изображений: %3 · отсутствующие шрифты: %4 · "
                       "внешних файлов нет (изображения встроены)")
            .arg(matched)
            .arg(images)
            .arg(missingImages)
            .arg(missingFamilies.isEmpty() ? QStringLiteral("нет")
                                           : missingFamilies.join(QStringLiteral(", "))));
}

void ResourceSearchDialog::focusSelected()
{
    QTreeWidgetItem *entry = m_results->currentItem();
    if (!entry)
        return;
    auto *item = reinterpret_cast<VectorItem *>(
        entry->data(0, Qt::UserRole).value<quintptr>());
    if (!item)
        return;
    m_view->focusItem(item);
}

void ResourceSearchDialog::renameSelected()
{
    QTreeWidgetItem *entry = m_results->currentItem();
    if (!entry)
        return;
    auto *item = reinterpret_cast<VectorItem *>(
        entry->data(0, Qt::UserRole).value<quintptr>());
    if (!item)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Имя объекта"), QStringLiteral("Название:"),
        QLineEdit::Normal, item->object().name, &ok);
    if (!ok)
        return;
    m_view->renameItem(item, name);
    refresh();
}
