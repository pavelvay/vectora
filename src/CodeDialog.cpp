#include "CodeDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "CanvasView.h"
#include "VectorItem.h"

class CodePreview : public QWidget
{
public:
    explicit CodePreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(260, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setObject(const VectorObject &object)
    {
        m_object = object;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(0x20, 0x22, 0x26));

        const QRectF surface = rect().adjusted(10, 10, -10, -10);
        painter.setPen(QPen(QColor(0x3a, 0x3f, 0x47), 1.0));
        painter.setBrush(QColor(0xf5, 0xf7, 0xfa));
        painter.drawRoundedRect(surface, 6.0, 6.0);

        const QSizeF codeSize(m_object.textBox.width(),
                              m_object.textBox.height());
        if (codeSize.width() < 1.0 || codeSize.height() < 1.0)
            return;
        const qreal scale = qMin(surface.width() / codeSize.width(),
                                 surface.height() / codeSize.height());
        painter.save();
        painter.setClipRect(surface);
        painter.translate(surface.center());
        painter.scale(scale, scale);
        painter.translate(-codeSize.width() / 2.0, -codeSize.height() / 2.0);
        VectorObject object = m_object;
        object.pos = QPointF();
        Document::paintObject(&painter, object);
        painter.restore();
    }

private:
    VectorObject m_object;
};

namespace {

QLabel *dialogLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    return label;
}

QDoubleSpinBox *sizeSpin(QWidget *parent, double value)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(20.0, 4000.0);
    spin->setDecimals(0);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral(" px"));
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(88);
    return spin;
}

void styleColorButton(QToolButton *button, const QColor &color)
{
    button->setText(color.name(QColor::HexArgb).toUpper());
    button->setStyleSheet(QStringLiteral("text-align:left; padding:4px;"));
}

} // namespace

CodeDialog::CodeDialog(CanvasView *view, VectorItem *item, QWidget *parent)
    : QDialog(parent)
    , m_view(view)
    , m_item(item)
{
    setWindowTitle(item ? QStringLiteral("Редактировать код")
                        : QStringLiteral("Новый код"));
    resize(760, 520);
    buildUi();
    loadItem();
    regenerate();
}

void CodeDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(14);
    m_preview = new CodePreview(this);
    columns->addWidget(m_preview, 2);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);

    m_kind = new QComboBox(this);
    m_kind->addItems(codeKindNames());
    form->addRow(dialogLabel(QStringLiteral("Тип")), m_kind);

    m_data = new QPlainTextEdit(this);
    m_data->setPlaceholderText(QStringLiteral(
        "Текст, ссылка, контактные данные или цифры для штрихкода"));
    m_data->setFixedHeight(86);
    form->addRow(dialogLabel(QStringLiteral("Данные")), m_data);

    m_ecc = new QComboBox(this);
    m_ecc->addItems({QStringLiteral("L — 7 %"), QStringLiteral("M — 15 %"),
                     QStringLiteral("Q — 25 %"), QStringLiteral("H — 30 %")});
    m_ecc->setCurrentIndex(1);
    form->addRow(dialogLabel(QStringLiteral("Коррекция QR")), m_ecc);

    m_margin = new QSpinBox(this);
    m_margin->setRange(0, 20);
    m_margin->setValue(4);
    m_margin->setSuffix(QStringLiteral(" мод."));
    form->addRow(dialogLabel(QStringLiteral("Отступ")), m_margin);

    auto *sizeRow = new QWidget(this);
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(4);
    m_width = sizeSpin(sizeRow, 220);
    m_height = sizeSpin(sizeRow, 220);
    sizeLayout->addWidget(m_width);
    sizeLayout->addWidget(m_height);
    form->addRow(dialogLabel(QStringLiteral("Размер")), sizeRow);

    m_foreground = new QToolButton(this);
    styleColorButton(m_foreground, m_color);
    form->addRow(dialogLabel(QStringLiteral("Цвет кода")), m_foreground);

    m_background = new QToolButton(this);
    m_background->setText(QStringLiteral("Нет фона"));
    form->addRow(dialogLabel(QStringLiteral("Фон")), m_background);

    m_showText = new QCheckBox(QStringLiteral("Показывать цифры под штрихкодом"),
                               this);
    m_showText->setChecked(true);
    form->addRow(QString(), m_showText);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    form->addRow(m_status);

    columns->addLayout(form, 3);
    root->addLayout(columns, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *applyButton = buttons->addButton(QStringLiteral("Применить"),
                                           QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        const CodeKind kind = codeKindFromIndex(m_kind->currentIndex());
        m_ecc->setEnabled(kind == CodeKind::Qr);
        m_showText->setEnabled(codeShowsHumanText(kind));
        if (!m_item) {
            const bool twoDimensional = kind == CodeKind::Qr
                                        || kind == CodeKind::DataMatrix;
            m_width->setValue(twoDimensional ? 220.0 : 340.0);
            m_height->setValue(twoDimensional ? 220.0 : 130.0);
            m_margin->setValue(twoDimensional ? 4 : 9);
        }
        regenerate();
    });
    connect(m_data, &QPlainTextEdit::textChanged, this, [this]() { regenerate(); });
    connect(m_ecc, &QComboBox::currentIndexChanged, this, [this](int) {
        regenerate();
    });
    connect(m_margin, &QSpinBox::valueChanged, this, [this](int) { regenerate(); });
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this](double) {
        regenerate();
    });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this](double) {
        regenerate();
    });
    connect(m_showText, &QCheckBox::toggled, this, [this](bool) { regenerate(); });

    connect(m_foreground, &QToolButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
            m_color, this, QStringLiteral("Цвет кода"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return;
        m_color = picked;
        styleColorButton(m_foreground, m_color);
        regenerate();
    });
    connect(m_background, &QToolButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
            m_backgroundColor, this, QStringLiteral("Фон кода"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return;
        m_backgroundColor = picked;
        styleColorButton(m_background, m_backgroundColor);
        regenerate();
    });

    connect(applyButton, &QPushButton::clicked, this, &CodeDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CodeDialog::loadItem()
{
    if (!m_item) {
        m_kind->setCurrentIndex(0);
        m_data->setPlainText(QStringLiteral("https://"));
        m_width->setValue(220.0);
        m_height->setValue(220.0);
        return;
    }
    const VectorObject object = m_item->object();
    m_updating = true;
    m_kind->setCurrentIndex(qBound(0, object.codeKind, 6));
    m_data->setPlainText(object.codeData);
    m_ecc->setCurrentIndex(qBound(0, object.codeEcc, 3));
    m_margin->setValue(qBound(0, object.codeMargin, 20));
    m_width->setValue(object.textBox.width());
    m_height->setValue(object.textBox.height());
    m_color = object.codeColor;
    m_backgroundColor = object.codeBackground;
    m_showText->setChecked(object.codeShowText);
    styleColorButton(m_foreground, m_color);
    styleColorButton(m_background, m_backgroundColor);
    m_updating = false;
    m_ecc->setEnabled(object.codeKind == 0);
    const CodeKind kind = codeKindFromIndex(object.codeKind);
    m_showText->setEnabled(codeShowsHumanText(kind));
}

void CodeDialog::setKind(CodeKind kind)
{
    m_updating = true;
    m_kind->setCurrentIndex(static_cast<int>(kind));
    m_updating = false;
    const bool twoDimensional = kind == CodeKind::Qr || kind == CodeKind::DataMatrix;
    if (!m_item) {
        m_width->setValue(twoDimensional ? 220.0 : 340.0);
        m_height->setValue(twoDimensional ? 220.0 : 130.0);
        m_margin->setValue(twoDimensional ? 4 : 9);
    }
    m_ecc->setEnabled(kind == CodeKind::Qr);
    m_showText->setEnabled(codeShowsHumanText(kind));
    regenerate();
}

void CodeDialog::regenerate()
{
    if (m_updating)
        return;
    const CodeKind kind = codeKindFromIndex(m_kind->currentIndex());
    QString data = m_data->toPlainText();
    if (kind != CodeKind::Qr)
        data = data.trimmed();
    else
        data.chop(0);
    m_symbol = generateCode(kind, data, m_ecc->currentIndex());
    if (!m_symbol.valid) {
        m_status->setText(m_symbol.error);
        m_status->setStyleSheet(QStringLiteral("color:#ffb547; font-size:11px;"));
    } else {
        QString info;
        if (kind == CodeKind::Qr) {
            const int version = (m_symbol.modulesX - 17) / 4;
            info = QStringLiteral("QR версии %1 · %2×%2 модулей · коррекция %3")
                       .arg(version)
                       .arg(m_symbol.modulesX)
                       .arg(m_ecc->currentText().left(1));
        } else if (kind == CodeKind::DataMatrix) {
            info = QStringLiteral("Data Matrix %1×%1 модулей")
                       .arg(m_symbol.modulesX);
        } else {
            info = QStringLiteral("%1 · %2 модулей · %3")
                       .arg(m_symbol.humanText)
                       .arg(m_symbol.modulesX)
                       .arg(m_symbol.kind == CodeKind::Code39
                                ? QStringLiteral("Code 39")
                                : codeKindName(kind));
        }
        m_status->setText(info);
        m_status->setStyleSheet(QStringLiteral("color:#06d6a0; font-size:11px;"));
    }
    updatePreview();
}

void CodeDialog::updatePreview()
{
    if (!m_preview)
        return;
    VectorObject object;
    object.type = ShapeType::Code;
    object.codeKind = static_cast<int>(codeKindFromIndex(m_kind->currentIndex()));
    object.codeData = m_data->toPlainText();
    object.codeEcc = m_ecc->currentIndex();
    object.codeMargin = m_margin->value();
    object.codeColor = m_color;
    object.codeBackground = m_backgroundColor;
    object.codeShowText = m_showText->isChecked();
    object.codeHumanText = m_symbol.humanText;
    object.codeModulesX = m_symbol.modulesX;
    object.codeModulesY = m_symbol.modulesY;
    object.codeCells = m_symbol.cells;
    object.textBox = QRectF(0.0, 0.0, m_width->value(), m_height->value());
    m_preview->setObject(object);
}

void CodeDialog::apply()
{
    if (!m_symbol.valid) {
        m_status->setText(m_symbol.error.isEmpty()
                              ? QStringLiteral("Проверьте данные кода.")
                              : m_symbol.error);
        m_status->setStyleSheet(QStringLiteral("color:#ff6b8a; font-size:11px;"));
        return;
    }
    if (!m_view)
        return;

    VectorObject object;
    if (m_item)
        object = m_item->object();
    object.type = ShapeType::Code;
    object.codeKind = m_kind->currentIndex();
    object.codeData = m_symbol.data;
    object.codeEcc = m_ecc->currentIndex();
    object.codeMargin = m_margin->value();
    object.codeColor = m_color;
    object.codeBackground = m_backgroundColor;
    object.codeShowText = m_showText->isChecked();
    object.codeModulesX = m_symbol.modulesX;
    object.codeModulesY = m_symbol.modulesY;
    object.codeCells = m_symbol.cells;
    object.codeHumanText = m_symbol.humanText;
    object.textBox.setSize(QSizeF(m_width->value(), m_height->value()));
    object.hasFill = false;
    object.hasStroke = false;

    if (m_item) {
        m_view->updateCodeItem(m_item, object);
    } else {
        const QSizeF page = m_view->pageSize();
        object.pos = QPointF((page.width() - object.textBox.width()) / 2.0,
                             (page.height() - object.textBox.height()) / 2.0);
        object.page = m_view->currentPage();
        object.layer = m_view->activeLayer();
        m_view->addCodeObject(object);
    }
    accept();
}
