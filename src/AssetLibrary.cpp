#include "AssetLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QUuid>

namespace {

const char *kCategories[] = {
    "Иконки", "Логотипы", "Фигуры", "Декоративные",
    "Геометрические", "Текстовые", "Символы", "Пользовательские",
};

QString makeSvg(const QString &body)
{
    return QStringLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" "
               "height=\"100\" viewBox=\"0 0 100 100\">%1</svg>")
        .arg(body);
}

Asset makeAsset(const QString &name, const QString &category, const QString &svg)
{
    Asset asset;
    asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.name = name;
    asset.category = category;
    asset.svg = svg;
    return asset;
}

QVector<Asset> builtinAssets()
{
    QVector<Asset> assets;

    assets.append(makeAsset(
        QStringLiteral("Круг"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<circle cx=\"50\" cy=\"50\" r=\"42\" fill=\"#4f7cc0\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Скруглённый квадрат"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<rect x=\"10\" y=\"10\" width=\"80\" height=\"80\" "
                               "rx=\"14\" fill=\"#06d6a0\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Треугольник"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<path d=\"M50 8 L94 88 L6 88 Z\" fill=\"#ffd166\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Шестиугольник"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<path d=\"M50 6 L88 28 L88 72 L50 94 L12 72 "
                               "L12 28 Z\" fill=\"#7f5af0\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Звезда"), QStringLiteral("Фигуры"),
        makeSvg(QStringLiteral("<path d=\"M50 6 L61 37 L94 37 L67 56 L77 88 "
                               "L50 68 L23 88 L33 56 L6 37 L39 37 Z\" "
                               "fill=\"#ffb547\"/>"))));

    assets.append(makeAsset(
        QStringLiteral("Стрелка"), QStringLiteral("Иконки"),
        makeSvg(QStringLiteral("<path d=\"M8 42 H60 V22 L94 50 L60 78 V58 H8 Z\" "
                               "fill=\"#1d2127\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Галочка"), QStringLiteral("Иконки"),
        makeSvg(QStringLiteral("<path d=\"M14 52 L38 78 L88 20\" fill=\"none\" "
                               "stroke=\"#06d6a0\" stroke-width=\"12\" "
                               "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Плюс"), QStringLiteral("Иконки"),
        makeSvg(QStringLiteral("<rect x=\"42\" y=\"12\" width=\"16\" height=\"76\" "
                               "rx=\"4\" fill=\"#e0466b\"/>"
                               "<rect x=\"12\" y=\"42\" width=\"76\" height=\"16\" "
                               "rx=\"4\" fill=\"#e0466b\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Сердце"), QStringLiteral("Иконки"),
        makeSvg(QStringLiteral("<path d=\"M50 90 C18 68 8 52 8 36 A20 20 0 0 1 50 26 "
                               "A20 20 0 0 1 92 36 C92 52 82 68 50 90 Z\" "
                               "fill=\"#e0466b\"/>"))));

    assets.append(makeAsset(
        QStringLiteral("Кольцо"), QStringLiteral("Декоративные"),
        makeSvg(QStringLiteral("<circle cx=\"50\" cy=\"50\" r=\"36\" fill=\"none\" "
                               "stroke=\"#e0466b\" stroke-width=\"12\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Точки"), QStringLiteral("Декоративные"),
        makeSvg(QStringLiteral("<circle cx=\"30\" cy=\"30\" r=\"9\" fill=\"#4f7cc0\"/>"
                               "<circle cx=\"70\" cy=\"30\" r=\"9\" fill=\"#06d6a0\"/>"
                               "<circle cx=\"30\" cy=\"70\" r=\"9\" fill=\"#ffb547\"/>"
                               "<circle cx=\"70\" cy=\"70\" r=\"9\" fill=\"#e0466b\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Волна"), QStringLiteral("Декоративные"),
        makeSvg(QStringLiteral("<path d=\"M6 62 C26 30 40 30 50 50 C60 70 74 70 94 38\" "
                               "fill=\"none\" stroke=\"#7f5af0\" stroke-width=\"10\" "
                               "stroke-linecap=\"round\"/>"))));

    assets.append(makeAsset(
        QStringLiteral("Линия"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<line x1=\"8\" y1=\"50\" x2=\"92\" y2=\"50\" "
                               "stroke=\"#1d2127\" stroke-width=\"8\" "
                               "stroke-linecap=\"round\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Прицел"), QStringLiteral("Геометрические"),
        makeSvg(QStringLiteral("<circle cx=\"50\" cy=\"50\" r=\"30\" fill=\"none\" "
                               "stroke=\"#1d2127\" stroke-width=\"5\"/>"
                               "<line x1=\"50\" y1=\"6\" x2=\"50\" y2=\"26\" "
                               "stroke=\"#1d2127\" stroke-width=\"5\"/>"
                               "<line x1=\"50\" y1=\"74\" x2=\"50\" y2=\"94\" "
                               "stroke=\"#1d2127\" stroke-width=\"5\"/>"
                               "<line x1=\"6\" y1=\"50\" x2=\"26\" y2=\"50\" "
                               "stroke=\"#1d2127\" stroke-width=\"5\"/>"
                               "<line x1=\"74\" y1=\"50\" x2=\"94\" y2=\"50\" "
                               "stroke=\"#1d2127\" stroke-width=\"5\"/>"))));

    assets.append(makeAsset(
        QStringLiteral("Логотип V"), QStringLiteral("Логотипы"),
        makeSvg(QStringLiteral("<path d=\"M14 16 L50 84 L86 16 L68 16 L50 56 L32 16 Z\" "
                               "fill=\"#00c987\"/>"))));
    assets.append(makeAsset(
        QStringLiteral("Орбита"), QStringLiteral("Логотипы"),
        makeSvg(QStringLiteral("<circle cx=\"50\" cy=\"50\" r=\"14\" fill=\"#4f7cc0\"/>"
                               "<ellipse cx=\"50\" cy=\"50\" rx=\"42\" ry=\"16\" "
                               "fill=\"none\" stroke=\"#1d2127\" stroke-width=\"4\" "
                               "transform=\"rotate(-24 50 50)\"/>"))));

    assets.append(makeAsset(
        QStringLiteral("Заголовок Aa"), QStringLiteral("Текстовые"),
        makeSvg(QStringLiteral("<text x=\"50\" y=\"66\" font-family=\"Adwaita Sans\" "
                               "font-size=\"52\" font-weight=\"bold\" "
                               "text-anchor=\"middle\" fill=\"#1d2127\">Aa</text>"))));
    assets.append(makeAsset(
        QStringLiteral("Символ @"), QStringLiteral("Символы"),
        makeSvg(QStringLiteral("<text x=\"50\" y=\"70\" font-family=\"Adwaita Sans\" "
                               "font-size=\"62\" text-anchor=\"middle\" "
                               "fill=\"#00c987\">@</text>"))));

    return assets;
}

QJsonObject assetToJson(const Asset &asset)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), asset.id);
    entry.insert(QStringLiteral("name"), asset.name);
    entry.insert(QStringLiteral("category"), asset.category);
    entry.insert(QStringLiteral("favorite"), asset.favorite);
    entry.insert(QStringLiteral("svg"), asset.svg);
    return entry;
}

Asset assetFromJson(const QJsonObject &entry)
{
    Asset asset;
    asset.id = entry.value(QStringLiteral("id")).toString();
    if (asset.id.isEmpty())
        asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.name = entry.value(QStringLiteral("name")).toString();
    asset.category = entry.value(QStringLiteral("category"))
                         .toString(QStringLiteral("Пользовательские"));
    asset.favorite = entry.value(QStringLiteral("favorite")).toBool(false);
    asset.svg = entry.value(QStringLiteral("svg")).toString();
    return asset;
}

AssetLibrary libraryFromJson(const QJsonObject &entry)
{
    AssetLibrary library;
    library.name = entry.value(QStringLiteral("name")).toString();
    for (const QJsonValue &value : entry.value(QStringLiteral("categories")).toArray())
        library.categories.append(value.toString());
    for (const QJsonValue &value : entry.value(QStringLiteral("assets")).toArray())
        library.assets.append(assetFromJson(value.toObject()));
    if (library.name.isEmpty())
        library.name = QStringLiteral("Библиотека");
    if (library.categories.isEmpty()) {
        for (const char *category : kCategories)
            library.categories.append(QString::fromUtf8(category));
    }
    return library;
}

QJsonObject libraryToJson(const AssetLibrary &library)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), library.name);
    QJsonArray categories;
    for (const QString &category : library.categories)
        categories.append(category);
    entry.insert(QStringLiteral("categories"), categories);
    QJsonArray assets;
    for (const Asset &asset : library.assets)
        assets.append(assetToJson(asset));
    entry.insert(QStringLiteral("assets"), assets);
    return entry;
}

} // namespace

