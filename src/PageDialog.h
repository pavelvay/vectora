#pragma once

#include <QDialog>
#include <QSizeF>

class QComboBox;
class QDoubleSpinBox;

class PageDialog : public QDialog
{
public:
    explicit PageDialog(QWidget *parent = nullptr);

    QSizeF pageSizePx() const;
    QString unitName() const;

private:
    void applyPreset();
    void changeUnit();
    void changeOrientation();
    void clampCustom();

    QComboBox *m_preset = nullptr;
    QComboBox *m_orientation = nullptr;
    QComboBox *m_units = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    bool m_updating = false;
    QString m_lastUnit = QStringLiteral("мм");
};
