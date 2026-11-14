#pragma once

#include <QColor>
#include <QDialog>
#include <QSizeF>

#include "Document.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;

class PageDialog : public QDialog
{
public:
    explicit PageDialog(QWidget *parent = nullptr);

    void setPageInfo(const PageInfo &info);

    QSizeF pageSizePx() const;
    QString unitName() const;
    QColor pageBackground() const { return m_background; }
    qreal marginPx() const;
    qreal bleedPx() const;

private:
    void applyPreset();
    void changeUnit();
    void changeOrientation();
    void clampCustom();
    void updateColorButton();

    QComboBox *m_preset = nullptr;
    QComboBox *m_orientation = nullptr;
    QComboBox *m_units = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QDoubleSpinBox *m_margin = nullptr;
    QDoubleSpinBox *m_bleed = nullptr;
    QPushButton *m_backgroundButton = nullptr;
    QColor m_background = QColor(Qt::white);
    bool m_updating = false;
    QString m_lastUnit = QStringLiteral("мм");
};