AssetStore::AssetStore() = default;

AssetStore &AssetStore::instance()
{
    static AssetStore store;
    return store;
}

QString AssetStore::libraryExtension()
{
    return QStringLiteral("vlib");
}

QString AssetStore::storageFile() const
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return directory + QStringLiteral("/asset-libraries.json");
}

const AssetLibrary &AssetStore::library(int index) const
{
    static const AssetLibrary empty;
    if (index < 0 || index >= m_libraries.size())
        return empty;
    return m_libraries.at(index);
}

AssetLibrary &AssetStore::libraryRef(int index)
{
    if (index < 0 || index >= m_libraries.size()) {
        m_libraries.append(AssetLibrary{QStringLiteral("Vectora"), {}, {}});
        m_current = m_libraries.size() - 1;
    }
    return m_libraries[index];
}

void AssetStore::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    QFile file(storageFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            const QJsonObject root = document.object();
            m_current = root.value(QStringLiteral("current")).toInt(0);
            for (const QJsonValue &value :
                 root.value(QStringLiteral("libraries")).toArray())
                m_libraries.append(libraryFromJson(value.toObject()));
        }
    }

    ensureDefaults();
    if (m_current < 0 || m_current >= m_libraries.size())
        m_current = 0;
    save();
}

void AssetStore::ensureDefaults()
{
    if (m_libraries.isEmpty()) {
        AssetLibrary library;
        library.name = QStringLiteral("Vectora");
        for (const char *category : kCategories)
            library.categories.append(QString::fromUtf8(category));
        library.assets = builtinAssets();
        m_libraries.append(library);
        m_current = 0;
        return;
    }

    // A library opened by an older version may lack the standard categories.
    for (AssetLibrary &library : m_libraries) {
        for (const char *category : kCategories) {
            const QString name = QString::fromUtf8(category);
            if (!library.categories.contains(name))
                library.categories.append(name);
        }
    }
}

