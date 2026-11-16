#pragma once

#include <QColor>
#include <QWidget>

#include "Tools.h"

class QButtonGroup;
class QMenu;
class QToolButton;
class FillStrokeIndicator;

class ToolPalette : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPalette(QWidget *parent = nullptr);

    void setCurrentTool(Tool tool);
    void setFillColor(const QColor &color);
    void setStrokeColor(const QColor &color);

signals:
    void toolSelected(Tool tool);
    void toolDoubleClicked(Tool tool);
    void fillColorClicked();
    void strokeColorClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setShapeTool(Tool tool);

    QButtonGroup *m_group = nullptr;
    QToolButton *m_shapeButton = nullptr;
    QMenu *m_shapeMenu = nullptr;
    Tool m_shapeTool = Tool::Rectangle;
    FillStrokeIndicator *m_indicator = nullptr;
    QColor m_fillColor = QColor("#4f7cc0");
    QColor m_strokeColor = QColor("#d7dbe2");
};
