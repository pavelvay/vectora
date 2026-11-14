#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "Document.h"

// Global library of user patterns, persisted between launches. Patterns stored
// here are copied into a document when applied.
class PatternStore : public QObject
{
    Q_OBJECT

public:
    static PatternStore &instance();

    void load();
    bool save(QString *error = nullptr);
    QString storageFile() const;
    static QString patternExtension();

    int count() const { return m_patterns.size(); }
    const QVector<PatternDefinition> &patterns() const { return m_patterns; }
    const PatternDefinition &pattern(int index) const;
    PatternDefinition &patternRef(int index);

    int add(const PatternDefinition &pattern);
    void remove(int index);
    void rename(int index, const QString &name);
    bool importPattern(const QString &path, QString *error);
    bool exportPattern(int index, const QString &path, QString *error) const;

signals:
    void changed();

private:
    PatternStore() = default;

    QVector<PatternDefinition> m_patterns;
    int m_nextId = 1;
    bool m_loaded = false;
};
