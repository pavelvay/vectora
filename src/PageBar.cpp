#include "PageBar.h"

#include "CanvasView.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>

PageBar::PageBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PageBar"));
    setFixedHeight(30);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 1, 6, 0);
    layout->setSpacing(4);

    const auto makeNavButton = [this, layout](const QString &text, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFixedSize(20, 22);
        layout->addWidget(button);
        return button;
    };
    m_prevButton = makeNavButton(QStringLiteral("◀"),
                                 QStringLiteral("Предыдущая страница"));
    m_nextButton = makeNavButton(QStringLiteral("▶"),
                                 QStringLiteral("Следующая страница"));

    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(QStringLiteral("PageTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setMovable(false);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabBar::tab { background:#23262c; color:#aeb6c2; border:1px solid #3a3f47;"
        " border-bottom:none; border-top-left-radius:4px; border-top-right-radius:4px;"
        " padding:3px 10px; margin-right:2px; max-width:180px; }"
        "QTabBar::tab:selected { background:#2f4a75; color:#ffffff; border-color:#4f7cc0; }"
        "QTabBar::tab:hover:!selected { background:#2b2f37; color:#e6ebf2; }"));
    layout->addWidget(m_tabs, 1);

    m_pageLabel = new QLabel(this);
    m_pageLabel->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    layout->addWidget(m_pageLabel);

    m_backButton = new QToolButton(this);
    m_backButton->setToolTip(QStringLiteral("Вернуться к страницам"));
    m_backButton->hide();
    layout->addWidget(m_backButton);

    const auto makeButton = [this, layout](const QString &text, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFixedSize(24, 22);
        layout->addWidget(button);
        return button;
    };
    m_addButton = makeButton(QStringLiteral("+"), QStringLiteral("Новая страница"));
    m_duplicateButton = makeButton(QStringLiteral("⧉"),
                                   QStringLiteral("Дублировать страницу"));
    m_removeButton = makeButton(QStringLiteral("−"), QStringLiteral("Удалить страницу"));
    m_masterButton = makeButton(QStringLiteral("★"), QStringLiteral("Мастер-страницы"));

    m_masterMenu = new QMenu(this);
    m_masterButton->setMenu(m_masterMenu);
    m_masterButton->setPopupMode(QToolButton::InstantPopup);

    connect(m_prevButton, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        const int page = m_view->currentPage();
        if (page > 0)
            m_view->setCurrentPage(page - 1);
    });
    connect(m_nextButton, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        const int page = m_view->currentPage();
        if (page >= 0 && page + 1 < m_view->pageList().size())
            m_view->setCurrentPage(page + 1);
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_updating || !m_view || index < 0)
            return;
        m_view->setCurrentPage(index);
    });
    connect(m_tabs, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (!m_view || index < 0)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Имя страницы"), QStringLiteral("Название:"),
            QLineEdit::Normal, m_view->pageLabel(index), &ok);
        if (ok)
            m_view->renamePage(index, name);
    });
    connect(m_tabs, &QTabBar::customContextMenuRequested, this,
            &PageBar::showTabMenu);
    connect(m_addButton, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        m_view->addPage();
        emit status(QStringLiteral("Страница добавлена."));
    });
    connect(m_duplicateButton, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        const int page = qMax(0, m_view->currentPage());
        m_view->duplicatePage(page);
        emit status(QStringLiteral("Страница продублирована."));
    });
    connect(m_removeButton, &QToolButton::clicked, this, [this]() {
        if (!m_view)
            return;
        if (m_view->pageList().size() <= 1) {
            emit status(QStringLiteral("Нельзя удалить единственную страницу."));
            return;
        }
        m_view->removePage(qMax(0, m_view->currentPage()));
    });
    connect(m_backButton, &QToolButton::clicked, this, [this]() {
        if (m_view)
            m_view->setCurrentPage(0);
    });
    connect(m_masterMenu, &QMenu::aboutToShow, this, &PageBar::rebuildMasterMenu);
}

void PageBar::setView(CanvasView *view)
{
    m_view = view;
    if (!m_view)
        return;
    connect(m_view, &CanvasView::pageChanged, this, [this](int) { refresh(); });
    refresh();
}

void PageBar::refresh()
{
    if (!m_view)
        return;

    m_updating = true;
    m_tabs->blockSignals(true);
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);

    const QVector<PageInfo> pages = m_view->pageList();
    for (int i = 0; i < pages.size(); ++i) {
        const QString title = QStringLiteral("%1  %2").arg(i + 1).arg(pages.at(i).name);
        m_tabs->addTab(title);
        m_tabs->setTabToolTip(i, QStringLiteral("%1\n%2 × %3 px\nМастер: %4")
                                     .arg(pages.at(i).name)
                                     .arg(qRound(pages.at(i).size.width()))
                                     .arg(qRound(pages.at(i).size.height()))
                                     .arg(pages.at(i).master >= 0
                                              ? m_view->pageLabel(-1 - pages.at(i).master)
                                              : QStringLiteral("нет")));
    }

    const int current = m_view->currentPage();
    if (current >= 0 && current < m_tabs->count())
        m_tabs->setCurrentIndex(current);
    else
        m_tabs->setCurrentIndex(-1);
    m_tabs->blockSignals(false);

    if (current < 0) {
        m_backButton->setText(QStringLiteral("✕ %1").arg(m_view->pageLabel(current)));
        m_backButton->setToolTip(QStringLiteral("Вернуться к страницам"));
        m_backButton->show();
    } else {
        m_backButton->hide();
    }

    m_pageLabel->setText(QStringLiteral("Стр. %1 из %2")
                             .arg(current >= 0 ? current + 1 : 0)
                             .arg(pages.size()));
    m_removeButton->setEnabled(pages.size() > 1);
    m_prevButton->setEnabled(current > 0);
    m_nextButton->setEnabled(current >= 0 && current + 1 < pages.size());
    m_updating = false;

    rebuildMasterMenu();
}

