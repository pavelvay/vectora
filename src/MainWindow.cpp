#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include "CanvasView.h"
#include "ColorPalette.h"
#include "Icons.h"
#include "Ruler.h"
#include "Theme.h"
#include "ToolPalette.h"

namespace {

constexpr qreal kPxPerMm = 3.7795275591;

QLabel *fieldLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    return label;
}

QDoubleSpinBox *numberField(QWidget *parent, double min, double max, double value,
                            const QString &suffix, int width = 80, int decimals = 1)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setValue(value);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    spin->setFixedWidth(width);
    spin->setKeyboardTracking(false);
    return spin;
}

QFrame *verticalRule(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFixedWidth(1);
    line->setStyleSheet(QStringLiteral("background:#34383f; border:none;"));
    return line;
}

QToolButton *textToggle(const QString &text, const QString &tip, QWidget *parent,
                        const QFont &font)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setCheckable(true);
    button->setFixedSize(26, 26);
    button->setFont(font);
    return button;
}

QPainterPath starShape(const QPointF &center, qreal outer, qreal inner, int points = 5)
{
    constexpr qreal pi = 3.14159265358979323846;
    QPainterPath path;
    for (int i = 0; i < points * 2; ++i) {
        const qreal radius = (i % 2 == 0) ? outer : inner;
        const qreal angle = -pi / 2.0 + i * pi / points;
        const QPointF pt(center.x() + radius * std::cos(angle),
                         center.y() + radius * std::sin(angle));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Vectora — векторный редактор"));
    setWindowIcon(Icons::appIcon());
    resize(1560, 980);
    setMinimumSize(1120, 720);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);

    createActions();
    createCanvas();
    createDocks();
    createStatusBar();
    createMenus();
    createMainToolBar();
    createPropertyBar();
    populateSample();
    applyTool(Tool::Select);

    QTimer::singleShot(0, this, [this]() { m_view->fitPage(); });
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

QAction *MainWindow::makeAction(const QString &key, const QString &text,
                                const QKeySequence &shortcut, const QString &statusTip,
                                bool stubAction)
{
    QString iconId = key;
    if (key.startsWith(QLatin1String("fx-")))
        iconId = QStringLiteral("fx");
    else if (key.startsWith(QLatin1String("order-")))
        iconId = QStringLiteral("layers");
    else if (key.startsWith(QLatin1String("text-")))
        iconId = QStringLiteral("text");
    else if (key == QLatin1String("zoom-selection"))
        iconId = QStringLiteral("crop");
    else if (key == QLatin1String("duplicate"))
        iconId = QStringLiteral("copy");
    else if (key == QLatin1String("preferences"))
        iconId = QStringLiteral("settings");

    auto *newAction = new QAction(Icons::icon(iconId), text, this);
    if (!shortcut.isEmpty())
        newAction->setShortcut(shortcut);
    newAction->setStatusTip(statusTip.isEmpty() ? text : statusTip);
    addAction(newAction);

    if (stubAction) {
        connect(newAction, &QAction::triggered, this,
                [this, text]() { showStub(text); });
    }

    m_actions.insert(key, newAction);
    return newAction;
}

QAction *MainWindow::action(const QString &key) const
{
    return m_actions.value(key);
}

void MainWindow::createActions()
{
    // File
    makeAction(QStringLiteral("new"), QStringLiteral("Создать"),
               QKeySequence::New, QStringLiteral("Создать новый документ"));
    makeAction(QStringLiteral("open"), QStringLiteral("Открыть…"),
               QKeySequence::Open, QStringLiteral("Открыть документ"));
    makeAction(QStringLiteral("save"), QStringLiteral("Сохранить"),
               QKeySequence::Save, QStringLiteral("Сохранить документ"));
    makeAction(QStringLiteral("save-as"), QStringLiteral("Сохранить как…"),
               QKeySequence::SaveAs, QStringLiteral("Сохранить документ под новым именем"));
    makeAction(QStringLiteral("import"), QStringLiteral("Импорт…"),
               QKeySequence(QStringLiteral("Ctrl+I")), QStringLiteral("Импортировать SVG, PDF или изображение"));
    makeAction(QStringLiteral("export"), QStringLiteral("Экспорт…"),
               QKeySequence(QStringLiteral("Ctrl+E")), QStringLiteral("Экспортировать документ"));
    makeAction(QStringLiteral("pdf"), QStringLiteral("Экспорт в PDF…"),
               QKeySequence(), QStringLiteral("Экспортировать документ в PDF"));
    makeAction(QStringLiteral("print"), QStringLiteral("Печать…"),
               QKeySequence::Print, QStringLiteral("Печать документа"));
    auto *quit = makeAction(QStringLiteral("quit"), QStringLiteral("Выход"),
                            QKeySequence::Quit, QStringLiteral("Закрыть приложение"), false);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    // Edit
    makeAction(QStringLiteral("undo"), QStringLiteral("Отменить"),
               QKeySequence::Undo, QStringLiteral("Отменить последнее действие"));
    makeAction(QStringLiteral("redo"), QStringLiteral("Повторить"),
               QKeySequence::Redo, QStringLiteral("Повторить отменённое действие"));
    makeAction(QStringLiteral("cut"), QStringLiteral("Вырезать"),
               QKeySequence::Cut, QStringLiteral("Вырезать выделенные объекты"));
    makeAction(QStringLiteral("copy"), QStringLiteral("Копировать"),
               QKeySequence::Copy, QStringLiteral("Копировать выделенные объекты"));
    makeAction(QStringLiteral("paste"), QStringLiteral("Вставить"),
               QKeySequence::Paste, QStringLiteral("Вставить объекты из буфера"));
    makeAction(QStringLiteral("duplicate"), QStringLiteral("Дублировать"),
               QKeySequence(QStringLiteral("Ctrl+D")), QStringLiteral("Создать копию выделения"));
    makeAction(QStringLiteral("delete"), QStringLiteral("Удалить"),
               QKeySequence::Delete, QStringLiteral("Удалить выделенные объекты"), false);
    makeAction(QStringLiteral("select-all"), QStringLiteral("Выделить всё"),
               QKeySequence::SelectAll, QStringLiteral("Выделить все объекты"), false);
    makeAction(QStringLiteral("preferences"), QStringLiteral("Настройки…"),
               QKeySequence(QStringLiteral("Ctrl+,")), QStringLiteral("Настройки приложения"));

    // View
    makeAction(QStringLiteral("zoom-in"), QStringLiteral("Увеличить"),
               QKeySequence::ZoomIn, QStringLiteral("Увеличить масштаб"), false);
    makeAction(QStringLiteral("zoom-out"), QStringLiteral("Уменьшить"),
               QKeySequence::ZoomOut, QStringLiteral("Уменьшить масштаб"), false);
    makeAction(QStringLiteral("actual"), QStringLiteral("Масштаб 100%"),
               QKeySequence(QStringLiteral("Ctrl+1")), QStringLiteral("Показать в натуральную величину"), false);
    makeAction(QStringLiteral("fit"), QStringLiteral("По размеру страницы"),
               QKeySequence(QStringLiteral("Ctrl+0")), QStringLiteral("Вписать страницу в окно"), false);
    makeAction(QStringLiteral("zoom-selection"), QStringLiteral("По выделению"),
               QKeySequence(QStringLiteral("Ctrl+2")), QStringLiteral("Приблизить выделенные объекты"));
    makeAction(QStringLiteral("rulers"), QStringLiteral("Линейки"),
               QKeySequence(QStringLiteral("Ctrl+R")), QStringLiteral("Показать или скрыть линейки"));
    makeAction(QStringLiteral("guides"), QStringLiteral("Направляющие"),
               QKeySequence(), QStringLiteral("Показать или скрыть направляющие"));
    makeAction(QStringLiteral("grid"), QStringLiteral("Сетка"),
               QKeySequence(QStringLiteral("Ctrl+Alt+G")), QStringLiteral("Показать или скрыть сетку"));
    makeAction(QStringLiteral("snap"), QStringLiteral("Привязка"),
               QKeySequence(), QStringLiteral("Привязка к сетке и направляющим"));
    auto *fullscreen = makeAction(QStringLiteral("fullscreen"), QStringLiteral("Полноэкранный режим"),
                                  QKeySequence(QStringLiteral("F11")), QStringLiteral("Во весь экран"));

    // Object
    makeAction(QStringLiteral("group"), QStringLiteral("Сгруппировать"),
               QKeySequence(QStringLiteral("Ctrl+G")), QStringLiteral("Сгруппировать выделенные объекты"));
    makeAction(QStringLiteral("ungroup"), QStringLiteral("Разгруппировать"),
               QKeySequence(QStringLiteral("Ctrl+U")), QStringLiteral("Разгруппировать выделение"));
    makeAction(QStringLiteral("order-front"), QStringLiteral("На передний план"),
               QKeySequence(QStringLiteral("Ctrl+Home")), QStringLiteral("Поднять объект на передний план"));
    makeAction(QStringLiteral("order-forward"), QStringLiteral("На уровень выше"),
               QKeySequence(QStringLiteral("Ctrl+Up")), QStringLiteral("Поднять объект на уровень выше"));
    makeAction(QStringLiteral("order-backward"), QStringLiteral("На уровень ниже"),
               QKeySequence(QStringLiteral("Ctrl+Down")), QStringLiteral("Опустить объект на уровень ниже"));
    makeAction(QStringLiteral("order-back"), QStringLiteral("На задний план"),
               QKeySequence(QStringLiteral("Ctrl+End")), QStringLiteral("Опустить объект на задний план"));
    makeAction(QStringLiteral("align-left"), QStringLiteral("Выровнять по левому краю"),
               QKeySequence(), QStringLiteral("Выровнять объекты по левому краю"));
    makeAction(QStringLiteral("align-hcenter"), QStringLiteral("Выровнять по центру"),
               QKeySequence(), QStringLiteral("Выровнять объекты по центру"));
    makeAction(QStringLiteral("align-right"), QStringLiteral("Выровнять по правому краю"),
               QKeySequence(), QStringLiteral("Выровнять объекты по правому краю"));
    makeAction(QStringLiteral("align-top"), QStringLiteral("Выровнять по верху"),
               QKeySequence(), QStringLiteral("Выровнять объекты по верхнему краю"));
    makeAction(QStringLiteral("align-vcenter"), QStringLiteral("Выровнять по середине"),
               QKeySequence(), QStringLiteral("Выровнять объекты по середине"));
    makeAction(QStringLiteral("align-bottom"), QStringLiteral("Выровнять по низу"),
               QKeySequence(), QStringLiteral("Выровнять объекты по нижнему краю"));
    makeAction(QStringLiteral("flip-h"), QStringLiteral("Отразить по горизонтали"),
               QKeySequence(), QStringLiteral("Отразить объект слева направо"));
    makeAction(QStringLiteral("flip-v"), QStringLiteral("Отразить по вертикали"),
               QKeySequence(), QStringLiteral("Отразить объект сверху вниз"));
    makeAction(QStringLiteral("rotate"), QStringLiteral("Повернуть на 90°"),
               QKeySequence(), QStringLiteral("Повернуть объект на 90 градусов"));
    makeAction(QStringLiteral("convert-curves"), QStringLiteral("Преобразовать в кривые"),
               QKeySequence(QStringLiteral("Ctrl+Q")), QStringLiteral("Преобразовать объект в редактируемые кривые"));
    makeAction(QStringLiteral("lock"), QStringLiteral("Заблокировать"),
               QKeySequence(), QStringLiteral("Заблокировать объект от изменений"));

    // Effects
    makeAction(QStringLiteral("fx-shadow"), QStringLiteral("Тень…"),
               QKeySequence(), QStringLiteral("Добавить отбрасываемую тень"));
    makeAction(QStringLiteral("fx-transparency"), QStringLiteral("Прозрачность…"),
               QKeySequence(), QStringLiteral("Настроить прозрачность"));
    makeAction(QStringLiteral("fx-blend"), QStringLiteral("Перетекание…"),
               QKeySequence(), QStringLiteral("Создать перетекание между объектами"));
    makeAction(QStringLiteral("fx-envelope"), QStringLiteral("Оболочка…"),
               QKeySequence(), QStringLiteral("Деформировать объект оболочкой"));
    makeAction(QStringLiteral("fx-extrude"), QStringLiteral("Экструзия…"),
               QKeySequence(), QStringLiteral("Придать объекту объём"));
    makeAction(QStringLiteral("fx-lens"), QStringLiteral("Линза…"),
               QKeySequence(), QStringLiteral("Наложить эффект линзы"));
    makeAction(QStringLiteral("fx-bevel"), QStringLiteral("Скос…"),
               QKeySequence(), QStringLiteral("Добавить фаску по краю"));

    // Text
    makeAction(QStringLiteral("text-character"), QStringLiteral("Символ…"),
               QKeySequence(), QStringLiteral("Свойства шрифта выделенного текста"));
    makeAction(QStringLiteral("text-paragraph"), QStringLiteral("Абзац…"),
               QKeySequence(), QStringLiteral("Форматирование абзаца"));
    makeAction(QStringLiteral("text-styles"), QStringLiteral("Стили текста"),
               QKeySequence(), QStringLiteral("Библиотека стилей текста"));
    makeAction(QStringLiteral("text-symbol"), QStringLiteral("Вставить символ…"),
               QKeySequence(), QStringLiteral("Вставить специальный символ"));

    // Help
    makeAction(QStringLiteral("shortcuts"), QStringLiteral("Горячие клавиши"),
               QKeySequence(QStringLiteral("Ctrl+F1")), QStringLiteral("Список горячих клавиш"), false);
    makeAction(QStringLiteral("about"), QStringLiteral("О программе"),
               QKeySequence(), QStringLiteral("Сведения о Vectora"), false);

    // Tool shortcuts
    for (const ToolInfo &info : toolInfos()) {
        auto *toolAction = new QAction(info.name, this);
        toolAction->setShortcut(QKeySequence(info.shortcut));
        toolAction->setShortcutContext(Qt::ApplicationShortcut);
        connect(toolAction, &QAction::triggered, this,
                [this, tool = info.tool]() { applyTool(tool); });
        addAction(toolAction);
    }

    // Wire the actions that already work.
    connect(action(QStringLiteral("zoom-in")), &QAction::triggered, this,
            [this]() { m_view->zoomStep(1.25); });
    connect(action(QStringLiteral("zoom-out")), &QAction::triggered, this,
            [this]() { m_view->zoomStep(1.0 / 1.25); });
    connect(action(QStringLiteral("actual")), &QAction::triggered, this,
            [this]() { m_view->actualSize(); });
    connect(action(QStringLiteral("fit")), &QAction::triggered, this,
            [this]() { m_view->fitPage(); });
    connect(action(QStringLiteral("select-all")), &QAction::triggered, this,
            &MainWindow::selectAllObjects);
    connect(action(QStringLiteral("delete")), &QAction::triggered, this,
            &MainWindow::deleteSelection);
    connect(action(QStringLiteral("shortcuts")), &QAction::triggered, this,
            &MainWindow::showShortcuts);
    connect(action(QStringLiteral("about")), &QAction::triggered, this,
            &MainWindow::showAbout);

    auto *gridAction = action(QStringLiteral("grid"));
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, this, [this](bool on) {
        if (!m_view)
            return;
        m_view->setShowGrid(on);
        statusBar()->showMessage(on ? QStringLiteral("Сетка включена")
                                    : QStringLiteral("Сетка выключена"),
                                 2500);
    });

    auto *rulersAction = action(QStringLiteral("rulers"));
    rulersAction->setCheckable(true);
    rulersAction->setChecked(true);
    connect(rulersAction, &QAction::toggled, this, [this](bool on) {
        if (!m_hRuler || !m_vRuler)
            return;
        m_hRuler->setVisible(on);
        m_vRuler->setVisible(on);
        statusBar()->showMessage(on ? QStringLiteral("Линейки показаны")
                                    : QStringLiteral("Линейки скрыты"),
                                 2500);
    });

    auto *snapAction = action(QStringLiteral("snap"));
    snapAction->setCheckable(true);
    snapAction->setChecked(true);

    auto *guidesAction = action(QStringLiteral("guides"));
    guidesAction->setCheckable(true);
    guidesAction->setChecked(true);

    fullscreen->setCheckable(true);
    connect(fullscreen, &QAction::toggled, this, [this](bool on) {
        if (on)
            showFullScreen();
        else
            showNormal();
    });
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("Файл"));
    fileMenu->addAction(action(QStringLiteral("new")));
    fileMenu->addAction(action(QStringLiteral("open")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("save")));
    fileMenu->addAction(action(QStringLiteral("save-as")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("import")));
    fileMenu->addAction(action(QStringLiteral("export")));
    fileMenu->addAction(action(QStringLiteral("pdf")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("print")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("quit")));

    auto *editMenu = menuBar()->addMenu(QStringLiteral("Правка"));
    editMenu->addAction(action(QStringLiteral("undo")));
    editMenu->addAction(action(QStringLiteral("redo")));
    editMenu->addSeparator();
    editMenu->addAction(action(QStringLiteral("cut")));
    editMenu->addAction(action(QStringLiteral("copy")));
    editMenu->addAction(action(QStringLiteral("paste")));
    editMenu->addAction(action(QStringLiteral("duplicate")));
    editMenu->addAction(action(QStringLiteral("delete")));
    editMenu->addSeparator();
    editMenu->addAction(action(QStringLiteral("select-all")));
    editMenu->addSeparator();
    editMenu->addAction(action(QStringLiteral("preferences")));

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("Просмотр"));
    viewMenu->addAction(action(QStringLiteral("zoom-in")));
    viewMenu->addAction(action(QStringLiteral("zoom-out")));
    viewMenu->addAction(action(QStringLiteral("actual")));
    viewMenu->addAction(action(QStringLiteral("fit")));
    viewMenu->addAction(action(QStringLiteral("zoom-selection")));
    viewMenu->addSeparator();
    viewMenu->addAction(action(QStringLiteral("rulers")));
    viewMenu->addAction(action(QStringLiteral("guides")));
    viewMenu->addAction(action(QStringLiteral("grid")));
    viewMenu->addAction(action(QStringLiteral("snap")));
    viewMenu->addSeparator();
    viewMenu->addAction(action(QStringLiteral("fullscreen")));

    auto *objectMenu = menuBar()->addMenu(QStringLiteral("Объект"));
    objectMenu->addAction(action(QStringLiteral("group")));
    objectMenu->addAction(action(QStringLiteral("ungroup")));
    objectMenu->addSeparator();
    objectMenu->addAction(action(QStringLiteral("order-front")));
    objectMenu->addAction(action(QStringLiteral("order-forward")));
    objectMenu->addAction(action(QStringLiteral("order-backward")));
    objectMenu->addAction(action(QStringLiteral("order-back")));
    objectMenu->addSeparator();
    objectMenu->addAction(action(QStringLiteral("align-left")));
    objectMenu->addAction(action(QStringLiteral("align-hcenter")));
    objectMenu->addAction(action(QStringLiteral("align-right")));
    objectMenu->addAction(action(QStringLiteral("align-top")));
    objectMenu->addAction(action(QStringLiteral("align-vcenter")));
    objectMenu->addAction(action(QStringLiteral("align-bottom")));
    objectMenu->addSeparator();
    objectMenu->addAction(action(QStringLiteral("flip-h")));
    objectMenu->addAction(action(QStringLiteral("flip-v")));
    objectMenu->addAction(action(QStringLiteral("rotate")));
    objectMenu->addSeparator();
    objectMenu->addAction(action(QStringLiteral("convert-curves")));
    objectMenu->addAction(action(QStringLiteral("lock")));

    auto *effectsMenu = menuBar()->addMenu(QStringLiteral("Эффекты"));
    effectsMenu->addAction(action(QStringLiteral("fx-shadow")));
    effectsMenu->addAction(action(QStringLiteral("fx-transparency")));
    effectsMenu->addAction(action(QStringLiteral("fx-blend")));
    effectsMenu->addSeparator();
    effectsMenu->addAction(action(QStringLiteral("fx-envelope")));
    effectsMenu->addAction(action(QStringLiteral("fx-extrude")));
    effectsMenu->addAction(action(QStringLiteral("fx-lens")));
    effectsMenu->addAction(action(QStringLiteral("fx-bevel")));

    auto *textMenu = menuBar()->addMenu(QStringLiteral("Текст"));
    textMenu->addAction(action(QStringLiteral("text-character")));
    textMenu->addAction(action(QStringLiteral("text-paragraph")));
    textMenu->addSeparator();
    textMenu->addAction(action(QStringLiteral("text-styles")));
    textMenu->addAction(action(QStringLiteral("text-symbol")));

    auto *windowMenu = menuBar()->addMenu(QStringLiteral("Окно"));
    windowMenu->addAction(m_toolsDock->toggleViewAction());
    windowMenu->addAction(m_objectsDock->toggleViewAction());
    windowMenu->addAction(m_propertiesDock->toggleViewAction());
    windowMenu->addAction(m_paletteDock->toggleViewAction());
    windowMenu->addSeparator();
    windowMenu->addAction(action(QStringLiteral("preferences")));

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("Справка"));
    helpMenu->addAction(action(QStringLiteral("shortcuts")));
    helpMenu->addAction(action(QStringLiteral("about")));
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void MainWindow::createCanvas()
{
    auto *central = new QWidget(this);
    auto *grid = new QGridLayout(central);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    auto *corner = new QLabel(QStringLiteral("мм"), central);
    corner->setAlignment(Qt::AlignCenter);
    corner->setFixedSize(22, 22);
    corner->setStyleSheet(QStringLiteral(
        "background:#202226; color:#8b929d; font-size:9px;"
        "border-right:1px solid #2c2f35; border-bottom:1px solid #2c2f35;"));

    m_hRuler = new Ruler(Qt::Horizontal, central);
    m_vRuler = new Ruler(Qt::Vertical, central);
    m_scene = new QGraphicsScene(this);
    m_view = new CanvasView(central);
    m_view->setScene(m_scene);

    m_hRuler->setView(m_view);
    m_vRuler->setView(m_view);

    grid->addWidget(corner, 0, 0);
    grid->addWidget(m_hRuler, 0, 1);
    grid->addWidget(m_vRuler, 1, 0);
    grid->addWidget(m_view, 1, 1);

    setCentralWidget(central);

    connect(m_view, &CanvasView::zoomChanged, this, &MainWindow::updateZoomUi);
    connect(m_view, &CanvasView::scenePosChanged, this, &MainWindow::updateCoordinates);
    connect(m_view, &CanvasView::statusHint, this, [this](const QString &text) {
        statusBar()->showMessage(text, 5000);
    });

    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, m_hRuler,
            qOverload<>(&QWidget::update));
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, m_vRuler,
            qOverload<>(&QWidget::update));
}

