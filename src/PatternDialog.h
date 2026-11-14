#pragma once

#include <QDialog>
#include <QVector>

#include "Document.h"

class CanvasView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class PatternPreview;

// Non-modal style editor for a pattern: repeat type, tile, spacing, offset,
// angle, scale, count, mirroring, seamless check and live preview.
class PatternDialog : public QDialog
{
    Q_OBJECT

public:
    PatternDialog(CanvasView *view, const PatternDefinition &pattern,
                  QWidget *parent = nullptr);

private:
    void buildUi();
    void updateFromFields();
    void updatePreview();
    void checkSeamless();
    void applyToSelection();
    void saveToLibrary();
    void expandToObjects();
    PatternDefinition currentPattern() const;

    CanvasView *m_view = nullptr;
    PatternDefinition m_pattern;
    QVector<PatternShape> m_shapes;

    QLineEdit *m_name = nullptr;
    QComboBox *m_type = nullptr;
    QDoubleSpinBox *m_tileW = nullptr;
    QDoubleSpinBox *m_tileH = nullptr;
    QDoubleSpinBox *m_spacingX = nullptr;
    QDoubleSpinBox *m_spacingY = nullptr;
    QDoubleSpinBox *m_offsetX = nullptr;
    QDoubleSpinBox *m_offsetY = nullptr;
    QDoubleSpinBox *m_angle = nullptr;
    QDoubleSpinBox *m_scale = nullptr;
    QSpinBox *m_count = nullptr;
    QDoubleSpinBox *m_pathStep = nullptr;
    QCheckBox *m_mirrorX = nullptr;
    QCheckBox *m_mirrorY = nullptr;
    QLabel *m_seamlessLabel = nullptr;
    PatternPreview *m_preview = nullptr;
    bool m_updating = false;
};
