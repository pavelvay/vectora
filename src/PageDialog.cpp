#include "PageDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

constexpr qreal kMmToPx = 96.0 / 25.4;

qreal unitFactor(const QString &unit)
{
    if (unit == QLatin1String("мм"))
        return kMmToPx;
    if (unit == QLatin1String("см"))
        return kMmToPx * 10.0;
    if (unit == QLatin1String("in"))
        return 96.0;
    if (unit == QLatin1String("pt"))
        return 96.0 / 72.0;
    return 1.0;
}

struct Preset {
    const char *name;
    qreal widthMm;
    qreal heightMm;
    bool pixels;
};

const Preset kPresets[] = {
    {"A4 — 210 × 297 мм", 210.0, 297.0, false},
    {"A3 — 297 × 420 мм", 297.0, 420.0, false},
    {"A2 — 420 × 594 мм", 420.0, 594.0, false},
    {"A1 — 594 × 841 мм", 594.0, 841.0, false},
    {"A0 — 841 × 1189 мм", 841.0, 1189.0, false},
    {"A5 — 148 × 210 мм", 148.0, 210.0, false},
    {"A6 — 105 × 148 мм", 105.0, 148.0, false},
    {"Letter — 8.5 × 11 in", 215.9, 279.4, false},
    {"Legal — 8.5 × 14 in", 215.9, 355.6, false},
    {"Tabloid — 11 × 17 in", 279.4, 431.8, false},
    {"1920 × 1080 px", 1920.0, 1080.0, true},
    {"Свой размер", 0.0, 0.0, false},
};

constexpr int kCustomPreset = int(sizeof(kPresets) / sizeof(kPresets[0])) - 1;

} // namespace

PageDialog::PageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Страница"));
    setModal(true);
    setMinimumWidth(400);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_preset = new QComboBox(this);
    for (const Preset &preset : kPresets)
        m_preset->addItem(QString::fromUtf8(preset.name));
    form->addRow(QStringLiteral("Формат"), m_preset);

    m_orientation = new QComboBox(this);
    m_orientation->addItems({QStringLiteral("Книжная"),
                             QStringLiteral("Альбомная")});
    form->addRow(QStringLiteral("Ориентация"), m_orientation);

    m_units = new QComboBox(this);
    m_units->addItems({QStringLiteral("px"), QStringLiteral("мм"),
                       QStringLiteral("см"), QStringLiteral("in"),
                       QStringLiteral("pt")});
    m_units->setCurrentText(QStringLiteral("мм"));
    form->addRow(QStringLiteral("Единицы"), m_units);

    m_width = new QDoubleSpinBox(this);
    m_width->setRange(1, 20000);
    m_width->setDecimals(1);
    m_width->setSuffix(QStringLiteral(" мм"));
    m_height = new QDoubleSpinBox(this);
    m_height->setRange(1, 20000);
    m_height->setDecimals(1);
    m_height->setSuffix(QStringLiteral(" мм"));
    form->addRow(QStringLiteral("Ширина"), m_width);
    form->addRow(QStringLiteral("Высота"), m_height);

    m_margin = new QDoubleSpinBox(this);
    m_margin->setRange(0, 200);
    m_margin->setDecimals(1);
    m_margin->setValue(10.0);
    m_margin->setSuffix(QStringLiteral(" мм"));
    m_bleed = new QDoubleSpinBox(this);
    m_bleed->setRange(0, 50);
    m_bleed->setDecimals(1);
    m_bleed->setValue(3.0);
    m_bleed->setSuffix(QStringLiteral(" мм"));
    form->addRow(QStringLiteral("Поля"), m_margin);
    form->addRow(QStringLiteral("Вылеты под обрез"), m_bleed);

    m_backgroundButton = new QPushButton(this);
    m_backgroundButton->setStyleSheet(QStringLiteral("text-align:left; padding:4px;"));
    updateColorButton();
    form->addRow(QStringLiteral("Цвет фона"), m_backgroundButton);

    auto *hint = new QLabel(QStringLiteral(
        "A0–A6, Letter, Legal и Tabloid заданы в миллиметрах. Можно выбрать "
        "пиксели, миллиметры, сантиметры, дюймы или пункты."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_preset, &QComboBox::currentIndexChanged, this,
            [this](int) { applyPreset(); });
    connect(m_units, &QComboBox::currentTextChanged, this,
            [this](const QString &) { changeUnit(); });
    connect(m_orientation, &QComboBox::currentIndexChanged, this,
            [this](int) { changeOrientation(); });
    connect(m_width, &QDoubleSpinBox::valueChanged, this,
            [this](double) { clampCustom(); });
    connect(m_height, &QDoubleSpinBox::valueChanged, this,
            [this](double) { clampCustom(); });
    connect(m_backgroundButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
            m_background, this, QStringLiteral("Цвет фона страницы"));
        if (picked.isValid()) {
            m_background = picked;
            updateColorButton();
        }
    });

    applyPreset();
}