// ---------------------------------------------------------------------------
// Docks
// ---------------------------------------------------------------------------

void MainWindow::createDocks()
{
    // Left tool box
    m_toolsDock = new QDockWidget(QStringLiteral("Инструменты"), this);
    m_toolsDock->setObjectName(QStringLiteral("ToolsDock"));
    m_toolPalette = new ToolPalette(m_toolsDock);
    m_toolsDock->setWidget(m_toolPalette);
    m_toolsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_toolsDock->setTitleBarWidget(new QWidget(m_toolsDock));
    m_toolsDock->setFixedWidth(54);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);
    connect(m_toolPalette, &ToolPalette::toolSelected, this, &MainWindow::applyTool);

    // Right: objects tree
    m_objectsDock = new QDockWidget(QStringLiteral("Объекты и слои"), this);
    m_objectsDock->setObjectName(QStringLiteral("ObjectsDock"));
    m_objectsDock->setMinimumWidth(250);

    auto *objectsHost = new QWidget(m_objectsDock);
    auto *objectsLayout = new QVBoxLayout(objectsHost);
    objectsLayout->setContentsMargins(6, 6, 6, 6);
    objectsLayout->setSpacing(4);

    auto *layerBar = new QWidget(objectsHost);
    auto *layerBarLayout = new QHBoxLayout(layerBar);
    layerBarLayout->setContentsMargins(0, 0, 0, 0);
    layerBarLayout->setSpacing(2);

    const struct {
        const char *icon;
        const char *tip;
        const char *message;
    } layerTools[] = {
        {"layer-add", "Новый слой", "Новый слой"},
        {"copy", "Дублировать объект", "Дублирование объекта"},
        {"delete", "Удалить объект", "Удаление объекта"},
    };

    for (const auto &tool : layerTools) {
        auto *button = new QToolButton(layerBar);
        button->setIcon(Icons::icon(QString::fromLatin1(tool.icon)));
        button->setIconSize(QSize(18, 18));
        button->setAutoRaise(true);
        button->setToolTip(QString::fromUtf8(tool.tip));
        connect(button, &QToolButton::clicked, this,
                [this, message = QString::fromUtf8(tool.message)]() { showStub(message); });
        layerBarLayout->addWidget(button);
    }
    layerBarLayout->addStretch(1);

    auto *tree = new QTreeWidget(objectsHost);
    tree->setHeaderHidden(true);
    tree->setIndentation(14);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *layer1 = new QTreeWidgetItem(tree, {QStringLiteral("Слой 1")});
    QFont layerFont = tree->font();
    layerFont.setBold(true);
    layer1->setFont(0, layerFont);
    layer1->setExpanded(true);

    const QStringList objects = {
        QStringLiteral("Текст: VECTORA"),
        QStringLiteral("Текст: векторный редактор"),
        QStringLiteral("Прямоугольник"),
        QStringLiteral("Эллипс"),
        QStringLiteral("Звезда"),
        QStringLiteral("Кривая"),
        QStringLiteral("Текст: Ваш дизайн здесь"),
    };
    for (const QString &name : objects)
        new QTreeWidgetItem(layer1, {name});

    auto *layer2 = new QTreeWidgetItem(tree, {QStringLiteral("Слой 2")});
    layer2->setFont(0, layerFont);

    objectsLayout->addWidget(layerBar);
    objectsLayout->addWidget(tree, 1);

    m_objectsDock->setWidget(objectsHost);
    addDockWidget(Qt::RightDockWidgetArea, m_objectsDock);

    // Right: properties
    m_propertiesDock = new QDockWidget(QStringLiteral("Свойства"), this);
    m_propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_propertiesDock->setMinimumWidth(250);

    auto *propsHost = new QWidget(m_propertiesDock);
    auto *propsLayout = new QVBoxLayout(propsHost);
    propsLayout->setContentsMargins(10, 8, 10, 10);
    propsLayout->setSpacing(8);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->addRow(fieldLabel(QStringLiteral("X")), numberField(propsHost, -10000, 10000, 100.0, QStringLiteral(" px"), 110, 0));
    form->addRow(fieldLabel(QStringLiteral("Y")), numberField(propsHost, -10000, 10000, 320.0, QStringLiteral(" px"), 110, 0));
    form->addRow(fieldLabel(QStringLiteral("Ширина")), numberField(propsHost, 0, 10000, 250.0, QStringLiteral(" px"), 110, 0));
    form->addRow(fieldLabel(QStringLiteral("Высота")), numberField(propsHost, 0, 10000, 250.0, QStringLiteral(" px"), 110, 0));
    form->addRow(fieldLabel(QStringLiteral("Угол")), numberField(propsHost, -360, 360, 0.0, QStringLiteral(" °")));
    propsLayout->addLayout(form);

    auto *fillButton = new QToolButton(propsHost);
    fillButton->setIcon(Icons::icon(QStringLiteral("swatch"), QColor(0x4f, 0x7c, 0xc0)));
    fillButton->setIconSize(QSize(18, 18));
    fillButton->setText(QStringLiteral("  Заливка"));
    fillButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fillButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *outlineButton = new QToolButton(propsHost);
    outlineButton->setIcon(Icons::icon(QStringLiteral("swatch"), QColor(0xe0, 0x46, 0x6b)));
    outlineButton->setIconSize(QSize(18, 18));
    outlineButton->setText(QStringLiteral("  Обводка"));
    outlineButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    outlineButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    propsLayout->addWidget(fillButton);
    propsLayout->addWidget(outlineButton);

    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(fieldLabel(QStringLiteral("Прозрачность")));
    auto *opacity = new QSlider(Qt::Horizontal, propsHost);
    opacity->setRange(0, 100);
    opacity->setValue(100);
    opacityRow->addWidget(opacity, 1);
    propsLayout->addLayout(opacityRow);

    auto *hint = new QLabel(QStringLiteral(
        "Значения справочные: редактирование объектов появится на следующем этапе."),
        propsHost);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    propsLayout->addWidget(hint);
    propsLayout->addStretch(1);

    m_propertiesDock->setWidget(propsHost);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);
    splitDockWidget(m_objectsDock, m_propertiesDock, Qt::Vertical);
    resizeDocks({m_objectsDock, m_propertiesDock}, {430, 360}, Qt::Vertical);

    // Bottom: colour palette
    m_paletteDock = new QDockWidget(QStringLiteral("Палитра"), this);
    m_paletteDock->setObjectName(QStringLiteral("PaletteDock"));
    m_palette = new ColorPalette(m_paletteDock);
    m_paletteDock->setWidget(m_palette);
    m_paletteDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_paletteDock->setTitleBarWidget(new QWidget(m_paletteDock));
    m_paletteDock->setFixedHeight(36);
    addDockWidget(Qt::BottomDockWidgetArea, m_paletteDock);

    connect(m_palette, &ColorPalette::statusHint, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });
}