void PageBar::rebuildMasterMenu()
{
    if (!m_view)
        return;
    m_masterMenu->clear();

    const int current = m_view->currentPage();
    const QVector<MasterPage> masters = m_view->masterList();
    const int applied = current >= 0 && current < m_view->pageList().size()
                            ? m_view->pageList().at(current).master
                            : -1;

    if (current >= 0) {
        auto *none = m_masterMenu->addAction(QStringLiteral("Без мастера"));
        none->setCheckable(true);
        none->setChecked(applied < 0);
        connect(none, &QAction::triggered, this, [this, current]() {
            m_view->applyMasterToPage(current, -1);
        });
        m_masterMenu->addSeparator();
        for (int i = 0; i < masters.size(); ++i) {
            auto *action = m_masterMenu->addAction(masters.at(i).name);
            action->setCheckable(true);
            action->setChecked(applied == i);
            connect(action, &QAction::triggered, this, [this, current, i]() {
                m_view->applyMasterToPage(current, i);
                emit status(QStringLiteral("Мастер применён к странице."));
            });
        }
        if (!masters.isEmpty())
            m_masterMenu->addSeparator();
        auto *create = m_masterMenu->addAction(
            QStringLiteral("Создать мастер из этой страницы…"));
        connect(create, &QAction::triggered, this,
                &PageBar::createMasterFromCurrentPage);
    }

    if (!masters.isEmpty()) {
        auto *editMenu = m_masterMenu->addMenu(QStringLiteral("Редактировать мастер"));
        for (int i = 0; i < masters.size(); ++i) {
            auto *action = editMenu->addAction(masters.at(i).name);
            connect(action, &QAction::triggered, this, [this, i]() {
                m_view->setCurrentPage(-1 - i);
            });
        }
    }

    auto *setup = m_masterMenu->addAction(QStringLiteral("Размер страницы…"));
    connect(setup, &QAction::triggered, this, [this]() { emit pageSetupRequested(); });
}

void PageBar::createMasterFromCurrentPage()
{
    if (!m_view || m_view->currentPage() < 0)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Новая мастер-страница"), QStringLiteral("Название:"),
        QLineEdit::Normal,
        QStringLiteral("Мастер %1").arg(m_view->masterList().size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const int master = m_view->createMasterFromPage(m_view->currentPage(), name.trimmed());
    if (master >= 0)
        emit status(QStringLiteral("Мастер-страница создана."));
}

void PageBar::showTabMenu(const QPoint &pos)
{
    if (!m_view)
        return;
    const int tab = m_tabs->tabAt(pos);
    if (tab >= 0 && tab != m_view->currentPage())
        m_view->setCurrentPage(tab);

    QMenu menu(this);
    QAction *rename = menu.addAction(QStringLiteral("Переименовать…"));
    QAction *duplicate = menu.addAction(QStringLiteral("Дублировать"));
    QAction *remove = menu.addAction(QStringLiteral("Удалить"));
    remove->setEnabled(m_view->pageList().size() > 1);
    menu.addSeparator();
    QAction *setup = menu.addAction(QStringLiteral("Размер страницы…"));
    menu.addSeparator();
    QMenu *masterMenu = menu.addMenu(QStringLiteral("Мастер"));
    const int current = m_view->currentPage();
    if (current >= 0) {
        const QVector<MasterPage> masters = m_view->masterList();
        const int applied = m_view->pageList().at(current).master;
        QAction *none = masterMenu->addAction(QStringLiteral("Без мастера"));
        none->setCheckable(true);
        none->setChecked(applied < 0);
        for (int i = 0; i < masters.size(); ++i) {
            QAction *action = masterMenu->addAction(masters.at(i).name);
            action->setCheckable(true);
            action->setChecked(applied == i);
        }
        masterMenu->addSeparator();
        masterMenu->addAction(QStringLiteral("Создать мастер из страницы…"));
    }

    QAction *chosen = menu.exec(m_tabs->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == rename) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Имя страницы"), QStringLiteral("Название:"),
            QLineEdit::Normal, m_view->pageLabel(current), &ok);
        if (ok)
            m_view->renamePage(current, name);
    } else if (chosen == duplicate) {
        m_view->duplicatePage(qMax(0, current));
    } else if (chosen == remove) {
        m_view->removePage(qMax(0, current));
    } else if (chosen == setup) {
        emit pageSetupRequested();
    } else if (chosen->text() == QLatin1String("Создать мастер из страницы…")) {
        createMasterFromCurrentPage();
    } else if (chosen->parent() == masterMenu) {
        int index = -1;
        const QVector<MasterPage> masters = m_view->masterList();
        for (int i = 0; i < masters.size(); ++i) {
            if (masters.at(i).name == chosen->text())
                index = i;
        }
        m_view->applyMasterToPage(current, index);
    }
}
