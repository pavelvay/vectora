#pragma once

#include <QHash>
#include <QIcon>
#include <QWidget>

class CanvasView;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;

// Dock panel with two pages: reusable asset libraries (global, persisted) and
// the symbols stored in the current document.
class AssetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetPanel(CanvasView *view, QWidget *parent = nullptr);

    void refresh();
    void refreshSymbols();
    void showAssetsTab();
    void showSymbolsTab();
    void showPatternsTab();
    void refreshPatterns();

    // Actions shared with the menu bar.
    void addSelectionToLibrary();
    void createSymbol();
    void insertCurrentSymbol();
    void updateSymbol();
    void detachSymbol();
    void renameSymbol();
    void deleteSymbol();
    void newLibrary();
    void importLibrary();
    void exportLibrary();

private:
    void buildUi();
    void applyFilters();
    void importSvgAsset();
    void insertAsset(const QString &svg, const QString &name);
    void newCategory();
    void showAssetMenu(const QPoint &pos);
    void showSymbolMenu(const QPoint &pos);
    QString promptCategory() const;

    CanvasView *m_view = nullptr;
    QComboBox *m_libraries = nullptr;
    QComboBox *m_categories = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_favorites = nullptr;
    QListWidget *m_assets = nullptr;
    QListWidget *m_symbols = nullptr;
    QListWidget *m_patterns = nullptr;
    QLabel *m_status = nullptr;
    class QTabWidget *m_tabs = nullptr;
    QHash<QString, QIcon> m_iconCache;
    bool m_updating = false;
};
