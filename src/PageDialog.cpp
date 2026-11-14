#include "PageDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

constexpr qreal kMmToPx = 96.0 / 25.4;

qreal unitFactor(const QString &unit)
{
    if (unit == QStringLiteral("мм"))
        return kMmToPx;
    if (unit == QStringLiteral("см"))
        return kMmToPx * 10.0;
    if (unit == QStringLiteral("in"))
        return 96.0;
    return 1.0;
}

} // namespace

PageDialog::PageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Новый документ"));
    setModal(true);
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_preset = new QComboBox(this);
    m_preset->addItems({QStringLiteral("A4 — 210 × 297 мм"),
                        QStringLiteral("A3 — 297 × 420 мм"),
                        QStringLiteral("A2 — 420 × 594 мм"),
                        QStringLiteral("1920 × 1080 px"),
                        QStringLiteral("Свой размер")});
    form->addRow(QStringLiteral("Формат"), m_preset);

    m_orientation = new QComboBox(this);
    m_orientation->addItems({QStringLiteral("Книжная"),
                             QStringLiteral("Альбомная")});
    form->addRow(QStringLiteral("Ориентация"), m_orientation);

    m_units = new QComboBox(this);
    m_units->addItems({QStringLiteral("px"), QStringLiteral("мм"),
                       QStringLiteral("см"), QStringLiteral("in")});
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

    auto *hint = new QLabel(QStringLiteral(
        "A4, A3 и A2 заданы в миллиметрах. Можно выбрать пиксели, "
        "сантиметры или дюймы и ввести свой размер."),
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
    connect(m_width, &QDoubleSpinBox::editingFinished, this,
            [this]() { clampCustom(); });
    connect(m_height, &QDoubleSpinBox::editingFinished, this,
            [this]() { clampCustom(); });

    applyPreset();
}

void PageDialog::applyPreset()
{
    m_updating = true;
    const int preset = m_preset->currentIndex();
    const QString unit = m_units->currentText();
    const qreal factor = unitFactor(unit);

    qreal widthPx = 794.0;
    qreal heightPx = 1123.0;
    bool landscape = false;
    if (preset == 1) {
        widthPx = 297.0 * kMmToPx;
        heightPx = 420.0 * kMmToPx;
    } else if (preset == 2) {
        widthPx = 420.0 * kMmToPx;
        heightPx = 594.0 * kMmToPx;
    } else if (preset == 3) {
        widthPx = 1920.0;
        heightPx = 1080.0;
        landscape = true;
    } else if (preset == 4) {
        widthPx = m_width->value() * factor;
        heightPx = m_height->value() * factor;
        landscape = widthPx > heightPx;
    } else {
        widthPx = 210.0 * kMmToPx;
        heightPx = 297.0 * kMmToPx;
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
    m_width->setEnabled(preset == 4);
    m_height->setEnabled(preset == 4);
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
    m_width->setSuffix(QStringLiteral(" ") + unit);
    m_height->setSuffix(QStringLiteral(" ") + unit);
    m_width->setValue(widthPx / newFactor);
    m_height->setValue(heightPx / newFactor);
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
    if (m_preset->currentIndex() != 4)
        m_preset->setCurrentIndex(4);
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