bool AssetStore::save(QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-asset-libraries"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("current"), m_current);
    QJsonArray libraries;
    for (const AssetLibrary &library : m_libraries)
        libraries.append(libraryToJson(library));
    root.insert(QStringLiteral("libraries"), libraries);

    QFile file(storageFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void AssetStore::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_libraries.size() || index == m_current)
        return;
    m_current = index;
    save();
    emit changed();
}

void AssetStore::pushUndo()
{
    m_redo.clear();
    QJsonArray libraries;
    for (const AssetLibrary &library : m_libraries)
        libraries.append(libraryToJson(library));
    QJsonObject root;
    root.insert(QStringLiteral("current"), m_current);
    root.insert(QStringLiteral("libraries"), libraries);
    m_undo.append(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (m_undo.size() > 50)
        m_undo.removeFirst();
}

bool AssetStore::undo()
{
    if (m_undo.isEmpty())
        return false;
    QJsonArray currentLibraries;
    for (const AssetLibrary &library : m_libraries)
        currentLibraries.append(libraryToJson(library));
    QJsonObject currentRoot;
    currentRoot.insert(QStringLiteral("current"), m_current);
    currentRoot.insert(QStringLiteral("libraries"), currentLibraries);
    m_redo.append(QJsonDocument(currentRoot).toJson(QJsonDocument::Compact));
    if (m_redo.size() > 50)
        m_redo.removeFirst();

    const QByteArray snapshot = m_undo.takeLast();
    const QJsonDocument document = QJsonDocument::fromJson(snapshot);
    if (!document.isObject())
        return false;
    const QJsonObject root = document.object();
    m_libraries.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("libraries")).toArray())
        m_libraries.append(libraryFromJson(value.toObject()));
    m_current = root.value(QStringLiteral("current")).toInt(0);
    if (m_current >= m_libraries.size())
        m_current = qMax(0, m_libraries.size() - 1);
    save();
    emit changed();
    return true;
}

