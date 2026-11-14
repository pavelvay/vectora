#include "DocumentTools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString g_testDirectory;

QString safeName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("document");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")),
                 QStringLiteral("_"));
    return name;
}

QString writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return QString();
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return QString();
    }
    return path;
}

QString typeName(ShapeType type)
{
    switch (type) {
    case ShapeType::Rectangle: return QStringLiteral("прямоугольники");
    case ShapeType::Ellipse: return QStringLiteral("эллипсы");
    case ShapeType::Polygon: return QStringLiteral("многоугольники");
    case ShapeType::Star: return QStringLiteral("звёзды");
    case ShapeType::Spiral: return QStringLiteral("спирали");
    case ShapeType::Line: return QStringLiteral("линии");
    case ShapeType::Bezier: return QStringLiteral("кривые");
    case ShapeType::Freehand: return QStringLiteral("рисунки от руки");
    case ShapeType::Artistic: return QStringLiteral("художественные мазки");
    case ShapeType::Text: return QStringLiteral("текст");
    case ShapeType::Image: return QStringLiteral("изображения");
    case ShapeType::Dimension: return QStringLiteral("размеры");
    case ShapeType::Code: return QStringLiteral("коды");
    }
    return QStringLiteral("объекты");
}

} // namespace

QString AutoSaveManager::directory()
{
    if (!g_testDirectory.isEmpty())
        return g_testDirectory;
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/autosave");
}

void AutoSaveManager::setDirectoryForTests(const QString &path)
{
    g_testDirectory = path;
}

bool AutoSaveManager::saveSnapshot(const QString &sessionId,
                                   const QString &originalPath,
                                   const QString &title,
                                   const QJsonObject &snapshot,
                                   int keepCount, QString *savedPath,
                                   QString *error)
{
    QDir().mkpath(directory());
    const QString session = safeName(sessionId);
    const QString stamp = QDateTime::currentDateTimeUtc()
                              .toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    QString base = directory() + QLatin1Char('/') + session
                   + QLatin1Char('_') + stamp;
    int suffix = 1;
    while (QFile::exists(base + QStringLiteral(".vct")))
        base = directory() + QLatin1Char('/') + session + QLatin1Char('_')
               + stamp + QStringLiteral("_%1").arg(suffix++);
    const QString path = base + QStringLiteral(".vct");
    if (writeJson(path, snapshot, error).isEmpty())
        return false;

    QJsonObject meta;
    meta.insert(QStringLiteral("originalPath"), originalPath);
    meta.insert(QStringLiteral("title"), title);
    meta.insert(QStringLiteral("session"), session);
    meta.insert(QStringLiteral("time"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    writeJson(base + QStringLiteral(".meta.json"), meta, nullptr);

    if (keepCount > 0) {
        QDir dir(directory());
        const QStringList files = dir.entryList(
            {session + QStringLiteral("_*.vct")}, QDir::Files, QDir::Time);
        for (int i = keepCount; i < files.size(); ++i) {
            const QString old = directory() + QLatin1Char('/') + files.at(i);
            QFile::remove(old);
            QFile::remove(old + QStringLiteral(".meta.json"));
        }
    }
    if (savedPath)
        *savedPath = path;
    return true;
}

QVector<AutoSaveEntry> AutoSaveManager::pending()
{
    QVector<AutoSaveEntry> entries;
    QDir dir(directory());
    if (!dir.exists())
        return entries;
    const QStringList files = dir.entryList({QStringLiteral("*.vct")}, QDir::Files,
                                            QDir::Time);
    for (const QString &file : files) {
        const QString path = dir.filePath(file);
        AutoSaveEntry entry;
        entry.snapshotPath = path;
        entry.time = QFileInfo(path).lastModified();
        QFile meta(path + QStringLiteral(".meta.json"));
        if (meta.open(QIODevice::ReadOnly)) {
            const QJsonObject object =
                QJsonDocument::fromJson(meta.readAll()).object();
            entry.originalPath = object.value(QStringLiteral("originalPath")).toString();
            entry.title = object.value(QStringLiteral("title")).toString();
            const QDateTime time = QDateTime::fromString(
                object.value(QStringLiteral("time")).toString(), Qt::ISODate);
            if (time.isValid())
                entry.time = time;
        }
        if (entry.title.isEmpty()) {
            entry.title = entry.originalPath.isEmpty()
                              ? QStringLiteral("Несохранённый документ")
                              : QFileInfo(entry.originalPath).fileName();
        }
        entries.append(entry);
    }
    return entries;
}

QJsonObject AutoSaveManager::loadSnapshot(const QString &path, QString *originalPath)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (originalPath) {
        QFile meta(path + QStringLiteral(".meta.json"));
        if (meta.open(QIODevice::ReadOnly)) {
            *originalPath = QJsonDocument::fromJson(meta.readAll())
                                .object()
                                .value(QStringLiteral("originalPath"))
                                .toString();
        }
    }
    return document.object();
}

void AutoSaveManager::discardSession(const QString &sessionId)
{
    QDir dir(directory());
    const QString session = safeName(sessionId);
    for (const QString &file :
         dir.entryList({session + QStringLiteral("_*")}, QDir::Files)) {
        QFile::remove(dir.filePath(file));
    }
}

void AutoSaveManager::discardAll()
{
    QDir dir(directory());
    for (const QString &file : dir.entryList({QStringLiteral("*")}, QDir::Files))
        QFile::remove(dir.filePath(file));
}

QString versionsSidecar(const QString &documentPath)
{
    if (!documentPath.isEmpty())
        return documentPath + QStringLiteral(".versions.json");
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base + QStringLiteral("/versions"));
    return base + QStringLiteral("/versions/untitled.json");
}

