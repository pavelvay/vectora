#pragma once

#include <QWidget>

class CanvasView;

class Ruler : public QWidget
{
    Q_OBJECT

public:
    explicit Ruler(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setView(CanvasView *view);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Qt::Orientation m_orientation;
    CanvasView *m_view = nullptr;
};