bool AssetStore::redo()
{
    if (m_redo.isEmpty())
        return false;
    QJsonArray currentLibraries;
    for (const AssetLibrary &library : m_libraries)
        currentLibraries.append(libraryToJson(library));
    QJsonObject currentRoot;
    currentRoot.insert(QStringLiteral("current"), m_current);
    currentRoot.insert(QStringLiteral("libraries"), currentLibraries);
    m_undo.append(QJsonDocument(currentRoot).toJson(QJsonDocument::Compact));
    if (m_undo.size() > 50)
        m_undo.removeFirst();

    const QByteArray snapshot = m_redo.takeLast();
    const QJsonDocument document = QJsonDocument::fromJson(snapshot);
    if (!document.isObject())
        return false;
    const QJsonObject root = document.object();
    m_libraries.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("libraries")).toArray())
        m_libraries.append(libraryFromJson(value.toObject()));
    m_current = root.value(QStringLiteral("current")).toInt(0);
    if (m_current >= m_libraries.size())
        m_current = qMax(0, m_libraries.size() - 1);
    save();
    emit changed();
    return true;
}

void AssetStore::clearUndo()
{
    m_undo.clear();
    m_redo.clear();
}

int AssetStore::addLibrary(const QString &name)
{
    pushUndo();
    AssetLibrary library;
    library.name = name.trimmed().isEmpty()
                      ? QStringLiteral("Библиотека %1").arg(m_libraries.size() + 1)
                      : name.trimmed();
    for (const char *category : kCategories)
        library.categories.append(QString::fromUtf8(category));
    m_libraries.append(library);
    m_current = m_libraries.size() - 1;
    save();
    emit changed();
    return m_current;
}

int AssetStore::importLibrary(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return -1;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Файл не является библиотекой Vectora.");
        return -1;
    }
    const QJsonObject root = document.object();
    QVector<AssetLibrary> imported;
    if (root.contains(QStringLiteral("libraries"))) {
        for (const QJsonValue &value :
             root.value(QStringLiteral("libraries")).toArray())
            imported.append(libraryFromJson(value.toObject()));
    } else {
        imported.append(libraryFromJson(root));
    }
    if (imported.isEmpty()) {
        if (error)
            *error = QStringLiteral("В библиотеке нет ресурсов.");
        return -1;
    }

    pushUndo();
    for (AssetLibrary &library : imported) {
        if (library.name.trimmed().isEmpty())
            library.name = QFileInfo(path).completeBaseName();
        for (Asset &asset : library.assets) {
            if (asset.id.isEmpty())
                asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        m_libraries.append(library);
    }
    m_current = m_libraries.size() - 1;
    save();
    emit changed();
    return m_current;
}

bool AssetStore::exportLibrary(int index, const QString &path, QString *error) const
{
    if (index < 0 || index >= m_libraries.size()) {
        if (error)
            *error = QStringLiteral("Библиотека не выбрана.");
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-asset-library"));
    root.insert(QStringLiteral("version"), 1);
    const AssetLibrary &library = m_libraries.at(index);
    root.insert(QStringLiteral("name"), library.name);
    QJsonArray categories;
    for (const QString &category : library.categories)
        categories.append(category);
    root.insert(QStringLiteral("categories"), categories);
    QJsonArray assets;
    for (const Asset &asset : library.assets)
        assets.append(assetToJson(asset));
    root.insert(QStringLiteral("assets"), assets);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool AssetStore::addAsset(int library, const Asset &asset)
{
    if (library < 0 || library >= m_libraries.size() || asset.svg.isEmpty())
        return false;
    pushUndo();
    Asset copy = asset;
    if (copy.id.isEmpty())
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = uniqueAssetName(library, copy.name);
    if (!m_libraries.at(library).categories.contains(copy.category))
        m_libraries[library].categories.append(copy.category);
    m_libraries[library].assets.append(copy);
    save();
    emit changed();
    return true;
}

bool AssetStore::removeAsset(int library, int assetIndex)
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size())
        return false;
    pushUndo();
    m_libraries[library].assets.removeAt(assetIndex);
    save();
    emit changed();
    return true;
}

