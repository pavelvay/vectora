#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Document.h"

// ---------------------------------------------------------------------------
// Autosave / crash recovery
// ---------------------------------------------------------------------------

struct AutoSaveEntry {
    QString snapshotPath;
    QString originalPath;
    QString title;
    QDateTime time;
};

class AutoSaveManager
{
public:
    static QString directory();
    static void setDirectoryForTests(const QString &path);

    static bool saveSnapshot(const QString &sessionId, const QString &originalPath,
                             const QString &title, const QJsonObject &snapshot,
                             int keepCount, QString *savedPath = nullptr,
                             QString *error = nullptr);
    static QVector<AutoSaveEntry> pending();
    static QJsonObject loadSnapshot(const QString &path, QString *originalPath = nullptr);
    static void discardSession(const QString &sessionId);
    static void discardAll();
};

// ---------------------------------------------------------------------------
// Named document versions
// ---------------------------------------------------------------------------

struct DocumentVersion {
    QString name;
    QString description;
    QDateTime created;
    QJsonObject snapshot;
};

QString versionsSidecar(const QString &documentPath);
QVector<DocumentVersion> loadVersions(const QString &documentPath);
bool saveVersions(const QString &documentPath, const QVector<DocumentVersion> &versions,
                  QString *error = nullptr);
QString compareDocuments(const QJsonObject &left, const QJsonObject &right,
                         const QString &leftName, const QString &rightName);

// ---------------------------------------------------------------------------
// Resource checks and packaging
// ---------------------------------------------------------------------------

QStringList missingFonts(const Document &document);
QStringList documentFonts(const Document &document);

struct PackageReport {
    bool ok = false;
    QString folder;
    QString text;
    QString error;
};

PackageReport packageDocument(const Document &document, const QString &targetDirectory,
                              const QString &documentName);