// ---------------------------------------------------------------------------
// Tool bars
// ---------------------------------------------------------------------------

void MainWindow::createMainToolBar()
{
    auto *bar = addToolBar(QStringLiteral("Основная"));
    bar->setObjectName(QStringLiteral("MainToolBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(20, 20));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    bar->addAction(action(QStringLiteral("new")));
    bar->addAction(action(QStringLiteral("open")));
    bar->addAction(action(QStringLiteral("save")));
    bar->addSeparator();
    bar->addAction(action(QStringLiteral("undo")));
    bar->addAction(action(QStringLiteral("redo")));
    bar->addSeparator();
    bar->addAction(action(QStringLiteral("cut")));
    bar->addAction(action(QStringLiteral("copy")));
    bar->addAction(action(QStringLiteral("paste")));
    bar->addSeparator();
    bar->addAction(action(QStringLiteral("zoom-out")));

    m_toolbarZoomCombo = new QComboBox(bar);
    m_toolbarZoomCombo->setEditable(true);
    m_toolbarZoomCombo->setFixedWidth(92);
    m_toolbarZoomCombo->addItems({QStringLiteral("25 %"), QStringLiteral("50 %"),
                                  QStringLiteral("75 %"), QStringLiteral("100 %"),
                                  QStringLiteral("150 %"), QStringLiteral("200 %"),
                                  QStringLiteral("400 %")});
    m_toolbarZoomCombo->setCurrentText(QStringLiteral("100 %"));
    bar->addWidget(m_toolbarZoomCombo);

    connect(m_toolbarZoomCombo, &QComboBox::activated, this, [this](int) {
        applyZoomText(m_toolbarZoomCombo->currentText());
    });
    connect(m_toolbarZoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        applyZoomText(m_toolbarZoomCombo->currentText());
    });

    bar->addAction(action(QStringLiteral("zoom-in")));
    bar->addAction(action(QStringLiteral("fit")));
    bar->addSeparator();
    bar->addAction(action(QStringLiteral("grid")));
    bar->addAction(action(QStringLiteral("snap")));
    bar->addSeparator();
    bar->addAction(action(QStringLiteral("group")));
    bar->addAction(action(QStringLiteral("ungroup")));

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    auto *pageBadge = new QLabel(QStringLiteral("A4  ·  210 × 297 мм"), bar);
    pageBadge->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px; padding-right:8px;"));
    bar->addWidget(pageBadge);
}

void MainWindow::createPropertyBar()
{
    auto *bar = addToolBar(QStringLiteral("Свойства инструмента"));
    bar->setObjectName(QStringLiteral("PropertyBar"));
    bar->setMovable(false);
    bar->setFloatable(false);

    m_propertyStack = new QStackedWidget(bar);
    m_propertyStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // 0 — zoom / pan
    auto *zoomPage = new QWidget(m_propertyStack);
    auto *zoomLayout = new QHBoxLayout(zoomPage);
    zoomLayout->setContentsMargins(2, 0, 2, 0);
    zoomLayout->setSpacing(8);
    zoomLayout->addWidget(fieldLabel(QStringLiteral("Масштаб")));
    auto *zoomPresets = new QComboBox(zoomPage);
    zoomPresets->addItems({QStringLiteral("25 %"), QStringLiteral("50 %"),
                           QStringLiteral("75 %"), QStringLiteral("100 %"),
                           QStringLiteral("150 %"), QStringLiteral("200 %"),
                           QStringLiteral("400 %")});
    zoomPresets->setFixedWidth(90);
    zoomPresets->setCurrentText(QStringLiteral("100 %"));
    zoomLayout->addWidget(zoomPresets);

    auto *fitButton = new QToolButton(zoomPage);
    fitButton->setText(QStringLiteral("По странице"));
    zoomLayout->addWidget(fitButton);

    auto *hundredButton = new QToolButton(zoomPage);
    hundredButton->setText(QStringLiteral("100 %"));
    zoomLayout->addWidget(hundredButton);
    zoomLayout->addWidget(verticalRule(zoomPage));
    zoomLayout->addWidget(fieldLabel(QStringLiteral("Совет: Ctrl + колесо мыши — масштаб, "
                                                     "пробел — панорама")));
    zoomLayout->addStretch(1);

    connect(zoomPresets, &QComboBox::activated, this, [this, zoomPresets](int) {
        applyZoomText(zoomPresets->currentText());
    });
    connect(fitButton, &QToolButton::clicked, this, [this]() { m_view->fitPage(); });
    connect(hundredButton, &QToolButton::clicked, this, [this]() { m_view->actualSize(); });

    // 1 — transform
    auto *transformPage = new QWidget(m_propertyStack);
    auto *transformLayout = new QHBoxLayout(transformPage);
    transformLayout->setContentsMargins(2, 0, 2, 0);
    transformLayout->setSpacing(6);

    const auto addPair = [&](const QString &label, QDoubleSpinBox *spin) {
        transformLayout->addWidget(fieldLabel(label));
        transformLayout->addWidget(spin);
    };
    addPair(QStringLiteral("X"), numberField(transformPage, -10000, 10000, 100.0, QStringLiteral(" px"), 84, 0));
    addPair(QStringLiteral("Y"), numberField(transformPage, -10000, 10000, 320.0, QStringLiteral(" px"), 84, 0));
    addPair(QStringLiteral("Ш"), numberField(transformPage, 0, 10000, 250.0, QStringLiteral(" px"), 84, 0));
    addPair(QStringLiteral("В"), numberField(transformPage, 0, 10000, 250.0, QStringLiteral(" px"), 84, 0));
    addPair(QStringLiteral("∠"), numberField(transformPage, -360, 360, 0.0, QStringLiteral(" °"), 70));
    transformLayout->addWidget(verticalRule(transformPage));
    addPair(QStringLiteral("Скругление"),
            numberField(transformPage, 0, 500, 0.0, QStringLiteral(" px"), 80, 0));
    transformLayout->addWidget(verticalRule(transformPage));

    auto *centerButton = new QToolButton(transformPage);
    centerButton->setText(QStringLiteral("По центру страницы"));
    connect(centerButton, &QToolButton::clicked, this,
            [this]() { showStub(QStringLiteral("Выравнивание по центру страницы")); });
    transformLayout->addWidget(centerButton);
    transformLayout->addStretch(1);

    // 2 — text
    auto *textPage = new QWidget(m_propertyStack);
    auto *textLayout = new QHBoxLayout(textPage);
    textLayout->setContentsMargins(2, 0, 2, 0);
    textLayout->setSpacing(6);

    textLayout->addWidget(fieldLabel(QStringLiteral("Шрифт")));
    auto *fontCombo = new QFontComboBox(textPage);
    fontCombo->setFixedWidth(180);
    fontCombo->setMaxVisibleItems(14);
    textLayout->addWidget(fontCombo);

    auto *sizeSpin = numberField(textPage, 4, 400, 24.0, QStringLiteral(" pt"), 66);
    sizeSpin->setDecimals(0);
    textLayout->addWidget(fieldLabel(QStringLiteral("Кегль")));
    textLayout->addWidget(sizeSpin);

    QFont boldFont = font();
    boldFont.setBold(true);
    QFont italicFont = font();
    italicFont.setItalic(true);
    QFont underlineFont = font();
    underlineFont.setUnderline(true);

    textLayout->addWidget(verticalRule(textPage));
    textLayout->addWidget(textToggle(QStringLiteral("Ж"), QStringLiteral("Полужирный"),
                                     textPage, boldFont));
    textLayout->addWidget(textToggle(QStringLiteral("К"), QStringLiteral("Курсив"),
                                     textPage, italicFont));
    textLayout->addWidget(textToggle(QStringLiteral("Ч"), QStringLiteral("Подчёркнутый"),
                                     textPage, underlineFont));
    textLayout->addSpacing(4);
    textLayout->addWidget(verticalRule(textPage));

    const char *alignIcons[] = {"align-left", "align-hcenter", "align-right"};
    const char *alignTips[] = {"По левому краю", "По центру", "По правому краю"};
    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(textPage);
        button->setIcon(Icons::icon(QString::fromLatin1(alignIcons[i])));
        button->setIconSize(QSize(18, 18));
        button->setCheckable(true);
        button->setToolTip(QString::fromUtf8(alignTips[i]));
        connect(button, &QToolButton::clicked, this,
                [this, tip = QString::fromUtf8(alignTips[i])]() { showStub(tip); });
        textLayout->addWidget(button);
    }

    textLayout->addWidget(verticalRule(textPage));
    auto *styleButton = new QToolButton(textPage);
    styleButton->setText(QStringLiteral("Стили текста"));
    connect(styleButton, &QToolButton::clicked, this,
            [this]() { showStub(QStringLiteral("Стили текста")); });
    textLayout->addWidget(styleButton);
    textLayout->addStretch(1);

    // 3 — fill / outline
    auto *paintPage = new QWidget(m_propertyStack);
    auto *paintLayout = new QHBoxLayout(paintPage);
    paintLayout->setContentsMargins(2, 0, 2, 0);
    paintLayout->setSpacing(8);

    auto *fillButton = new QToolButton(paintPage);
    fillButton->setIcon(Icons::icon(QStringLiteral("swatch"), QColor(0x4f, 0x7c, 0xc0)));
    fillButton->setIconSize(QSize(18, 18));
    fillButton->setText(QStringLiteral("Заливка"));
    fillButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(fillButton, &QToolButton::clicked, this,
            [this]() { showStub(QStringLiteral("Выбор заливки")); });
    paintLayout->addWidget(fillButton);

    auto *outlineButton = new QToolButton(paintPage);
    outlineButton->setIcon(Icons::icon(QStringLiteral("swatch"), QColor(0xe0, 0x46, 0x6b)));
    outlineButton->setIconSize(QSize(18, 18));
    outlineButton->setText(QStringLiteral("Обводка"));
    outlineButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(outlineButton, &QToolButton::clicked, this,
            [this]() { showStub(QStringLiteral("Настройка обводки")); });
    paintLayout->addWidget(outlineButton);

    paintLayout->addWidget(verticalRule(paintPage));
    paintLayout->addWidget(fieldLabel(QStringLiteral("Толщина")));
    paintLayout->addWidget(numberField(paintPage, 0.0, 100.0, 1.0, QStringLiteral(" pt"), 66));

    paintLayout->addWidget(verticalRule(paintPage));
    paintLayout->addWidget(fieldLabel(QStringLiteral("Прозрачность")));
    auto *opacity = new QSlider(Qt::Horizontal, paintPage);
    opacity->setRange(0, 100);
    opacity->setValue(100);
    opacity->setFixedWidth(120);
    paintLayout->addWidget(opacity);
    auto *opacityValue = new QLabel(QStringLiteral("100 %"), paintPage);
    opacityValue->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    opacityValue->setFixedWidth(38);
    paintLayout->addWidget(opacityValue);
    connect(opacity, &QSlider::valueChanged, opacityValue, [opacityValue](int value) {
        opacityValue->setText(QStringLiteral("%1 %").arg(value));
    });

    paintLayout->addStretch(1);

    m_propertyStack->addWidget(zoomPage);      // 0
    m_propertyStack->addWidget(transformPage); // 1
    m_propertyStack->addWidget(textPage);      // 2
    m_propertyStack->addWidget(paintPage);     // 3
    m_propertyStack->setCurrentIndex(m_transformPageIndex);

    bar->addWidget(m_propertyStack);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void MainWindow::createStatusBar()
{
    m_hintLabel = new QLabel(QStringLiteral("Готово к работе"), this);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#9aa1ac; padding-left:6px;"));
    statusBar()->addWidget(m_hintLabel, 1);

    m_coordsLabel = new QLabel(QStringLiteral("X: 0,0  Y: 0,0 мм"), this);
    m_coordsLabel->setMinimumWidth(190);
    m_coordsLabel->setStyleSheet(QStringLiteral("color:#8b929d; padding:0 10px;"));
    statusBar()->addPermanentWidget(m_coordsLabel);

    m_pageLabel = new QLabel(QStringLiteral("Страница 1 из 1"), this);
    m_pageLabel->setStyleSheet(QStringLiteral("color:#8b929d; padding:0 10px;"));
    statusBar()->addPermanentWidget(m_pageLabel);

    m_unitsCombo = new QComboBox(this);
    m_unitsCombo->addItems({QStringLiteral("px"), QStringLiteral("мм"), QStringLiteral("см")});
    m_unitsCombo->setCurrentText(QStringLiteral("мм"));
    m_unitsCombo->setFixedWidth(62);
    m_unitsCombo->setToolTip(QStringLiteral("Единицы измерения"));
    statusBar()->addPermanentWidget(m_unitsCombo);

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->addItems({QStringLiteral("25 %"), QStringLiteral("50 %"),
                           QStringLiteral("75 %"), QStringLiteral("100 %"),
                           QStringLiteral("150 %"), QStringLiteral("200 %"),
                           QStringLiteral("400 %")});
    m_zoomCombo->setCurrentText(QStringLiteral("100 %"));
    m_zoomCombo->setFixedWidth(86);
    m_zoomCombo->setToolTip(QStringLiteral("Масштаб холста"));
    statusBar()->addPermanentWidget(m_zoomCombo);

    connect(m_zoomCombo, &QComboBox::activated, this, [this](int) {
        applyZoomText(m_zoomCombo->currentText());
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        applyZoomText(m_zoomCombo->currentText());
    });
    connect(m_unitsCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
        updateCoordinates(m_cursorScenePos);
    });
}