QVector<DocumentVersion> loadVersions(const QString &documentPath)
{
    QVector<DocumentVersion> versions;
    QFile file(versionsSidecar(documentPath));
    if (!file.open(QIODevice::ReadOnly))
        return versions;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value :
         root.value(QStringLiteral("versions")).toArray()) {
        const QJsonObject entry = value.toObject();
        DocumentVersion version;
        version.name = entry.value(QStringLiteral("name")).toString();
        version.description = entry.value(QStringLiteral("description")).toString();
        version.created = QDateTime::fromString(
            entry.value(QStringLiteral("created")).toString(), Qt::ISODate);
        version.snapshot = entry.value(QStringLiteral("snapshot")).toObject();
        if (!version.snapshot.isEmpty())
            versions.append(version);
    }
    return versions;
}

bool saveVersions(const QString &documentPath,
                  const QVector<DocumentVersion> &versions, QString *error)
{
    QJsonArray array;
    for (const DocumentVersion &version : versions) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), version.name);
        entry.insert(QStringLiteral("description"), version.description);
        entry.insert(QStringLiteral("created"),
                     version.created.toString(Qt::ISODate));
        entry.insert(QStringLiteral("snapshot"), version.snapshot);
        array.append(entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-versions"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("versions"), array);
    return !writeJson(versionsSidecar(documentPath), root, error).isEmpty();
}

