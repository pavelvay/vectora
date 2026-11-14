#pragma once

#include <QWidget>

class CanvasView;
class QLabel;
class QMenu;
class QTabBar;
class QToolButton;

class PageBar : public QWidget
{
    Q_OBJECT

public:
    explicit PageBar(QWidget *parent = nullptr);

    void setView(CanvasView *view);
    void refresh();

signals:
    void pageSetupRequested();
    void status(const QString &text);

private:
    void showTabMenu(const QPoint &pos);
    void rebuildMasterMenu();
    void createMasterFromCurrentPage();

    CanvasView *m_view = nullptr;
    QTabBar *m_tabs = nullptr;
    QLabel *m_pageLabel = nullptr;
    QToolButton *m_backButton = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_duplicateButton = nullptr;
    QToolButton *m_removeButton = nullptr;
    QToolButton *m_masterButton = nullptr;
    QMenu *m_masterMenu = nullptr;
    bool m_updating = false;
};
