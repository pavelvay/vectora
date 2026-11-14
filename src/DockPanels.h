#pragma once

#include <QColor>

#include "Document.h"
#include <QList>
#include <QObject>

class QCheckBox;
class QComboBox;
class QEvent;
class QTimer;
class QDockWidget;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QMainWindow;
class QSlider;
class QSpinBox;
class QToolButton;
class QTreeWidget;
class QWidget;

class CanvasView;
class VectorItem;

class DockPanels : public QObject
{
    Q_OBJECT

public:
    DockPanels(QMainWindow *host, CanvasView *view, QObject *parent = nullptr);

    QList<QDockWidget *> docks() const { return m_docks; }
    QDockWidget *propertiesDock() const { return m_propertiesDock; }
    void refresh();
    void openGradientDialog();

signals:
    void beginEdit();
    void endEdit();
    void status(const QString &text);

private:
    QDockWidget *createDock(const QString &title, QWidget *content);
    void buildObjectsDocker();
    void buildPropertiesDocker();
    void buildColorDocker();
    void buildAlignDocker();
    void buildEffectsDocker();
    void buildOutlineDocker();
    void buildTextDocker();

    QList<VectorItem *> selection() const;
    bool eventFilter(QObject *watched, QEvent *event) override;

    void refreshTree();
    void refreshProperties();
    void refreshEffects();
    void refreshText();
    void updateColorButtons();
    void updateColorPreview();

    void applyTransform();
    void applyColor(const QColor &color, bool forFill);
    void applyAlign(int mode);
    void distribute(bool horizontal);
    void applyShadow();
    void applyText();
    void beginOpacityEdit();
    void finishOpacityEdit();
    void applyOpacityLive(int percent);

    QMainWindow *m_host = nullptr;
    CanvasView *m_view = nullptr;
    QList<QDockWidget *> m_docks;
    QDockWidget *m_propertiesDock = nullptr;

    QTreeWidget *m_tree = nullptr;
    QComboBox *m_layerTarget = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QWidget *m_propertiesContent = nullptr;

    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
    QDoubleSpinBox *m_angle = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QToolButton *m_fillButton = nullptr;
    QToolButton *m_strokeButton = nullptr;
    QDoubleSpinBox *m_strokeWidth = nullptr;

    QLabel *m_colorPreview = nullptr;
    QSlider *m_hue = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_value = nullptr;
    QSlider *m_alpha = nullptr;
    QLineEdit *m_hex = nullptr;
    QColor m_color = QColor(0x4f, 0x7c, 0xc0);
    QColor m_gradientColor2 = QColor(0xff, 0xff, 0xff);

    QComboBox *m_alignTarget = nullptr;

    QCheckBox *m_shadowEnabled = nullptr;
    QDoubleSpinBox *m_shadowX = nullptr;
    QDoubleSpinBox *m_shadowY = nullptr;
    QDoubleSpinBox *m_shadowOpacity = nullptr;
    QToolButton *m_shadowColorButton = nullptr;
    QColor m_shadowColor = QColor(0, 0, 0, 180);
    QSlider *m_effectOpacity = nullptr;
    QLabel *m_effectOpacityValue = nullptr;
    QSlider *m_fillOpacity = nullptr;
    QSlider *m_strokeOpacity = nullptr;
    QComboBox *m_blendMode = nullptr;
    QSpinBox *m_blendSteps = nullptr;

    QToolButton *m_outlineColorButton = nullptr;
    QDoubleSpinBox *m_outlineWidth = nullptr;
    QComboBox *m_capCombo = nullptr;
    QComboBox *m_joinCombo = nullptr;
    QDoubleSpinBox *m_miterSpin = nullptr;
    QLineEdit *m_dashEdit = nullptr;
    QColor m_outlineColor = QColor(0x1d, 0x21, 0x27);

    QWidget *m_textContent = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QDoubleSpinBox *m_fontSize = nullptr;
    QDoubleSpinBox *m_letterSpacing = nullptr;
    QDoubleSpinBox *m_lineSpacing = nullptr;
    QToolButton *m_boldButton = nullptr;
    QToolButton *m_italicButton = nullptr;
    QToolButton *m_underlineButton = nullptr;
    QToolButton *m_alignButtons[3] = {nullptr, nullptr, nullptr};
    int m_textAlign = 0;

    bool m_updating = false;
    QTimer *m_colorTimer = nullptr;
    bool m_colorEditActive = false;
    QTimer *m_opacityTimer = nullptr;
    bool m_opacityEditActive = false;
};
