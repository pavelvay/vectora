#include "PatternDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "CanvasView.h"
#include "PatternStore.h"
#include "Patterns.h"

class PatternPreview : public QWidget
{
public:
    explicit PatternPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(220, 220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPattern(const PatternSettings &settings,
                    const QVector<PatternShape> &shapes)
    {
        m_settings = settings;
        m_shapes = shapes;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(0x20, 0x22, 0x26));

        const QRectF surface = rect().adjusted(8, 8, -8, -8);
        painter.setPen(QPen(QColor(0x3a, 0x3f, 0x47), 1.0));
        painter.setBrush(QColor(0xf5, 0xf7, 0xfa));
        painter.drawRoundedRect(surface, 6.0, 6.0);

        if (m_shapes.isEmpty())
            return;

        const QImage preview = renderPatternPreview(m_settings, m_shapes,
                                                   QSize(int(surface.width()) * 2,
                                                         int(surface.height()) * 2));
        painter.setClipRect(surface);
        painter.drawImage(surface, preview);
    }

private:
    PatternSettings m_settings;
    QVector<PatternShape> m_shapes;
};

namespace {

QLabel *formLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    return label;
}

QDoubleSpinBox *makeSpin(QWidget *parent, double minimum, double maximum,
                         double value, const QString &suffix,
                         int decimals = 1)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setValue(value);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(96);
    return spin;
}

} // namespace

PatternDialog::PatternDialog(CanvasView *view, const PatternDefinition &pattern,
                             QWidget *parent)
    : QDialog(parent)
    , m_view(view)
    , m_pattern(pattern)
{
    setWindowTitle(QStringLiteral("Узор — %1").arg(m_pattern.name));
    resize(780, 560);
    buildUi();
    m_shapes = bakePatternShapes(m_pattern);
    updatePreview();
}

void PatternDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(14);

    m_preview = new PatternPreview(this);
    columns->addWidget(m_preview, 2);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);

    m_name = new QLineEdit(m_pattern.name, this);
    form->addRow(formLabel(QStringLiteral("Название")), m_name);

    m_type = new QComboBox(this);
    m_type->addItems(patternTypeNames());
    m_type->setCurrentIndex(qBound(0, m_pattern.settings.type, 6));
    form->addRow(formLabel(QStringLiteral("Повторение")), m_type);

    auto *tileRow = new QWidget(this);
    auto *tileLayout = new QHBoxLayout(tileRow);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(4);
    m_tileW = makeSpin(tileRow, 4.0, 4000.0, m_pattern.settings.tile.width(),
                       QStringLiteral(" px"));
    m_tileH = makeSpin(tileRow, 4.0, 4000.0, m_pattern.settings.tile.height(),
                       QStringLiteral(" px"));
    tileLayout->addWidget(m_tileW);
    tileLayout->addWidget(new QLabel(QStringLiteral("×"), tileRow));
    tileLayout->addWidget(m_tileH);
    form->addRow(formLabel(QStringLiteral("Ячейка")), tileRow);

    auto *spacingRow = new QWidget(this);
    auto *spacingLayout = new QHBoxLayout(spacingRow);
    spacingLayout->setContentsMargins(0, 0, 0, 0);
    spacingLayout->setSpacing(4);
    m_spacingX = makeSpin(spacingRow, -400.0, 400.0, m_pattern.settings.spacingX,
                          QStringLiteral(" px"));
    m_spacingY = makeSpin(spacingRow, -400.0, 400.0, m_pattern.settings.spacingY,
                          QStringLiteral(" px"));
    spacingLayout->addWidget(m_spacingX);
    spacingLayout->addWidget(m_spacingY);
    form->addRow(formLabel(QStringLiteral("Интервал X / Y")), spacingRow);

    auto *offsetRow = new QWidget(this);
    auto *offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetLayout->setSpacing(4);
    m_offsetX = makeSpin(offsetRow, -1000.0, 1000.0, m_pattern.settings.offsetX,
                         QStringLiteral(" px"));
    m_offsetY = makeSpin(offsetRow, -1000.0, 1000.0, m_pattern.settings.offsetY,
                         QStringLiteral(" px"));
    offsetLayout->addWidget(m_offsetX);
    offsetLayout->addWidget(m_offsetY);
    form->addRow(formLabel(QStringLiteral("Смещение X / Y")), offsetRow);

    m_angle = makeSpin(this, -360.0, 360.0, m_pattern.settings.angle,
                       QStringLiteral(" °"));
    form->addRow(formLabel(QStringLiteral("Угол повторения")), m_angle);

    m_scale = makeSpin(this, 0.1, 8.0, m_pattern.settings.scale, QStringLiteral(" ×"),
                       2);
    form->addRow(formLabel(QStringLiteral("Масштаб элементов")), m_scale);

    m_count = new QSpinBox(this);
    m_count->setRange(1, 64);
    m_count->setValue(qBound(1, m_pattern.settings.count, 64));
    form->addRow(formLabel(QStringLiteral("Количество (радиус/кривая)")), m_count);

    m_pathStep = makeSpin(this, 4.0, 800.0, m_pattern.settings.pathStep,
                          QStringLiteral(" px"));
    form->addRow(formLabel(QStringLiteral("Шаг по кривой")), m_pathStep);

    auto *mirrorRow = new QWidget(this);
    auto *mirrorLayout = new QHBoxLayout(mirrorRow);
    mirrorLayout->setContentsMargins(0, 0, 0, 0);
    m_mirrorX = new QCheckBox(QStringLiteral("по X"), mirrorRow);
    m_mirrorY = new QCheckBox(QStringLiteral("по Y"), mirrorRow);
    m_mirrorX->setChecked(m_pattern.settings.mirrorX);
    m_mirrorY->setChecked(m_pattern.settings.mirrorY);
    mirrorLayout->addWidget(m_mirrorX);
    mirrorLayout->addWidget(m_mirrorY);
    form->addRow(formLabel(QStringLiteral("Симметрия")), mirrorRow);

    m_seamlessLabel = new QLabel(
        QStringLiteral("Нажмите «Проверить бесшовность»."), this);
    m_seamlessLabel->setWordWrap(true);
    m_seamlessLabel->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    form->addRow(formLabel(QStringLiteral("Шов")), m_seamlessLabel);

    columns->addLayout(form, 3);
    root->addLayout(columns, 1);

    auto *actions = new QHBoxLayout;
    auto *check = new QToolButton(this);
    check->setText(QStringLiteral("Проверить бесшовность"));
    actions->addWidget(check);
    actions->addStretch(1);

    auto *library = new QToolButton(this);
    library->setText(QStringLiteral("В библиотеку"));
    actions->addWidget(library);

    auto *expand = new QToolButton(this);
    expand->setText(QStringLiteral("Разобрать на объекты"));
    actions->addWidget(expand);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *apply = buttons->addButton(QStringLiteral("Применить к выделению"),
                                     QDialogButtonBox::AcceptRole);
    root->addLayout(actions);
    root->addWidget(buttons);

    const auto changed = [this]() { updateFromFields(); };
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_updating)
            return;
        m_pattern.name = m_name->text().trimmed();
        setWindowTitle(QStringLiteral("Узор — %1").arg(m_pattern.name));
    });
    connect(m_type, &QComboBox::currentIndexChanged, this,
            [this, changed](int) { changed(); });
    for (QDoubleSpinBox *spin : {m_tileW, m_tileH, m_spacingX, m_spacingY,
                                 m_offsetX, m_offsetY, m_angle, m_scale,
                                 m_pathStep}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [changed](double) {
            changed();
        });
    }
    connect(m_count, &QSpinBox::valueChanged, this, [changed](int) { changed(); });
    connect(m_mirrorX, &QCheckBox::toggled, this, [changed](bool) { changed(); });
    connect(m_mirrorY, &QCheckBox::toggled, this, [changed](bool) { changed(); });
    connect(check, &QToolButton::clicked, this, &PatternDialog::checkSeamless);
    connect(library, &QToolButton::clicked, this, &PatternDialog::saveToLibrary);
    connect(expand, &QToolButton::clicked, this, &PatternDialog::expandToObjects);
    connect(apply, &QPushButton::clicked, this, &PatternDialog::applyToSelection);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PatternDialog::updateFromFields()
{
    if (m_updating)
        return;
    m_pattern.name = m_name->text().trimmed();
    m_pattern.settings.type = m_type->currentIndex();
    m_pattern.settings.tile = QSizeF(m_tileW->value(), m_tileH->value());
    m_pattern.settings.spacingX = m_spacingX->value();
    m_pattern.settings.spacingY = m_spacingY->value();
    m_pattern.settings.offsetX = m_offsetX->value();
    m_pattern.settings.offsetY = m_offsetY->value();
    m_pattern.settings.angle = m_angle->value();
    m_pattern.settings.scale = m_scale->value();
    m_pattern.settings.count = m_count->value();
    m_pattern.settings.pathStep = m_pathStep->value();
    m_pattern.settings.mirrorX = m_mirrorX->isChecked();
    m_pattern.settings.mirrorY = m_mirrorY->isChecked();

    const bool radial = m_pattern.settings.type == 4 || m_pattern.settings.type == 5;
    const bool alongPath = m_pattern.settings.type == 6;
    m_count->setEnabled(radial || alongPath);
    m_pathStep->setEnabled(alongPath);
    m_mirrorX->setEnabled(m_pattern.settings.type != 3);
    m_mirrorY->setEnabled(m_pattern.settings.type != 3);

    // Rebake only when the tile size changed; otherwise keep the shapes.
    m_shapes = bakePatternShapes(m_pattern);
    updatePreview();
}

