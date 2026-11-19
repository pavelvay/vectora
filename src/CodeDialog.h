#pragma once

#include <QDialog>
#include <QImage>
#include <QVector>

#include "CodeGenerator.h"
#include "Document.h"

class CanvasView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;
class VectorItem;
class CodePreview;

// Editor for QR codes, linear barcodes and Data Matrix symbols. The preview is
// rendered from the same module data stored in the document object.
class CodeDialog : public QDialog
{
    Q_OBJECT

public:
    CodeDialog(CanvasView *view, VectorItem *item = nullptr,
               QWidget *parent = nullptr);

    void setKind(CodeKind kind);

private:
    void buildUi();
    void loadItem();
    void regenerate();
    void updatePreview();
    void apply();

    CanvasView *m_view = nullptr;
    VectorItem *m_item = nullptr;
    CodeSymbol m_symbol;

    QComboBox *m_kind = nullptr;
    QPlainTextEdit *m_data = nullptr;
    QComboBox *m_ecc = nullptr;
    QSpinBox *m_margin = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QToolButton *m_foreground = nullptr;
    QToolButton *m_background = nullptr;
    QToolButton *m_logoButton = nullptr;
    QToolButton *m_logoClear = nullptr;
    QDoubleSpinBox *m_logoScale = nullptr;
    QCheckBox *m_showText = nullptr;
    QLabel *m_status = nullptr;
    CodePreview *m_preview = nullptr;
    QColor m_color = QColor(0, 0, 0);
    QColor m_backgroundColor = QColor(255, 255, 255);
    QImage m_logo;
    bool m_updating = false;
};