// ---------------------------------------------------------------------------
// Sample document
// ---------------------------------------------------------------------------

void MainWindow::populateSample()
{
    QFont titleFont(QStringLiteral("Adwaita Sans"));
    titleFont.setPixelSize(58);
    titleFont.setWeight(QFont::Bold);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 11.0);

    auto *title = m_scene->addText(QStringLiteral("VECTORA"), titleFont);
    title->setDefaultTextColor(QColor(0x1d, 0x21, 0x27));
    title->setTextWidth(794);
    title->setPos(0, 96);
    {
        QTextOption option;
        option.setAlignment(Qt::AlignHCenter);
        title->document()->setDefaultTextOption(option);
    }

    QFont subtitleFont(QStringLiteral("Adwaita Sans"));
    subtitleFont.setPixelSize(19);
    subtitleFont.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);

    auto *subtitle = m_scene->addText(QStringLiteral("векторный редактор"), subtitleFont);
    subtitle->setDefaultTextColor(QColor(0x6b, 0x72, 0x80));
    subtitle->setTextWidth(794);
    subtitle->setPos(0, 182);
    {
        QTextOption option;
        option.setAlignment(Qt::AlignHCenter);
        subtitle->document()->setDefaultTextOption(option);
    }

    auto *rule = m_scene->addLine(0, 0, 140, 0, QPen(QColor(0x4f, 0x7c, 0xc0), 4.0));
    rule->setPos(327, 236);

    // Gradient card.
    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(0, 0, 250, 250), 28, 28);
    QLinearGradient cardGradient(0, 0, 250, 250);
    cardGradient.setColorAt(0.0, QColor("#5b8def"));
    cardGradient.setColorAt(1.0, QColor("#8a5cf6"));
    auto *card = m_scene->addPath(cardPath, Qt::NoPen, QBrush(cardGradient));
    card->setPos(100, 320);
    card->setRotation(-6);
    card->setTransformOriginPoint(125, 125);

    auto *circle = m_scene->addEllipse(QRectF(0, 0, 176, 176), Qt::NoPen,
                                       QBrush(QColor("#ff8a3d")));
    circle->setPos(438, 352);
    circle->setOpacity(0.94);

    auto *dot = m_scene->addEllipse(QRectF(0, 0, 54, 54), Qt::NoPen,
                                    QBrush(QColor(0x1d, 0x21, 0x27)));
    dot->setPos(500, 690);

    QPainterPath star = starShape(QPointF(0, 0), 78, 33);
    star.translate(660, 640);
    auto *starItem = m_scene->addPath(star, QPen(QColor("#0f7f7f"), 3.0),
                                      QBrush(QColor("#16a3a3")));
    starItem->setOpacity(0.92);

    QPainterPath curve;
    curve.moveTo(0, 0);
    curve.cubicTo(130, -70, 330, 80, 520, -10);
    auto *curveItem = m_scene->addPath(curve, QPen(QColor("#e0466b"), 5.0, Qt::SolidLine,
                                                   Qt::RoundCap, Qt::RoundJoin));
    curveItem->setPos(128, 742);

    QFont headlineFont(QStringLiteral("Adwaita Sans"));
    headlineFont.setPixelSize(44);
    headlineFont.setWeight(QFont::Bold);

    auto *headline = m_scene->addText(QStringLiteral("Ваш дизайн\nздесь"), headlineFont);
    headline->setDefaultTextColor(QColor(0x1d, 0x21, 0x27));
    headline->setTextWidth(520);
    headline->setPos(100, 842);

    QFont captionFont(QStringLiteral("Adwaita Sans"));
    captionFont.setPixelSize(15);
    captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);

    auto *caption = m_scene->addText(QStringLiteral("интерфейс · этап 1"), captionFont);
    caption->setDefaultTextColor(QColor(0x8b, 0x92, 0x9d));
    caption->setPos(102, 990);

    auto *format = m_scene->addText(QStringLiteral("A4 · 210 × 297 мм"), captionFont);
    format->setDefaultTextColor(QColor(0x9a, 0xa1, 0xac));
    format->setPos(586, 1064);
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