void PageDialog::setPageInfo(const PageInfo &info)
{
    m_updating = true;
    m_background = info.background;
    updateColorButton();

    const qreal factor = unitFactor(m_lastUnit);
    m_width->setValue(info.size.width() / factor);
    m_height->setValue(info.size.height() / factor);
    {
        const QSignalBlocker blocker(m_orientation);
        m_orientation->setCurrentIndex(info.size.width() > info.size.height() ? 1 : 0);
    }
    m_margin->setValue(info.margin / factor);
    m_bleed->setValue(info.bleed / factor);

    int preset = kCustomPreset;
    for (int i = 0; i < kCustomPreset; ++i) {
        const Preset &entry = kPresets[i];
        const qreal widthPx = entry.pixels ? entry.widthMm
                                           : entry.widthMm * kMmToPx;
        const qreal heightPx = entry.pixels ? entry.heightMm
                                            : entry.heightMm * kMmToPx;
        const qreal w = qMax(widthPx, heightPx);
        const qreal h = qMin(widthPx, heightPx);
        const qreal currentW = qMax(info.size.width(), info.size.height());
        const qreal currentH = qMin(info.size.width(), info.size.height());
        if (qAbs(w - currentW) < 2.0 && qAbs(h - currentH) < 2.0) {
            preset = i;
            break;
        }
    }
    {
        const QSignalBlocker blocker(m_preset);
        m_preset->setCurrentIndex(preset);
    }
    m_width->setEnabled(true);
    m_height->setEnabled(true);
    m_updating = false;
}

void PageDialog::updateColorButton()
{
    m_backgroundButton->setText(m_background.name(QColor::HexRgb).toUpper());
    m_backgroundButton->setStyleSheet(
        QStringLiteral("text-align:left; padding:4px; background:%1; color:%2;")
            .arg(m_background.name(),
                 m_background.lightness() > 128 ? QStringLiteral("#111111")
                                                : QStringLiteral("#f2f4f8")));
}

void PageDialog::applyPreset()
{
    if (m_updating)
        return;
    m_updating = true;
    const int preset = m_preset->currentIndex();
    const QString unit = m_units->currentText();
    const qreal factor = unitFactor(unit);

    qreal widthPx = 794.0;
    qreal heightPx = 1123.0;
    bool landscape = false;
    if (preset >= 0 && preset < kCustomPreset) {
        const Preset &entry = kPresets[preset];
        widthPx = entry.pixels ? entry.widthMm : entry.widthMm * kMmToPx;
        heightPx = entry.pixels ? entry.heightMm : entry.heightMm * kMmToPx;
        landscape = widthPx > heightPx;
    } else {
        widthPx = m_width->value() * factor;
        heightPx = m_height->value() * factor;
        landscape = widthPx > heightPx;
    }

    {
        const QSignalBlocker blocker(m_orientation);
        m_orientation->setCurrentIndex(landscape ? 1 : 0);
    }
    if (landscape && widthPx < heightPx)
        std::swap(widthPx, heightPx);
    else if (!landscape && widthPx > heightPx)
        std::swap(widthPx, heightPx);

    m_width->setValue(widthPx / factor);
    m_height->setValue(heightPx / factor);
    m_width->setSuffix(QStringLiteral(" ") + unit);
    m_height->setSuffix(QStringLiteral(" ") + unit);
    m_margin->setSuffix(QStringLiteral(" ") + unit);
    m_bleed->setSuffix(QStringLiteral(" ") + unit);
    m_width->setEnabled(true);
    m_height->setEnabled(true);
    m_lastUnit = unit;
    m_updating = false;
}

void PageDialog::changeUnit()
{
    if (m_updating)
        return;

    m_updating = true;
    const QString unit = m_units->currentText();
    const qreal oldFactor = unitFactor(m_lastUnit);
    const qreal newFactor = unitFactor(unit);
    const qreal widthPx = m_width->value() * oldFactor;
    const qreal heightPx = m_height->value() * oldFactor;
    const qreal marginPx = m_margin->value() * oldFactor;
    const qreal bleedPx = m_bleed->value() * oldFactor;
    m_width->setSuffix(QStringLiteral(" ") + unit);
    m_height->setSuffix(QStringLiteral(" ") + unit);
    m_margin->setSuffix(QStringLiteral(" ") + unit);
    m_bleed->setSuffix(QStringLiteral(" ") + unit);
    m_width->setValue(widthPx / newFactor);
    m_height->setValue(heightPx / newFactor);
    m_margin->setValue(marginPx / newFactor);
    m_bleed->setValue(bleedPx / newFactor);
    m_lastUnit = unit;
    m_updating = false;
}

void PageDialog::changeOrientation()
{
    if (m_updating)
        return;
    m_updating = true;
    const qreal width = m_width->value();
    const qreal height = m_height->value();
    m_width->setValue(height);
    m_height->setValue(width);
    m_updating = false;
}

void PageDialog::clampCustom()
{
    if (m_updating)
        return;
    m_updating = true;
    if (m_preset->currentIndex() != kCustomPreset)
        m_preset->setCurrentIndex(kCustomPreset);
    m_width->setEnabled(true);
    m_height->setEnabled(true);
    m_updating = false;
}

QSizeF PageDialog::pageSizePx() const
{
    const qreal factor = unitFactor(m_units->currentText());
    return QSizeF(qBound(1.0, m_width->value() * factor, 40000.0),
                  qBound(1.0, m_height->value() * factor, 40000.0));
}

QString PageDialog::unitName() const
{
    return m_units->currentText();
}

qreal PageDialog::marginPx() const
{
    return m_margin->value() * unitFactor(m_units->currentText());
}

qreal PageDialog::bleedPx() const
{
    return m_bleed->value() * unitFactor(m_units->currentText());
}
