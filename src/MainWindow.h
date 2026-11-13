#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointF>

#include "Tools.h"

class QAction;
class QComboBox;
class QDockWidget;
class QGraphicsScene;
class QLabel;
class QStackedWidget;
class QTreeWidget;

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
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_toolbarZoomCombo = nullptr;
    QComboBox *m_unitsCombo = nullptr;
    QLabel *m_coordsLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_pageLabel = nullptr;

    QPointF m_cursorScenePos;
    int m_zoomPageIndex = 0;
    int m_transformPageIndex = 1;
    int m_textPageIndex = 2;
    int m_paintPageIndex = 3;
};