void MainWindow::setActiveTool(Tool tool)
{
    applyTool(tool);
}

void MainWindow::applyTool(Tool tool)
{
    const ToolInfo *info = toolInfo(tool);
    if (!info)
        return;

    m_toolPalette->setCurrentTool(tool);
    m_view->setActiveTool(tool);

    int page = m_transformPageIndex;
    switch (tool) {
    case Tool::Zoom:
    case Tool::Pan:
        page = m_zoomPageIndex;
        break;
    case Tool::Text:
        page = m_textPageIndex;
        break;
    case Tool::Eyedropper:
    case Tool::Fill:
    case Tool::Outline:
    case Tool::Transparency:
    case Tool::Shadow:
    case Tool::Blend:
        page = m_paintPageIndex;
        break;
    default:
        page = m_transformPageIndex;
        break;
    }
    m_propertyStack->setCurrentIndex(page);

    setWindowTitle(QStringLiteral("Vectora — %1").arg(info->name));
    m_hintLabel->setText(QStringLiteral("%1 · %2").arg(info->name, info->hint));
    statusBar()->showMessage(QStringLiteral("Инструмент: %1 (%2)")
                                 .arg(info->name, info->shortcut),
                             3500);
}

void MainWindow::showStub(const QString &feature)
{
    statusBar()->showMessage(
        QStringLiteral("«%1» — следующий этап. Сейчас собран интерфейс приложения.")
            .arg(feature),
        5000);
}