void PatternDialog::updatePreview()
{
    if (!m_preview)
        return;
    m_preview->setPattern(m_pattern.settings, m_shapes);
    m_seamlessLabel->setText(QStringLiteral("Нажмите «Проверить бесшовность»."));
    m_seamlessLabel->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
}

void PatternDialog::checkSeamless()
{
    qreal difference = 0.0;
    const bool seamless = patternIsSeamless(m_pattern.settings, m_shapes, &difference);
    if (seamless) {
        m_seamlessLabel->setText(
            QStringLiteral("Узор бесшовный (разница краёв %1 %).")
                .arg(difference * 100.0, 0, 'f', 1));
        m_seamlessLabel->setStyleSheet(
            QStringLiteral("color:#06d6a0; font-size:11px;"));
    } else {
        m_seamlessLabel->setText(
            QStringLiteral("На стыке виден шов (разница %1 %). Сдвиньте элементы "
                           "или включите интервал.")
                .arg(difference * 100.0, 0, 'f', 1));
        m_seamlessLabel->setStyleSheet(
            QStringLiteral("color:#ffb547; font-size:11px;"));
    }
}

PatternDefinition PatternDialog::currentPattern() const
{
    PatternDefinition definition = m_pattern;
    definition.settings.type = m_type->currentIndex();
    definition.settings.tile = QSizeF(m_tileW->value(), m_tileH->value());
    definition.settings.spacingX = m_spacingX->value();
    definition.settings.spacingY = m_spacingY->value();
    definition.settings.offsetX = m_offsetX->value();
    definition.settings.offsetY = m_offsetY->value();
    definition.settings.angle = m_angle->value();
    definition.settings.scale = m_scale->value();
    definition.settings.count = m_count->value();
    definition.settings.pathStep = m_pathStep->value();
    definition.settings.mirrorX = m_mirrorX->isChecked();
    definition.settings.mirrorY = m_mirrorY->isChecked();
    if (!definition.name.trimmed().isEmpty())
        definition.name = definition.name.trimmed();
    return definition;
}

void PatternDialog::applyToSelection()
{
    if (!m_view) {
        reject();
        return;
    }
    PatternDefinition definition = currentPattern();
    if (definition.name.isEmpty())
        definition.name = QStringLiteral("Узор");

    bool applied = false;
    if (definition.id > 0 && m_view->patternDefinition(definition.id).id > 0) {
        m_view->updatePatternDefinition(definition);
        applied = m_view->applyPatternToSelection(definition.id);
    } else {
        applied = m_view->applyPatternDefinitionToSelection(definition);
    }
    if (!applied) {
        QMessageBox::information(
            this, QStringLiteral("Узор"),
            QStringLiteral("Выделите фигуру с замкнутым контуром, чтобы применить узор."));
        return;
    }
    accept();
}

void PatternDialog::saveToLibrary()
{
    PatternDefinition definition = currentPattern();
    definition.id = 0;
    PatternStore &store = PatternStore::instance();
    store.load();
    if (store.add(definition) >= 0) {
        m_seamlessLabel->setText(QStringLiteral("Узор сохранён в библиотеку."));
        m_seamlessLabel->setStyleSheet(
            QStringLiteral("color:#06d6a0; font-size:11px;"));
    }
}

void PatternDialog::expandToObjects()
{
    if (!m_view) {
        reject();
        return;
    }
    // Apply first so the selected object definitely carries this pattern.
    PatternDefinition definition = currentPattern();
    if (definition.id > 0 && m_view->patternDefinition(definition.id).id > 0) {
        m_view->updatePatternDefinition(definition);
        m_view->applyPatternToSelection(definition.id);
    } else if (!m_view->applyPatternDefinitionToSelection(definition)) {
        return;
    }
    m_view->expandSelectedPatterns();
    accept();
}