QString compareDocuments(const QJsonObject &left, const QJsonObject &right,
                         const QString &leftName, const QString &rightName)
{
    if (left == right)
        return QStringLiteral("Снимки полностью совпадают.");

    QString error;
    const Document a = Document::fromJson(left, &error);
    const Document b = Document::fromJson(right, &error);

    QMap<QString, int> countsA;
    QMap<QString, int> countsB;
    for (const VectorObject &object : a.objects)
        countsA[typeName(object.type)] += 1;
    for (const VectorObject &object : b.objects)
        countsB[typeName(object.type)] += 1;

    QStringList lines;
    lines << QStringLiteral("Сравнение: %1 ↔ %2").arg(leftName, rightName);
    lines << QStringLiteral("Объектов: %1 → %2 (%3%4)")
                 .arg(a.objects.size())
                 .arg(b.objects.size())
                 .arg(b.objects.size() - a.objects.size() >= 0
                          ? QStringLiteral("+")
                          : QString())
                 .arg(b.objects.size() - a.objects.size());
    lines << QStringLiteral("Страниц: %1 → %2").arg(a.pages.size()).arg(b.pages.size());
    lines << QStringLiteral("Слоёв: %1 → %2").arg(a.layers.size()).arg(b.layers.size());
    lines << QStringLiteral("Масштаб чертежа: 1:%1 → 1:%2")
                 .arg(a.drawingScale).arg(b.drawingScale);
    lines << QStringLiteral("Символов: %1 → %2").arg(a.symbols.size()).arg(b.symbols.size());
    lines << QStringLiteral("Узоров: %1 → %2").arg(a.patterns.size()).arg(b.patterns.size());

    QStringList types = countsA.keys() + countsB.keys();
    types.removeDuplicates();
    types.sort();
    for (const QString &type : types) {
        const int av = countsA.value(type);
        const int bv = countsB.value(type);
        if (av != bv)
            lines << QStringLiteral("· %1: %2 → %3").arg(type).arg(av).arg(bv);
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList documentFonts(const Document &document)
{
    QSet<QString> families;
    for (const VectorObject &object : document.objects) {
        if (object.type == ShapeType::Text && !object.font.family().isEmpty())
            families.insert(object.font.family());
    }
    QStringList result = families.values();
    result.sort();
    return result;
}

QStringList missingFonts(const Document &document)
{
    const QStringList available = QFontDatabase::families();
    QStringList missing;
    for (const QString &family : documentFonts(document)) {
        if (!available.contains(family, Qt::CaseInsensitive))
            missing.append(family);
    }
    return missing;
}

PackageReport packageDocument(const Document &document,
                              const QString &targetDirectory,
                              const QString &documentName)
{
    PackageReport result;
    if (targetDirectory.isEmpty()) {
        result.error = QStringLiteral("Не выбрана папка.");
        return result;
    }
    const QString base = safeName(documentName);
    QString folder = targetDirectory + QLatin1Char('/') + base
                     + QStringLiteral("_package");
    int suffix = 2;
    while (QDir(folder).exists())
        folder = targetDirectory + QLatin1Char('/') + base
                 + QStringLiteral("_package_%1").arg(suffix++);
    QDir().mkpath(folder);
    QDir().mkpath(folder + QStringLiteral("/images"));

    QString error;
    const QString documentPath = folder + QLatin1Char('/') + base
                                 + QStringLiteral(".vct");
    if (!document.save(documentPath, &error)) {
        result.error = QStringLiteral("Не удалось сохранить документ: %1").arg(error);
        return result;
    }

    int imageCount = 0;
    for (const VectorObject &object : document.objects) {
        if (object.type != ShapeType::Image || object.image.isNull())
            continue;
        const QString imagePath = folder
                                  + QStringLiteral("/images/image_%1.png")
                                        .arg(++imageCount, 4, 10, QLatin1Char('0'));
        object.image.save(imagePath, "PNG");
    }

    const QStringList fonts = documentFonts(document);
    const QStringList absentFonts = missingFonts(document);
    QFile file(documentPath);
    QByteArray checksum;
    if (file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(file.readAll());
        checksum = hash.result().toHex();
    }
    Document reloaded;
    const bool verified = reloaded.load(documentPath, &error)
                          && reloaded.objects.size() == document.objects.size();

    QStringList report;
    report << QStringLiteral("Vectora · отчёт об упаковке");
    report << QStringLiteral("Документ: %1").arg(base + QStringLiteral(".vct"));
    report << QStringLiteral("Страниц: %1").arg(document.pages.size());
    report << QStringLiteral("Слоёв: %1").arg(document.layers.size());
    report << QStringLiteral("Объектов: %1").arg(document.objects.size());
    report << QStringLiteral("Изображений выгружено: %1 (в документе они встроены)")
                  .arg(imageCount);
    report << QStringLiteral("Символов: %1").arg(document.symbols.size());
    report << QStringLiteral("Узоров: %1").arg(document.patterns.size());
    report << QStringLiteral("Перетеканий: %1").arg(document.blends.size());
    report << QStringLiteral("Шрифты: %1")
                  .arg(fonts.isEmpty() ? QStringLiteral("не используются")
                                       : fonts.join(QStringLiteral(", ")));
    if (!absentFonts.isEmpty()) {
        report << QStringLiteral("Отсутствующие шрифты: %1")
                      .arg(absentFonts.join(QStringLiteral(", ")));
    }
    report << QStringLiteral("Шрифтовые файлы не копируются: для их передачи "
                             "нужны лицензионные права.");
    report << QStringLiteral("Контрольная сумма SHA-1: %1")
                  .arg(QString::fromLatin1(checksum));
    report << QStringLiteral("Проверка целостности: %1")
                  .arg(verified ? QStringLiteral("успешно")
                                : QStringLiteral("ошибка загрузки копии"));

    QFile reportFile(folder + QStringLiteral("/report.txt"));
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reportFile.write(report.join(QLatin1Char('\n')).toUtf8());
    }

    result.ok = verified;
    result.folder = folder;
    result.text = report.join(QLatin1Char('\n'));
    if (!verified)
        result.error = QStringLiteral("Сохранённая копия не прошла проверку.");
    return result;
}
