#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "DocumentTools.h"

class CanvasView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTextBrowser;
class QTreeWidget;

// Undo/redo history with named states.
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    HistoryDialog(const QStringList &labels, const QVector<QDateTime> &times,
                  int currentIndex, QWidget *parent = nullptr);
    int selectedIndex() const { return m_selected; }

private:
    int m_selected = -1;
};

// Named document versions with restore and comparison.
class VersionDialog : public QDialog
{
    Q_OBJECT

public:
    VersionDialog(QVector<DocumentVersion> *versions,
                  const QJsonObject &currentSnapshot, const QString &currentName,
                  QWidget *parent = nullptr);

    bool restoreRequested() const { return m_restore; }
    QJsonObject restoreSnapshot() const { return m_restoreSnapshot; }
    bool versionsChanged() const { return m_changed; }

private:
    void refresh();
    void createVersion();
    void restoreVersion();
    void deleteVersion();
    void compareVersions();

    QVector<DocumentVersion> *m_versions = nullptr;
    QJsonObject m_current;
    QString m_currentName;
    QListWidget *m_list = nullptr;
    QTextBrowser *m_details = nullptr;
    bool m_restore = false;
    bool m_changed = false;
    QJsonObject m_restoreSnapshot;
};

// Manager for built-in and user document templates.
class TemplateDialog : public QDialog
{
    Q_OBJECT

public:
    TemplateDialog(const QJsonObject &currentSnapshot, QWidget *parent = nullptr);

    QJsonObject selectedDocument() const { return m_selected; }

private:
    void refresh();
    void updatePreview();
    void createFromTemplate();
    void saveCurrent();
    void removeTemplate();
    void toggleFavorite();
    void importTemplate();
    void exportTemplate();

    QJsonObject m_current;
    QJsonObject m_selected;
    QLineEdit *m_search = nullptr;
    QComboBox *m_category = nullptr;
    QCheckBox *m_favorites = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_summary = nullptr;
};

// Search objects by name, type, layer, page and missing fonts.
class ResourceSearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResourceSearchDialog(CanvasView *view, QWidget *parent = nullptr);

private:
    void refresh();
    void focusSelected();
    void renameSelected();

    CanvasView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_layer = nullptr;
    QSpinBox *m_page = nullptr;
    QCheckBox *m_missingFonts = nullptr;
    QTreeWidget *m_results = nullptr;
    QLabel *m_summary = nullptr;
};
