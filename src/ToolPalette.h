#pragma once

#include <QWidget>

#include "Tools.h"

class QButtonGroup;

class ToolPalette : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPalette(QWidget *parent = nullptr);

    void setCurrentTool(Tool tool);

signals:
    void toolSelected(Tool tool);

private:
    QButtonGroup *m_group = nullptr;
};