void AssetStore::renameAsset(int library, int assetIndex, const QString &name)
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size()
        || name.trimmed().isEmpty())
        return;
    pushUndo();
    m_libraries[library].assets[assetIndex].name = name.trimmed();
    save();
    emit changed();
}

void AssetStore::setAssetCategory(int library, int assetIndex, const QString &category)
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size()
        || category.trimmed().isEmpty())
        return;
    pushUndo();
    m_libraries[library].assets[assetIndex].category = category.trimmed();
    if (!m_libraries.at(library).categories.contains(category.trimmed()))
        m_libraries[library].categories.append(category.trimmed());
    save();
    emit changed();
}

void AssetStore::toggleFavorite(int library, int assetIndex)
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size())
        return;
    pushUndo();
    Asset &asset = m_libraries[library].assets[assetIndex];
    asset.favorite = !asset.favorite;
    save();
    emit changed();
}

bool AssetStore::copyAsset(int fromLibrary, int assetIndex, int toLibrary)
{
    if (fromLibrary < 0 || fromLibrary >= m_libraries.size()
        || toLibrary < 0 || toLibrary >= m_libraries.size()
        || fromLibrary == toLibrary
        || assetIndex < 0 || assetIndex >= m_libraries.at(fromLibrary).assets.size())
        return false;
    Asset copy = m_libraries.at(fromLibrary).assets.at(assetIndex);
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString base = copy.name;
    copy.name = base + QStringLiteral(" (копия)");
    return addAsset(toLibrary, copy);
}

QStringList AssetStore::categories(int libraryIndex) const
{
    return library(libraryIndex).categories;
}

void AssetStore::addCategory(int library, const QString &name)
{
    if (library < 0 || library >= m_libraries.size() || name.trimmed().isEmpty())
        return;
    if (m_libraries.at(library).categories.contains(name.trimmed()))
        return;
    pushUndo();
    m_libraries[library].categories.append(name.trimmed());
    save();
    emit changed();
}

QString AssetStore::uniqueAssetName(int libraryIndex, const QString &base) const
{
    const AssetLibrary &target = library(libraryIndex);
    QString name = base.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("Ресурс");
    QString candidate = name;
    int suffix = 2;
    while ([&target, &candidate]() {
        for (const Asset &asset : target.assets) {
            if (asset.name.compare(candidate, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }()) {
        candidate = QStringLiteral("%1 %2").arg(name).arg(suffix++);
    }
    return candidate;
}

Asset *AssetStore::assetAt(int library, int assetIndex)
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size())
        return nullptr;
    return &m_libraries[library].assets[assetIndex];
}

const Asset *AssetStore::assetAt(int library, int assetIndex) const
{
    if (library < 0 || library >= m_libraries.size()
        || assetIndex < 0 || assetIndex >= m_libraries.at(library).assets.size())
        return nullptr;
    return &m_libraries.at(library).assets.at(assetIndex);
}

int AssetStore::findAsset(const QString &id, int library) const
{
    for (int l = 0; l < m_libraries.size(); ++l) {
        if (library >= 0 && l != library)
            continue;
        for (int i = 0; i < m_libraries.at(l).assets.size(); ++i) {
            if (m_libraries.at(l).assets.at(i).id == id)
                return i;
        }
    }
    return -1;
}

int AssetStore::libraryOfAsset(const QString &id) const
{
    for (int l = 0; l < m_libraries.size(); ++l) {
        for (const Asset &asset : m_libraries.at(l).assets) {
            if (asset.id == id)
                return l;
        }
    }
    return -1;
}

QImage AssetStore::renderPreview(const QString &svg, const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Light tile keeps dark artwork visible in the dark dock theme.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xf2, 0xf4, 0xf7));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 6, 6);
    const qreal inset = qMin(size.width(), size.height()) * 0.08;
    renderer.render(&painter, QRectF(QPointF(inset, inset),
                                     QSizeF(size) - QSizeF(inset * 2, inset * 2)));
    painter.end();
    return image;
}
