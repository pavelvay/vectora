#pragma once

#include <QColor>
#include <QWidget>

#include "Tools.h"

class QButtonGroup;
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
    QButtonGroup *m_group = nullptr;
    FillStrokeIndicator *m_indicator = nullptr;
    QColor m_fillColor = QColor("#4f7cc0");
    QColor m_strokeColor = QColor("#d7dbe2");
};
