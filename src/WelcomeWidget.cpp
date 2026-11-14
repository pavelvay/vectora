#include "WelcomeWidget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "Icons.h"

namespace {

class StartCard : public QFrame
{
public:
    StartCard(const QString &iconId, const QString &title, const QString &subtitle,
              QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("startCard"));
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(250, 158);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(6);

        auto *icon = new QLabel(this);
        icon->setPixmap(Icons::icon(iconId).pixmap(34, 34));
        icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto *titleLabel = new QLabel(title, this);
        titleLabel->setStyleSheet(
            QStringLiteral("color:#eef1f6; font-size:16px; font-weight:600;"));
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto *subtitleLabel = new QLabel(subtitle, this);
        subtitleLabel->setWordWrap(true);
        subtitleLabel->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
        subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        layout->addWidget(icon);
        layout->addSpacing(4);
        layout->addWidget(titleLabel);
        layout->addWidget(subtitleLabel);
        layout->addStretch(1);
    }

    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())
            && onClick) {
            onClick();
        }
        QFrame::mouseReleaseEvent(event);
    }
};

QLabel *mutedLabel(const QString &text, QWidget *parent, int size = 11)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(
        QStringLiteral("color:#7f8792; font-size:%1px;").arg(size));
    return label;
}

} // namespace

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("welcomePage"));

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(56, 44, 56, 40);
    root->setSpacing(44);

    // --- Left: identity and actions ---------------------------------------
    auto *left = new QVBoxLayout;
    left->setSpacing(0);

    auto *logoRow = new QHBoxLayout;
    logoRow->setSpacing(14);

    auto *logo = new QLabel(this);
    logo->setPixmap(Icons::appIcon().pixmap(96, 96));
    logoRow->addWidget(logo);

    auto *nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(2);
    auto *tagline = new QLabel(QStringLiteral("векторный редактор"), this);
    tagline->setStyleSheet(QStringLiteral("color:#aab2bd; font-size:14px;"));
    auto *platform = new QLabel(QStringLiteral("Linux · Windows · macOS"), this);
    platform->setStyleSheet(QStringLiteral("color:#6f7681; font-size:11px;"));
    nameColumn->addStretch(1);
    nameColumn->addWidget(tagline);
    nameColumn->addWidget(platform);
    nameColumn->addStretch(1);
    logoRow->addLayout(nameColumn);
    logoRow->addStretch(1);
    left->addLayout(logoRow);

    left->addSpacing(46);

    auto *heading = new QLabel(QStringLiteral("Начните новый проект"), this);
    heading->setStyleSheet(QStringLiteral("color:#e8ecf3; font-size:24px; font-weight:600;"));
    left->addWidget(heading);

    auto *subheading = new QLabel(
        QStringLiteral("Создайте документ A4 или откройте существующий проект. "
                       "Слои, векторные объекты и эффекты — внутри."),
        this);
    subheading->setWordWrap(true);
    subheading->setMaximumWidth(620);
    subheading->setStyleSheet(QStringLiteral("color:#8b929d; font-size:13px;"));
    left->addSpacing(8);
    left->addWidget(subheading);

    left->addSpacing(26);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(14);

    auto *newCard = new StartCard(QStringLiteral("new"),
                                  QStringLiteral("Новый документ"),
                                  QStringLiteral("A4 · 210 × 297 мм\nПустая страница со слоем"),
                                  this);
    newCard->onClick = [this]() { emit createRequested(); };

    auto *openCard = new StartCard(QStringLiteral("open"),
                                   QStringLiteral("Открыть файл"),
                                   QStringLiteral("Документы .vct\nSVG и изображения — импорт"),
                                   this);
    openCard->onClick = [this]() { emit openRequested(); };

    cards->addWidget(newCard);
    cards->addWidget(openCard);
    cards->addStretch(1);
    left->addLayout(cards);

    left->addSpacing(22);

    auto *sample = new QToolButton(this);
    sample->setText(QStringLiteral("Открыть демонстрационный документ  →"));
    sample->setCursor(Qt::PointingHandCursor);
    sample->setAutoRaise(true);
    sample->setStyleSheet(
        QStringLiteral("QToolButton { color:#7fa9e8; font-size:12px; border:none; "
                       "padding:4px 2px; text-align:left; }"
                       "QToolButton:hover { color:#a8c7f5; }"));
    connect(sample, &QToolButton::clicked, this, &WelcomeWidget::sampleRequested);
    left->addWidget(sample, 0, Qt::AlignLeft);

    left->addStretch(1);
    left->addWidget(mutedLabel(
        QStringLiteral("Vectora 0.1.7 · Linux · Windows · macOS · .vct, SVG, PNG, PDF"),
        this));

    root->addLayout(left, 1);

    // --- Right: recent documents ------------------------------------------
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("recentPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setMinimumWidth(330);
    panel->setMaximumWidth(390);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 18, 18, 18);
    panelLayout->setSpacing(10);

    auto *recentTitle = new QLabel(QStringLiteral("Недавние документы"), panel);
    recentTitle->setStyleSheet(
        QStringLiteral("color:#eef1f6; font-size:15px; font-weight:600;"));
    panelLayout->addWidget(recentTitle);
    panelLayout->addWidget(mutedLabel(
        QStringLiteral("Файлы, которые вы открывали или сохраняли."), panel));

    m_recentList = new QListWidget(panel);
    m_recentList->setObjectName(QStringLiteral("recentList"));
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panelLayout->addWidget(m_recentList, 1);

    m_emptyLabel = mutedLabel(
        QStringLiteral("Пока пусто. Создайте документ или откройте готовый — "
                       "он появится здесь."),
        panel, 12);
    m_emptyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    panelLayout->addWidget(m_emptyLabel, 1);

    root->addWidget(panel);

    connect(m_recentList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                    emit recentRequested(path);
            });
}

void WelcomeWidget::setRecentFiles(const QStringList &paths)
{
    m_recentList->clear();

    int added = 0;
    for (const QString &path : paths) {
        if (added >= 8)
            break;
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
            continue;

        auto *item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());
        item->setSizeHint(QSize(0, 56));

        auto *row = new QWidget(m_recentList);
        row->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(4, 6, 4, 6);
        rowLayout->setSpacing(2);

        auto *title = new QLabel(info.fileName(), row);
        title->setStyleSheet(
            QStringLiteral("color:#dfe5ee; font-size:12px; font-weight:600;"));
        auto *folder = new QLabel(info.absolutePath(), row);
        folder->setStyleSheet(QStringLiteral("color:#7f8792; font-size:10px;"));
        folder->setTextInteractionFlags(Qt::NoTextInteraction);

        rowLayout->addWidget(title);
        rowLayout->addWidget(folder);
        m_recentList->setItemWidget(item, row);
        ++added;
    }

    const bool empty = added == 0;
    m_recentList->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}
