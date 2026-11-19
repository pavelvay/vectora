#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointF>
#include <QVector>

#include "Document.h"
#include "DocumentTools.h"
#include "Tools.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QGraphicsScene;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

class CanvasView;
class ColorPalette;
class PageBar;
class DockPanels;
class AssetPanel;
class UpdateChecker;
class Ruler;
class ToolPalette;
class VectorItem;
class WelcomeWidget;

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
    void startDimensionTool(int type);
    void updateScaleLabel();
    void updateRulers();
    void openCodeDialog(VectorItem *item, int kind);
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
    void exportAllPages();
    void printDocument();
    void applyDocument(const Document &document);
    Document currentDocument() const;
    bool maybeSave();
    void setModified(bool modified);
    void updateTitle();
    void updatePageBadge();
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    void resetHistory();
    void setupAutosave();
    void performAutosave();
    void clearAutosave();
    void recoverAutosaves();
    void saveHistorySidecar();
    void loadHistorySidecar();
    void restoreHistoryIndex(int index);
    void openHistoryDialog();
    void openVersionDialog();
    void openTemplateDialog();
    void openResourceSearch();
    void packageCurrentDocument();
    void showPreferences();
    void updateHistoryActions();
    void onPaletteColor(const QColor &color);
    void applyFillToSelection(const QColor &color);
    void onSelectionChanged(int count);
    void showWelcome();
    void showEditor();
    void showDockerTab(const QString &title, int tab);
    void showCanvasContextMenu(const QPoint &globalPos, VectorItem *item);
    void changePageSize();
    void loadSampleDocument();
    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void refreshQuickTransform();
    void applyQuickTransform();
    void syncShapeOptions();
    void applyShapeOptions();
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
    PageBar *m_pageBar = nullptr;

    QDockWidget *m_toolsDock = nullptr;
    QDockWidget *m_paletteDock = nullptr;
    DockPanels *m_dockPanels = nullptr;
    QDockWidget *m_assetDock = nullptr;
    AssetPanel *m_assetPanel = nullptr;
    QStackedWidget *m_centralStack = nullptr;
    QWidget *m_editorPage = nullptr;
    WelcomeWidget *m_welcome = nullptr;
    QToolBar *m_mainToolBar = nullptr;
    QToolBar *m_propertyBar = nullptr;

    QStackedWidget *m_propertyStack = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_toolbarZoomCombo = nullptr;
    QComboBox *m_unitsCombo = nullptr;
    QLabel *m_coordsLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_pageBadge = nullptr;
    QLabel *m_rulerCorner = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QDoubleSpinBox *m_quickX = nullptr;
    QDoubleSpinBox *m_quickY = nullptr;
    QDoubleSpinBox *m_quickW = nullptr;
    QDoubleSpinBox *m_quickH = nullptr;
    QDoubleSpinBox *m_quickAngle = nullptr;

    QPointF m_cursorScenePos;
    int m_zoomPageIndex = 0;
    int m_transformPageIndex = 1;
    int m_textPageIndex = 2;
    int m_paintPageIndex = 3;
    int m_bezierPageIndex = 4;
    int m_rectPageIndex = 5;
    int m_polygonPageIndex = 6;
    int m_starPageIndex = 7;
    int m_spiralPageIndex = 8;
    int m_nodePageIndex = 9;
    int m_dimensionPageIndex = 10;
    Tool m_currentTool = Tool::Select;
    bool m_updatingShapeOptions = false;
    QDoubleSpinBox *m_rectRadius = nullptr;
    QDoubleSpinBox *m_polygonSides = nullptr;
    QDoubleSpinBox *m_starPoints = nullptr;
    QDoubleSpinBox *m_starRatio = nullptr;
    QDoubleSpinBox *m_spiralTurns = nullptr;
    QDoubleSpinBox *m_dimensionOffsetSpin = nullptr;
    QComboBox *m_dimensionTypeCombo = nullptr;
    QComboBox *m_dimensionUnitsCombo = nullptr;
    QSpinBox *m_dimensionPrecisionSpin = nullptr;
    QCheckBox *m_dimensionAssocCheck = nullptr;
    QLabel *m_scaleLabel = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    bool m_manualUpdateCheck = false;
    bool m_updateDialogOpen = false;

    QString m_filePath;
    bool m_modified = false;
    bool m_editOpen = false;
    bool m_restoring = false;
    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
    QStringList m_undoLabels;
    QStringList m_redoLabels;
    QVector<QDateTime> m_undoTimes;
    QVector<QDateTime> m_redoTimes;
    QString m_lastHint;
    QDateTime m_lastHintTime;
    QVector<DocumentVersion> m_versions;
    QString m_autosaveSession;
    class QTimer *m_autosaveTimer = nullptr;
    bool m_selectionUpdateScheduled = false;
    QJsonObject m_pendingSnapshot;
    QVector<VectorObject> m_clipboard;
};