void MainWindow::updateZoomUi(qreal zoom)
{
    const QString text = QStringLiteral("%1 %").arg(qRound(zoom * 100.0));

    if (m_zoomCombo) {
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setCurrentText(text);
    }
    if (m_toolbarZoomCombo) {
        const QSignalBlocker blocker(m_toolbarZoomCombo);
        m_toolbarZoomCombo->setCurrentText(text);
    }
    if (m_hRuler)
        m_hRuler->update();
    if (m_vRuler)
        m_vRuler->update();
}

void MainWindow::applyZoomText(const QString &text)
{
    QString digits;
    for (const QChar ch : text) {
        if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char(','))
            digits.append(ch == QLatin1Char(',') ? QLatin1Char('.') : ch);
    }
    bool ok = false;
    const double percent = digits.toDouble(&ok);
    if (ok && percent > 0.0)
        m_view->setZoom(percent / 100.0);
    else
        updateZoomUi(m_view->zoom());
}

void MainWindow::updateCoordinates(const QPointF &scenePos)
{
    m_cursorScenePos = scenePos;

    double factor = 1.0;
    QString unit = QStringLiteral("мм");
    if (m_unitsCombo) {
        unit = m_unitsCombo->currentText();
        if (unit == QLatin1String("px"))
            factor = 1.0;
        else if (unit == QLatin1String("см"))
            factor = 1.0 / (kPxPerMm * 10.0);
        else
            factor = 1.0 / kPxPerMm;
    }

    m_coordsLabel->setText(QStringLiteral("X: %1  Y: %2 %3")
                               .arg(scenePos.x() * factor, 0, 'f', 1)
                               .arg(scenePos.y() * factor, 0, 'f', 1)
                               .arg(unit));
}

