#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointF>
#include <QVector>

#include "Document.h"
#include "Tools.h"

class QAction;
class QComboBox;
class QDockWidget;
class QGraphicsScene;
class QLabel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

class CanvasView;
class ColorPalette;
class Ruler;
class ToolPalette;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void setActiveTool(Tool tool);
    bool openPath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createMainToolBar();
    void createPropertyBar();
    void createDocks();
    void createCanvas();
    void createStatusBar();
    void populateSample();
    void applyTool(Tool tool);

    void newDocument();
    void openDocument();
    void saveDocument();
    void saveDocumentAs();
    void importFile();
    void exportFile();
    void exportPdf();
    void printDocument();
    void applyDocument(const Document &document);
    Document currentDocument() const;
    bool maybeSave();
    void setModified(bool modified);
    void updateTitle();
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    void resetHistory();
    void updateHistoryActions();
    void onPaletteColor(const QColor &color);
    void applyFillToSelection(const QColor &color);
    void onSelectionChanged(int count);
    void rebuildObjectsTree();
    void copySelection();
    void cutSelection();
    void pasteClipboard();

    QAction *makeAction(const QString &key, const QString &text,
                        const QKeySequence &shortcut = QKeySequence(),
                        const QString &statusTip = QString(),
                        bool stubAction = true);
    QAction *action(const QString &key) const;

    void showStub(const QString &feature);
    void showShortcuts();
    void showAbout();
    void updateZoomUi(qreal zoom);
    void applyZoomText(const QString &text);
    void updateCoordinates(const QPointF &scenePos);
    void selectAllObjects();
    void deleteSelection();

    QHash<QString, QAction *> m_actions;

    CanvasView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    Ruler *m_hRuler = nullptr;
    Ruler *m_vRuler = nullptr;
    ToolPalette *m_toolPalette = nullptr;
    ColorPalette *m_palette = nullptr;

    QDockWidget *m_toolsDock = nullptr;
    QDockWidget *m_objectsDock = nullptr;
    QDockWidget *m_propertiesDock = nullptr;
    QDockWidget *m_paletteDock = nullptr;

    QStackedWidget *m_propertyStack = nullptr;
    QTreeWidget *m_objectsTree = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_toolbarZoomCombo = nullptr;
    QComboBox *m_unitsCombo = nullptr;
    QLabel *m_coordsLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;

    QPointF m_cursorScenePos;
    int m_zoomPageIndex = 0;
    int m_transformPageIndex = 1;
    int m_textPageIndex = 2;
    int m_paintPageIndex = 3;

    QString m_filePath;
    bool m_modified = false;
    bool m_editOpen = false;
    bool m_restoring = false;
    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
    QJsonObject m_pendingSnapshot;
    QVector<VectorObject> m_clipboard;
};
