#include "PatternStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "Patterns.h"

PatternStore &PatternStore::instance()
{
    static PatternStore store;
    return store;
}

QString PatternStore::patternExtension()
{
    return QStringLiteral("vpat");
}

QString PatternStore::storageFile() const
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return directory + QStringLiteral("/patterns.json");
}

void PatternStore::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    QFile file(storageFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    const QJsonObject root = document.object();
    for (const QJsonValue &value : root.value(QStringLiteral("patterns")).toArray()) {
        PatternDefinition pattern = patternDefinitionFromJson(value.toObject());
        if (pattern.objects.isEmpty())
            continue;
        pattern.id = m_nextId++;
        m_patterns.append(pattern);
    }
}

bool PatternStore::save(QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-patterns"));
    root.insert(QStringLiteral("version"), 1);
    QJsonArray array;
    for (const PatternDefinition &pattern : m_patterns)
        array.append(patternDefinitionToJson(pattern));
    root.insert(QStringLiteral("patterns"), array);

    QFile file(storageFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

const PatternDefinition &PatternStore::pattern(int index) const
{
    static const PatternDefinition empty;
    if (index < 0 || index >= m_patterns.size())
        return empty;
    return m_patterns.at(index);
}

PatternDefinition &PatternStore::patternRef(int index)
{
    static PatternDefinition fallback;
    if (index < 0 || index >= m_patterns.size()) {
        fallback = PatternDefinition();
        return fallback;
    }
    return m_patterns[index];
}

int PatternStore::add(const PatternDefinition &pattern)
{
    if (pattern.objects.isEmpty())
        return -1;
    PatternDefinition copy = pattern;
    copy.id = m_nextId++;
    m_patterns.append(copy);
    save();
    emit changed();
    return m_patterns.size() - 1;
}

void PatternStore::remove(int index)
{
    if (index < 0 || index >= m_patterns.size())
        return;
    m_patterns.removeAt(index);
    save();
    emit changed();
}

void PatternStore::rename(int index, const QString &name)
{
    if (index < 0 || index >= m_patterns.size() || name.trimmed().isEmpty())
        return;
    m_patterns[index].name = name.trimmed();
    save();
    emit changed();
}

bool PatternStore::importPattern(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Файл не является узором Vectora.");
        return false;
    }
    PatternDefinition pattern = patternDefinitionFromJson(document.object());
    if (pattern.objects.isEmpty()) {
        // Allow a library file with a patterns array as well.
        const QJsonArray array =
            document.object().value(QStringLiteral("patterns")).toArray();
        if (!array.isEmpty())
            pattern = patternDefinitionFromJson(array.first().toObject());
    }
    if (pattern.objects.isEmpty()) {
        if (error)
            *error = QStringLiteral("В файле нет объектов узора.");
        return false;
    }
    if (pattern.name.trimmed().isEmpty())
        pattern.name = QFileInfo(path).completeBaseName();
    add(pattern);
    return true;
}

bool PatternStore::exportPattern(int index, const QString &path, QString *error) const
{
    if (index < 0 || index >= m_patterns.size()) {
        if (error)
            *error = QStringLiteral("Узор не выбран.");
        return false;
    }
    QJsonObject root = patternDefinitionToJson(m_patterns.at(index));
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-pattern"));
    root.insert(QStringLiteral("version"), 1);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