void MainWindow::selectAllObjects()
{
    const QList<QGraphicsItem *> items = m_scene->items();
    for (QGraphicsItem *item : items)
        item->setSelected(true);
    statusBar()->showMessage(QStringLiteral("Выделено объектов: %1").arg(items.size()), 3000);
}

void MainWindow::deleteSelection()
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Нечего удалять: нет выделенных объектов"), 3000);
        return;
    }
    for (QGraphicsItem *item : selected) {
        m_scene->removeItem(item);
        delete item;
    }
    statusBar()->showMessage(QStringLiteral("Удалено объектов: %1").arg(selected.size()), 3000);
}

void MainWindow::showShortcuts()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Горячие клавиши"));
    dialog.resize(660, 560);

    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(false);
    browser->setHtml(QStringLiteral(
        "<h3 style='color:#e3e7ee'>Инструменты</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>V</b></td><td>Выделение</td><td><b>N</b></td><td>Форма (узлы)</td></tr>"
        "<tr><td><b>R</b></td><td>Прямоугольник</td><td><b>E</b></td><td>Эллипс</td></tr>"
        "<tr><td><b>P</b></td><td>Многоугольник</td><td><b>Shift+P</b></td><td>Звезда</td></tr>"
        "<tr><td><b>Shift+S</b></td><td>Спираль</td><td><b>L</b></td><td>Линия</td></tr>"
        "<tr><td><b>B</b></td><td>Кривая Безье</td><td><b>F</b></td><td>Свободная форма</td></tr>"
        "<tr><td><b>A</b></td><td>Художественное перо</td><td><b>T</b></td><td>Текст</td></tr>"
        "<tr><td><b>I</b></td><td>Пипетка</td><td><b>G</b></td><td>Заливка</td></tr>"
        "<tr><td><b>O</b></td><td>Обводка</td><td><b>Shift+T</b></td><td>Прозрачность</td></tr>"
        "<tr><td><b>Shift+D</b></td><td>Тень</td><td><b>Shift+B</b></td><td>Перетекание</td></tr>"
        "<tr><td><b>Z</b></td><td>Масштаб</td><td><b>H</b></td><td>Панорама</td></tr>"
        "<tr><td><b>C</b></td><td>Обрезка</td><td><b>Пробел</b></td><td>Панорама (удерживать)</td></tr>"
        "</table>"
        "<h3 style='color:#e3e7ee'>Файл и правка</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>Ctrl+N</b></td><td>Создать</td><td><b>Ctrl+O</b></td><td>Открыть</td></tr>"
        "<tr><td><b>Ctrl+S</b></td><td>Сохранить</td><td><b>Ctrl+Shift+S</b></td><td>Сохранить как</td></tr>"
        "<tr><td><b>Ctrl+I</b></td><td>Импорт</td><td><b>Ctrl+E</b></td><td>Экспорт</td></tr>"
        "<tr><td><b>Ctrl+Z</b></td><td>Отменить</td><td><b>Ctrl+Shift+Z</b></td><td>Повторить</td></tr>"
        "<tr><td><b>Ctrl+D</b></td><td>Дублировать</td><td><b>Del</b></td><td>Удалить</td></tr>"
        "</table>"
        "<h3 style='color:#e3e7ee'>Просмотр</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>Ctrl + колесо</b></td><td>Масштаб под курсором</td>"
        "<td><b>Ctrl+0</b></td><td>По размеру страницы</td></tr>"
        "<tr><td><b>Ctrl+1</b></td><td>100 %</td><td><b>Ctrl+2</b></td><td>По выделению</td></tr>"
        "<tr><td><b>Ctrl+R</b></td><td>Линейки</td><td><b>F11</b></td><td>Полный экран</td></tr>"
        "</table>"
        "<p style='color:#8b929d'>Часть команд пока показывает сообщение о следующем этапе — "
        "это интерфейсный прототип.</p>"));

    layout->addWidget(browser);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("О программе"),
        QStringLiteral(
            "<h3>Vectora 0.1</h3>"
            "<p>Кроссплатформенный векторный редактор на C++ и Qt 6.</p>"
            "<p>Сейчас готов интерфейс: инструменты, панель свойств, линейки, "
            "слои, палитра и холст с пробной страницей A4.</p>"
            "<p style='color:#666'>Linux · Windows · macOS</p>"));
}
