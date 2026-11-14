#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// One reusable resource. Resources are stored as SVG markup: it keeps vector
// geometry, text and colours, can be previewed with QSvgRenderer and inserted
// back through the SVG importer.
struct Asset {
    QString id;
    QString name;
    QString category = QStringLiteral("Пользовательские");
    bool favorite = false;
    QString svg;
};

struct AssetLibrary {
    QString name;
    QStringList categories;
    QVector<Asset> assets;
};

// Global store of asset libraries, persisted between launches. Symbols are a
// per-document concept and live in Document/SymbolDefinition instead.
class AssetStore : public QObject
{
    Q_OBJECT

public:
    static AssetStore &instance();

    void load();
    bool save(QString *error = nullptr);
    QString storageFile() const;
    static QString libraryExtension();

    int count() const { return m_libraries.size(); }
    const AssetLibrary &library(int index) const;
    AssetLibrary &libraryRef(int index);
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);

    int addLibrary(const QString &name);
    int importLibrary(const QString &path, QString *error);
    bool exportLibrary(int index, const QString &path, QString *error) const;

    bool addAsset(int library, const Asset &asset);
    bool removeAsset(int library, int assetIndex);
    void renameAsset(int library, int assetIndex, const QString &name);
    void setAssetCategory(int library, int assetIndex, const QString &category);
    void toggleFavorite(int library, int assetIndex);
    bool copyAsset(int fromLibrary, int assetIndex, int toLibrary);
    QStringList categories(int library) const;
    void addCategory(int library, const QString &name);
    QString uniqueAssetName(int library, const QString &base) const;
    Asset *assetAt(int library, int assetIndex);
    const Asset *assetAt(int library, int assetIndex) const;
    int findAsset(const QString &id, int library = -1) const;
    int libraryOfAsset(const QString &id) const;

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool undo();
    bool redo();
    void clearUndo();

    static QImage renderPreview(const QString &svg, const QSize &size);

signals:
    void changed();

private:
    AssetStore();
    void pushUndo();
    void ensureDefaults();

    QVector<AssetLibrary> m_libraries;
    int m_current = 0;
    QVector<QByteArray> m_undo;
    QVector<QByteArray> m_redo;
    bool m_loaded = false;
};
