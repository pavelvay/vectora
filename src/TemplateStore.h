#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct DocumentTemplate {
    QString id;
    QString name;
    QString category;
    QString description;
    bool favorite = false;
    bool builtIn = false;
    QJsonObject document;
};

class TemplateStore : public QObject
{
    Q_OBJECT

public:
    static TemplateStore &instance();

    void load();
    bool save(QString *error = nullptr);
    QString storageFile() const;
    static QString templateExtension();

    int count() const { return m_templates.size(); }
    const QVector<DocumentTemplate> &templates() const { return m_templates; }
    const DocumentTemplate &at(int index) const;
    DocumentTemplate *at(int index);
    int indexOf(const QString &id) const;
    QStringList categories() const;
    static QStringList builtinCategories();

    int add(const DocumentTemplate &documentTemplate);
    bool remove(int index);
    void toggleFavorite(int index);
    bool importTemplate(const QString &path, QString *error);
    bool exportTemplate(int index, const QString &path, QString *error) const;

signals:
    void changed();

private:
    TemplateStore();
    void ensureBuiltins();

    QVector<DocumentTemplate> m_templates;
    bool m_loaded = false;
};
