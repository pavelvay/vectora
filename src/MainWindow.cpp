#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QListWidget>
#include <QFontDatabase>
#include <QFontInfo>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QDesktopServices>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QPrintDialog>
#include <QPushButton>
#include <QPrinter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QImage>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSvgRenderer>
#include <QtMath>

#include "CanvasView.h"
#include "ColorPalette.h"
#include "PageBar.h"
#include "AssetLibrary.h"
#include "AssetPanel.h"
#include "CodeDialog.h"
#include "DocumentDialogs.h"
#include "DocumentTools.h"
#include "TemplateStore.h"
#include "DockPanels.h"
#include "Icons.h"
#include "PageDialog.h"
#include "PatternDialog.h"
#include "PatternStore.h"
#include "Patterns.h"
#include "Ruler.h"
#include "SvgImporter.h"
#include "Theme.h"
#include "UpdateChecker.h"
#include "ToolPalette.h"
#include "VectorItem.h"
#include "WelcomeWidget.h"

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

struct DialogNumber {
    QString label;
    double value = 0.0;
    double minimum = -100000.0;
    double maximum = 100000.0;
    QString suffix;
    int decimals = 2;
};

bool askNumbers(QWidget *parent, const QString &title, QVector<DialogNumber> *rows)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new QFormLayout(&dialog);
    QVector<QDoubleSpinBox *> spins;
    for (const DialogNumber &row : *rows) {
        auto *spin = numberField(&dialog, row.minimum, row.maximum, row.value,
                                 row.suffix, 120, row.decimals);
        form->addRow(row.label, spin);
        spins.append(spin);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    for (int i = 0; i < rows->size() && i < spins.size(); ++i)
        (*rows)[i].value = spins.at(i)->value();
    return true;
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
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::South);

    createActions();
    createCanvas();
    createDocks();
    createStatusBar();
    createMenus();
    createMainToolBar();
    createPropertyBar();
    applyDocument(Document());
    m_modified = false;
    resetHistory();
    updateTitle();
    applyTool(Tool::Select);
    showWelcome();

    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
            [this](const UpdateInfo &info) {
                m_manualUpdateCheck = false;
                if (m_updateDialogOpen)
                    return;
                m_updateDialogOpen = true;
                UpdateDialog dialog(info, this);
                dialog.exec();
                m_updateDialogOpen = false;
            });
    connect(m_updateChecker, &UpdateChecker::upToDate, this, [this](const QString &version) {
        if (m_manualUpdateCheck) {
            m_manualUpdateCheck = false;
            QMessageBox::information(this, QStringLiteral("Обновления"),
                                     QStringLiteral("Установлена последняя версия: %1.")
                                         .arg(version));
        }
    });
    connect(m_updateChecker, &UpdateChecker::failed, this, [this](const QString &error) {
        if (!m_manualUpdateCheck) {
            statusBar()->showMessage(QStringLiteral("Не удалось проверить обновления."), 4000);
            return;
        }
        m_manualUpdateCheck = false;
        QMessageBox::warning(this, QStringLiteral("Обновления"), error);
    });
    connect(m_view, &CanvasView::statusHint, this, [this](const QString &text) {
        m_lastHint = text;
        m_lastHintTime = QDateTime::currentDateTime();
    });
    if (m_dockPanels) {
        connect(m_dockPanels, &DockPanels::status, this, [this](const QString &text) {
            m_lastHint = text;
            m_lastHintTime = QDateTime::currentDateTime();
        });
    }
    m_autosaveSession = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setupAutosave();
    if (!qEnvironmentVariableIsSet("VECTORA_NO_RECOVERY"))
        QTimer::singleShot(600, this, [this]() { recoverAutosaves(); });

    {
        QSettings settings;
        const bool automatic = settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool();
        const bool disabled = qEnvironmentVariableIsSet("VECTORA_NO_UPDATE_CHECK");
        if (automatic && !disabled) {
            QTimer::singleShot(2500, this, [this]() {
                if (m_updateChecker)
                    m_updateChecker->check(false);
            });
        }
    }

    QTimer::singleShot(0, this, [this]() {
        if (m_centralStack && m_centralStack->currentWidget() == m_editorPage)
            m_view->fitPage();
    });
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
    QAction *act = makeAction(QStringLiteral("new"), QStringLiteral("Создать"),
                              QKeySequence::New, QStringLiteral("Создать новый документ"), false);
    connect(act, &QAction::triggered, this, &MainWindow::newDocument);

    act = makeAction(QStringLiteral("open"), QStringLiteral("Открыть…"),
                     QKeySequence::Open, QStringLiteral("Открыть документ"), false);
    connect(act, &QAction::triggered, this, &MainWindow::openDocument);

    act = makeAction(QStringLiteral("save"), QStringLiteral("Сохранить"),
                     QKeySequence::Save, QStringLiteral("Сохранить документ"), false);
    connect(act, &QAction::triggered, this, &MainWindow::saveDocument);

    act = makeAction(QStringLiteral("save-as"), QStringLiteral("Сохранить как…"),
                     QKeySequence::SaveAs, QStringLiteral("Сохранить документ под новым именем"), false);
    connect(act, &QAction::triggered, this, &MainWindow::saveDocumentAs);

    act = makeAction(QStringLiteral("home"), QStringLiteral("Главная страница"),
                     QKeySequence(QStringLiteral("Ctrl+Shift+H")),
                     QStringLiteral("Открыть стартовую страницу"), false);
    connect(act, &QAction::triggered, this, &MainWindow::showWelcome);

    act = makeAction(QStringLiteral("import"), QStringLiteral("Импорт…"),
                     QKeySequence(QStringLiteral("Ctrl+I")),
                     QStringLiteral("Импортировать SVG или изображение"), false);
    connect(act, &QAction::triggered, this, &MainWindow::importFile);

    act = makeAction(QStringLiteral("export"), QStringLiteral("Экспорт…"),
                     QKeySequence(QStringLiteral("Ctrl+E")),
                     QStringLiteral("Экспортировать в PNG или SVG"), false);
    connect(act, &QAction::triggered, this, &MainWindow::exportFile);

    act = makeAction(QStringLiteral("pdf"), QStringLiteral("Экспорт в PDF…"),
                     QKeySequence(), QStringLiteral("Экспортировать документ в PDF"), false);
    connect(act, &QAction::triggered, this, &MainWindow::exportPdf);

    act = makeAction(QStringLiteral("export-pages"), QStringLiteral("Экспорт всех страниц…"),
                     QKeySequence(), QStringLiteral("Сохранить каждую страницу в PNG"), false);
    connect(act, &QAction::triggered, this, &MainWindow::exportAllPages);

    act = makeAction(QStringLiteral("print"), QStringLiteral("Печать…"),
                     QKeySequence::Print, QStringLiteral("Печать документа"), false);
    connect(act, &QAction::triggered, this, &MainWindow::printDocument);

    auto *quit = makeAction(QStringLiteral("quit"), QStringLiteral("Выход"),
                            QKeySequence::Quit, QStringLiteral("Закрыть приложение"), false);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // Edit
    act = makeAction(QStringLiteral("undo"), QStringLiteral("Отменить"),
                     QKeySequence::Undo, QStringLiteral("Отменить последнее действие"), false);
    connect(act, &QAction::triggered, this, &MainWindow::undo);
    act->setEnabled(false);

    act = makeAction(QStringLiteral("redo"), QStringLiteral("Повторить"),
                     QKeySequence::Redo, QStringLiteral("Повторить отменённое действие"), false);
    connect(act, &QAction::triggered, this, &MainWindow::redo);
    act->setEnabled(false);
    makeAction(QStringLiteral("cut"), QStringLiteral("Вырезать"),
               QKeySequence::Cut, QStringLiteral("Вырезать выделенные объекты"), false);
    makeAction(QStringLiteral("copy"), QStringLiteral("Копировать"),
               QKeySequence::Copy, QStringLiteral("Копировать выделенные объекты"), false);
    makeAction(QStringLiteral("paste"), QStringLiteral("Вставить"),
               QKeySequence::Paste, QStringLiteral("Вставить объекты из буфера"), false);
    makeAction(QStringLiteral("duplicate"), QStringLiteral("Дублировать"),
               QKeySequence(QStringLiteral("Ctrl+D")), QStringLiteral("Создать копию выделения"), false);
    makeAction(QStringLiteral("step-repeat"), QStringLiteral("Шаг и повтор…"),
               QKeySequence(QStringLiteral("Ctrl+Shift+D")),
               QStringLiteral("Создать несколько копий со смещением"), false);
    makeAction(QStringLiteral("clone"), QStringLiteral("Клонировать"),
               QKeySequence(), QStringLiteral("Создать связанный клон объекта"), false);
    makeAction(QStringLiteral("delete"), QStringLiteral("Удалить"),
               QKeySequence::Delete, QStringLiteral("Удалить выделенные объекты"), false);
    makeAction(QStringLiteral("select-all"), QStringLiteral("Выделить всё"),
               QKeySequence::SelectAll, QStringLiteral("Выделить все объекты"), false);
    makeAction(QStringLiteral("preferences"), QStringLiteral("Настройки…"),
               QKeySequence(QStringLiteral("Ctrl+,")), QStringLiteral("Настройки приложения"), false);
    makeAction(QStringLiteral("history"), QStringLiteral("История изменений…"),
               QKeySequence(), QStringLiteral("Список действий и переход к состоянию"), false);
    makeAction(QStringLiteral("versions"), QStringLiteral("Версии документа…"),
               QKeySequence(), QStringLiteral("Именованные версии и сравнение"), false);
    makeAction(QStringLiteral("templates"), QStringLiteral("Шаблоны документов…"),
               QKeySequence(), QStringLiteral("Создать документ из шаблона"), false);
    makeAction(QStringLiteral("search-objects"), QStringLiteral("Поиск объектов…"),
               QKeySequence(QStringLiteral("Ctrl+F")), QStringLiteral("Поиск объектов по имени и типу"), false);
    makeAction(QStringLiteral("package"), QStringLiteral("Упаковать документ…"),
               QKeySequence(), QStringLiteral("Собрать документ, изображения и отчёт"), false);

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
               QKeySequence(QStringLiteral("Ctrl+2")), QStringLiteral("Приблизить выделенные объекты"), false);
    makeAction(QStringLiteral("rulers"), QStringLiteral("Линейки"),
               QKeySequence(QStringLiteral("Ctrl+R")), QStringLiteral("Показать или скрыть линейки"));
    makeAction(QStringLiteral("guides"), QStringLiteral("Направляющие"),
               QKeySequence(), QStringLiteral("Показать или скрыть направляющие"));
    makeAction(QStringLiteral("clear-guides"), QStringLiteral("Удалить направляющие"),
               QKeySequence(), QStringLiteral("Убрать все направляющие с холста"), false);
    auto *printAreaAction = makeAction(QStringLiteral("print-area"),
                                       QStringLiteral("Область печати"),
                                       QKeySequence(),
                                       QStringLiteral("Показать область печати"),
                                       false);
    printAreaAction->setCheckable(true);
    printAreaAction->setChecked(false);
    makeAction(QStringLiteral("grid"), QStringLiteral("Сетка"),
               QKeySequence(QStringLiteral("Ctrl+Alt+G")), QStringLiteral("Показать или скрыть сетку"));
    makeAction(QStringLiteral("snap"), QStringLiteral("Привязка"),
               QKeySequence(), QStringLiteral("Привязка к сетке, объектам и направляющим"));
    makeAction(QStringLiteral("snap-settings"), QStringLiteral("Сетка и привязка…"),
               QKeySequence(), QStringLiteral("Шаг сетки и чувствительность привязки"), false);
    auto *fullscreen = makeAction(QStringLiteral("fullscreen"), QStringLiteral("Полноэкранный режим"),
                                  QKeySequence(QStringLiteral("F11")), QStringLiteral("Во весь экран"));

    // Object
    makeAction(QStringLiteral("group"), QStringLiteral("Сгруппировать"),
               QKeySequence(QStringLiteral("Ctrl+G")), QStringLiteral("Сгруппировать выделенные объекты"), false);
    makeAction(QStringLiteral("ungroup"), QStringLiteral("Разгруппировать"),
               QKeySequence(QStringLiteral("Ctrl+U")), QStringLiteral("Разгруппировать выделение"), false);
    makeAction(QStringLiteral("ungroup-all"), QStringLiteral("Разгруппировать всё"),
               QKeySequence(QStringLiteral("Ctrl+Shift+U")), QStringLiteral("Расформировать все вложенные группы"), false);
    makeAction(QStringLiteral("order-front"), QStringLiteral("На передний план"),
               QKeySequence(QStringLiteral("Ctrl+Home")), QStringLiteral("Поднять объект на передний план"), false);
    makeAction(QStringLiteral("order-forward"), QStringLiteral("На уровень выше"),
               QKeySequence(QStringLiteral("Ctrl+Up")), QStringLiteral("Поднять объект на уровень выше"), false);
    makeAction(QStringLiteral("order-backward"), QStringLiteral("На уровень ниже"),
               QKeySequence(QStringLiteral("Ctrl+Down")), QStringLiteral("Опустить объект на уровень ниже"), false);
    makeAction(QStringLiteral("order-back"), QStringLiteral("На задний план"),
               QKeySequence(QStringLiteral("Ctrl+End")), QStringLiteral("Опустить объект на задний план"), false);
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
               QKeySequence(), QStringLiteral("Отразить объект слева направо"), false);
    makeAction(QStringLiteral("flip-v"), QStringLiteral("Отразить по вертикали"),
               QKeySequence(), QStringLiteral("Отразить объект сверху вниз"), false);
    makeAction(QStringLiteral("rotate"), QStringLiteral("Повернуть на 90°"),
               QKeySequence(), QStringLiteral("Повернуть объект на 90 градусов"), false);
    makeAction(QStringLiteral("skew"), QStringLiteral("Наклонить…"),
               QKeySequence(), QStringLiteral("Наклонить объект по горизонтали и вертикали"), false);
    makeAction(QStringLiteral("bool-union"), QStringLiteral("Объединение"),
               QKeySequence(), QStringLiteral("Логическое объединение контуров"), false);
    makeAction(QStringLiteral("bool-difference"), QStringLiteral("Вычитание"),
               QKeySequence(), QStringLiteral("Вычесть верхние объекты из нижнего"), false);
    makeAction(QStringLiteral("bool-intersection"), QStringLiteral("Пересечение"),
               QKeySequence(), QStringLiteral("Оставить общую область контуров"), false);
    makeAction(QStringLiteral("bool-exclude"), QStringLiteral("Исключение"),
               QKeySequence(), QStringLiteral("Убрать пересекающиеся области"), false);
    makeAction(QStringLiteral("bool-divide"), QStringLiteral("Разделение"),
               QKeySequence(), QStringLiteral("Разделить пересечения на отдельные области"), false);
    makeAction(QStringLiteral("bool-trim"), QStringLiteral("Обрезка"),
               QKeySequence(), QStringLiteral("Обрезать объекты верхним контуром"), false);
    makeAction(QStringLiteral("contour"), QStringLiteral("Контур…"),
               QKeySequence(), QStringLiteral("Создать внутренние и внешние контуры"), false);
    makeAction(QStringLiteral("text-on-path"), QStringLiteral("Текст по контуру"),
               QKeySequence(), QStringLiteral("Разместить текст вдоль кривой"), false);
    makeAction(QStringLiteral("text-in-shape"), QStringLiteral("Текст в фигуре"),
               QKeySequence(), QStringLiteral("Вписать текст в фигуру"), false);
    makeAction(QStringLiteral("text-detach"), QStringLiteral("Отделить текст"),
               QKeySequence(), QStringLiteral("Убрать текст с контура или из фигуры"), false);
    makeAction(QStringLiteral("text-opentype"), QStringLiteral("OpenType и оси…"),
               QKeySequence(), QStringLiteral("OpenType-функции и вариативные оси шрифта"), false);
    makeAction(QStringLiteral("text-fonts"), QStringLiteral("Менеджер шрифтов…"),
               QKeySequence(), QStringLiteral("Обзор, поиск и избранное шрифтов"), false);
    makeAction(QStringLiteral("shape-builder-tool"), QStringLiteral("Создание фигур"),
               QKeySequence(), QStringLiteral("Инструмент слияния областей"), false);
    makeAction(QStringLiteral("convert-curves"), QStringLiteral("Преобразовать в кривые"),
               QKeySequence(QStringLiteral("Ctrl+Q")), QStringLiteral("Преобразовать текст в редактируемые кривые"), false);
    makeAction(QStringLiteral("lock"), QStringLiteral("Заблокировать"),
               QKeySequence(), QStringLiteral("Заблокировать объект от изменений"));

    // Effects
    makeAction(QStringLiteral("fx-shadow"), QStringLiteral("Тень…"),
               QKeySequence(), QStringLiteral("Добавить отбрасываемую тень"), false);
    makeAction(QStringLiteral("fx-transparency"), QStringLiteral("Прозрачность…"),
               QKeySequence(), QStringLiteral("Настроить прозрачность"), false);
    makeAction(QStringLiteral("fx-blend"), QStringLiteral("Перетекание…"),
               QKeySequence(), QStringLiteral("Создать перетекание между объектами"), false);
    makeAction(QStringLiteral("fx-envelope"), QStringLiteral("Оболочка…"),
               QKeySequence(), QStringLiteral("Деформировать объект оболочкой"));
    makeAction(QStringLiteral("fx-extrude"), QStringLiteral("Экструзия…"),
               QKeySequence(), QStringLiteral("Придать объекту объём"));
    makeAction(QStringLiteral("fx-lens"), QStringLiteral("Линза (размытие)…"),
               QKeySequence(), QStringLiteral("Наложить эффект линзы"));
    makeAction(QStringLiteral("fx-bevel"), QStringLiteral("Скос (внутренняя тень)…"),
               QKeySequence(), QStringLiteral("Добавить фаску по краю"));
    makeAction(QStringLiteral("fx-glow"), QStringLiteral("Свечение"),
               QKeySequence(), QStringLiteral("Добавить внешнее свечение"), false);
    makeAction(QStringLiteral("fx-blur"), QStringLiteral("Размытие"),
               QKeySequence(), QStringLiteral("Добавить размытие"), false);
    makeAction(QStringLiteral("fx-distort"), QStringLiteral("Искажение"),
               QKeySequence(), QStringLiteral("Добавить волновое искажение"), false);
    makeAction(QStringLiteral("fx-warp"), QStringLiteral("Деформация"),
               QKeySequence(), QStringLiteral("Добавить радиальную деформацию"), false);
    makeAction(QStringLiteral("fx-perspective"), QStringLiteral("Перспектива"),
               QKeySequence(), QStringLiteral("Добавить перспективное искажение"), false);
    makeAction(QStringLiteral("fx-clip"), QStringLiteral("Обтравочная маска"),
               QKeySequence(), QStringLiteral("Обрезать объект фигурой-маской"), false);
    makeAction(QStringLiteral("fx-opacity"), QStringLiteral("Маска прозрачности"),
               QKeySequence(), QStringLiteral("Применить маску прозрачности"), false);
    makeAction(QStringLiteral("fx-clear"), QStringLiteral("Очистить эффекты"),
               QKeySequence(), QStringLiteral("Удалить все эффекты выделения"), false);
    makeAction(QStringLiteral("fx-panel"), QStringLiteral("Панель эффектов…"),
               QKeySequence(), QStringLiteral("Открыть панель эффектов"), false);

    // Symbols and the asset library
    makeAction(QStringLiteral("symbol-create"), QStringLiteral("Создать символ из выделения…"),
               QKeySequence(QStringLiteral("Ctrl+K")),
               QStringLiteral("Создать символ из выделенных объектов"), false);
    makeAction(QStringLiteral("symbol-update"), QStringLiteral("Обновить символ по экземпляру"),
               QKeySequence(QStringLiteral("Ctrl+Shift+K")),
               QStringLiteral("Перенести изменения экземпляра в исходный символ"), false);
    makeAction(QStringLiteral("symbol-detach"), QStringLiteral("Отделить экземпляр"),
               QKeySequence(QStringLiteral("Ctrl+Alt+K")),
               QStringLiteral("Отвязать экземпляр от исходного символа"), false);
    makeAction(QStringLiteral("symbol-rename"), QStringLiteral("Переименовать символ…"),
               QKeySequence(), QStringLiteral("Переименовать символ документа"), false);
    makeAction(QStringLiteral("symbol-delete"), QStringLiteral("Удалить символ"),
               QKeySequence(), QStringLiteral("Удалить символ из документа"), false);
    makeAction(QStringLiteral("symbol-panel"), QStringLiteral("Панель символов…"),
               QKeySequence(), QStringLiteral("Открыть панель символов"), false);
    makeAction(QStringLiteral("library-add"), QStringLiteral("Добавить выделение в библиотеку…"),
               QKeySequence(QStringLiteral("Ctrl+Shift+L")),
               QStringLiteral("Сохранить выделение как ресурс библиотеки"), false);
    makeAction(QStringLiteral("library-panel"), QStringLiteral("Панель библиотеки…"),
               QKeySequence(), QStringLiteral("Открыть библиотеку ресурсов"), false);
    makeAction(QStringLiteral("library-new"), QStringLiteral("Создать библиотеку…"),
               QKeySequence(), QStringLiteral("Создать новую библиотеку ресурсов"));
    makeAction(QStringLiteral("library-import"), QStringLiteral("Импортировать библиотеку…"),
               QKeySequence(), QStringLiteral("Загрузить библиотеку из файла .vlib"));
    makeAction(QStringLiteral("library-export"), QStringLiteral("Экспортировать библиотеку…"),
               QKeySequence(), QStringLiteral("Сохранить библиотеку в файл .vlib"));
    makeAction(QStringLiteral("library-undo"), QStringLiteral("Отменить изменение библиотеки"),
               QKeySequence(), QStringLiteral("Отменить последнее изменение библиотеки"));
    // Drafting
    makeAction(QStringLiteral("dimension-horizontal"), QStringLiteral("Горизонтальный размер"),
               QKeySequence(), QStringLiteral("Линейный горизонтальный размер"));
    makeAction(QStringLiteral("dimension-vertical"), QStringLiteral("Вертикальный размер"),
               QKeySequence(), QStringLiteral("Линейный вертикальный размер"));
    makeAction(QStringLiteral("dimension-aligned"), QStringLiteral("Наклонный размер"),
               QKeySequence(), QStringLiteral("Размер вдоль наклонной линии"));
    makeAction(QStringLiteral("dimension-angular"), QStringLiteral("Угловой размер"),
               QKeySequence(), QStringLiteral("Измерить угол между двумя лучами"));
    makeAction(QStringLiteral("dimension-radius"), QStringLiteral("Радиус"),
               QKeySequence(), QStringLiteral("Радиус окружности или эллипса"));
    makeAction(QStringLiteral("dimension-diameter"), QStringLiteral("Диаметр"),
               QKeySequence(), QStringLiteral("Диаметр окружности или эллипса"));
    makeAction(QStringLiteral("construct-line"), QStringLiteral("Линия по координатам…"),
               QKeySequence(), QStringLiteral("Построить линию по двум точкам"));
    makeAction(QStringLiteral("construct-angle"), QStringLiteral("Линия по длине и углу…"),
               QKeySequence(), QStringLiteral("Построить линию заданной длины под углом"));
    makeAction(QStringLiteral("construct-parallel"), QStringLiteral("Параллельная линия…"),
               QKeySequence(), QStringLiteral("Построить параллель к выделенной линии"));
    makeAction(QStringLiteral("construct-perpendicular"), QStringLiteral("Перпендикуляр…"),
               QKeySequence(), QStringLiteral("Построить перпендикуляр из середины линии"));
    makeAction(QStringLiteral("construct-tangent"), QStringLiteral("Касательная из точки…"),
               QKeySequence(), QStringLiteral("Построить касательные к выделенному эллипсу"));
    makeAction(QStringLiteral("construct-circle"), QStringLiteral("Окружность по радиусу…"),
               QKeySequence(), QStringLiteral("Построить окружность заданного радиуса"));
    makeAction(QStringLiteral("construct-arc"), QStringLiteral("Дуга по радиусу…"),
               QKeySequence(), QStringLiteral("Построить дугу заданного радиуса"));
    makeAction(QStringLiteral("construct-polygon"), QStringLiteral("Правильный многоугольник…"),
               QKeySequence(), QStringLiteral("Построить правильный многоугольник"));
    makeAction(QStringLiteral("construct-copy"), QStringLiteral("Копия со смещением…"),
               QKeySequence(), QStringLiteral("Точное копирование с заданным смещением"));
    makeAction(QStringLiteral("scale-selection"), QStringLiteral("Масштабировать выделение…"),
               QKeySequence(), QStringLiteral("Масштабировать объекты на коэффициент"));
    makeAction(QStringLiteral("grid-cartesian"), QStringLiteral("Декартова сетка"),
               QKeySequence(), QStringLiteral("Переключить сетку на декартову"));
    makeAction(QStringLiteral("grid-polar"), QStringLiteral("Полярная сетка"),
               QKeySequence(), QStringLiteral("Переключить сетку на полярную"));
    makeAction(QStringLiteral("grid-isometric"), QStringLiteral("Изометрическая сетка"),
               QKeySequence(), QStringLiteral("Переключить сетку на изометрическую"));
    makeAction(QStringLiteral("pattern-create"), QStringLiteral("Создать узор из выделения…"),
               QKeySequence(QStringLiteral("Ctrl+Alt+P")),
               QStringLiteral("Создать узор из выделенных объектов"), false);
    makeAction(QStringLiteral("pattern-edit"), QStringLiteral("Редактировать узор…"),
               QKeySequence(), QStringLiteral("Изменить повторение и ячейку узора"), false);
    makeAction(QStringLiteral("pattern-update"), QStringLiteral("Обновить узор из выделения"),
               QKeySequence(), QStringLiteral("Заменить элементы узора выделением"), false);
    makeAction(QStringLiteral("pattern-expand"), QStringLiteral("Разобрать узор на объекты"),
               QKeySequence(), QStringLiteral("Преобразовать узорную заливку в объекты"), false);
    makeAction(QStringLiteral("pattern-save-library"), QStringLiteral("Сохранить узор в библиотеку"),
               QKeySequence(), QStringLiteral("Добавить узор в общую библиотеку"), false);
    makeAction(QStringLiteral("code-qr"), QStringLiteral("QR-код…"),
               QKeySequence(QStringLiteral("Ctrl+Alt+Q")),
               QStringLiteral("Создать QR-код из текста или ссылки"), false);
    makeAction(QStringLiteral("code-ean13"), QStringLiteral("Штрихкод EAN-13…"),
               QKeySequence(), QStringLiteral("Создать штрихкод EAN-13"), false);
    makeAction(QStringLiteral("code-ean8"), QStringLiteral("Штрихкод EAN-8…"),
               QKeySequence(), QStringLiteral("Создать штрихкод EAN-8"), false);
    makeAction(QStringLiteral("code-upca"), QStringLiteral("Штрихкод UPC-A…"),
               QKeySequence(), QStringLiteral("Создать штрихкод UPC-A"), false);
    makeAction(QStringLiteral("code-code128"), QStringLiteral("Штрихкод Code 128…"),
               QKeySequence(), QStringLiteral("Создать штрихкод Code 128"), false);
    makeAction(QStringLiteral("code-code39"), QStringLiteral("Штрихкод Code 39…"),
               QKeySequence(), QStringLiteral("Создать штрихкод Code 39"), false);
    makeAction(QStringLiteral("code-datamatrix"), QStringLiteral("Data Matrix…"),
               QKeySequence(), QStringLiteral("Создать код Data Matrix"), false);
    makeAction(QStringLiteral("code-edit"), QStringLiteral("Редактировать код…"),
               QKeySequence(), QStringLiteral("Изменить данные и параметры кода"), false);
    makeAction(QStringLiteral("pattern-library"), QStringLiteral("Библиотека узоров…"),
               QKeySequence(), QStringLiteral("Открыть вкладку узоров"), false);
    makeAction(QStringLiteral("drawing-scale"), QStringLiteral("Масштаб чертежа…"),
               QKeySequence(), QStringLiteral("Задать масштаб 1:N и реальные размеры"));

    makeAction(QStringLiteral("library-redo"), QStringLiteral("Вернуть изменение библиотеки"),
               QKeySequence(QStringLiteral("Ctrl+Alt+Z")),
               QStringLiteral("Повторить отменённое изменение библиотеки"));

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
    makeAction(QStringLiteral("check-updates"), QStringLiteral("Проверить обновления…"),
               QKeySequence(), QStringLiteral("Проверить новую версию на GitHub"), false);
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
    connect(action(QStringLiteral("zoom-selection")), &QAction::triggered, this,
            [this]() { m_view->zoomToSelection(); });
    connect(action(QStringLiteral("duplicate")), &QAction::triggered, this, [this]() {
        const QList<VectorItem *> selected = m_view->selectedVectorItems();
        if (selected.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Нет объектов для дублирования."), 3000);
            return;
        }
        beginEdit();
        m_view->scene()->clearSelection();
        for (VectorItem *item : selected) {
            VectorObject copy = item->object();
            copy.pos += QPointF(16.0, 16.0);
            copy.group = 0;
            copy.id = 0;
            copy.masterId = 0;
            VectorItem *added = m_view->addVectorObject(copy, false, false);
            if (added)
                added->setSelected(true);
        }
        endEdit();
    });
    connect(action(QStringLiteral("step-repeat")), &QAction::triggered, this, [this]() {
        if (m_view->selectedVectorItems().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите объекты."), 3000);
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Шаг и повтор"));
        auto *form = new QFormLayout(&dialog);
        auto *count = new QSpinBox(&dialog);
        count->setRange(1, 200);
        count->setValue(5);
        auto *dx = new QDoubleSpinBox(&dialog);
        dx->setRange(-2000, 2000);
        dx->setValue(20);
        dx->setSuffix(QStringLiteral(" px"));
        auto *dy = new QDoubleSpinBox(&dialog);
        dy->setRange(-2000, 2000);
        dy->setValue(20);
        dy->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Количество копий"), count);
        form->addRow(QStringLiteral("Смещение X"), dx);
        form->addRow(QStringLiteral("Смещение Y"), dy);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            m_view->duplicateSelection(count->value(),
                                       QPointF(dx->value(), dy->value()));
    });
    connect(action(QStringLiteral("clone")), &QAction::triggered, this,
            [this]() { m_view->cloneSelection(); });
    connect(action(QStringLiteral("skew")), &QAction::triggered, this, [this]() {
        if (m_view->selectedVectorItems().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите объекты."), 3000);
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Наклонить"));
        auto *form = new QFormLayout(&dialog);
        auto *ax = new QDoubleSpinBox(&dialog);
        ax->setRange(-75, 75);
        ax->setValue(15);
        ax->setSuffix(QStringLiteral(" °"));
        auto *ay = new QDoubleSpinBox(&dialog);
        ay->setRange(-75, 75);
        ay->setValue(0);
        ay->setSuffix(QStringLiteral(" °"));
        form->addRow(QStringLiteral("По горизонтали"), ax);
        form->addRow(QStringLiteral("По вертикали"), ay);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            m_view->skewSelection(ax->value(), ay->value());
    });
    connect(action(QStringLiteral("convert-curves")), &QAction::triggered, this,
            [this]() { m_view->convertSelectionToCurves(); });
    connect(action(QStringLiteral("shape-builder-tool")), &QAction::triggered, this,
            [this]() { setActiveTool(Tool::ShapeBuilder); });
    connect(action(QStringLiteral("text-on-path")), &QAction::triggered, this,
            [this]() { m_view->applyTextOnPath(); });
    connect(action(QStringLiteral("text-in-shape")), &QAction::triggered, this,
            [this]() { m_view->applyTextInShape(); });
    connect(action(QStringLiteral("text-detach")), &QAction::triggered, this,
            [this]() { m_view->detachTextSpecial(); });
    connect(action(QStringLiteral("text-character")), &QAction::triggered, this, [this]() {
        QList<VectorItem *> texts;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Text)
                texts.append(item);
        }
        if (texts.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите текстовый объект."), 3000);
            return;
        }
        const VectorObject first = texts.first()->object();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Символ и типографика"));
        auto *form = new QFormLayout(&dialog);
        auto *family = new QFontComboBox(&dialog);
        family->setCurrentFont(first.font);
        auto *size = new QDoubleSpinBox(&dialog);
        size->setRange(4, 400);
        size->setValue(first.font.pixelSize() > 0 ? first.font.pixelSize() : 18);
        auto *bold = new QCheckBox(QStringLiteral("Жирный"), &dialog);
        bold->setChecked(first.font.bold());
        auto *italic = new QCheckBox(QStringLiteral("Курсив"), &dialog);
        italic->setChecked(first.font.italic());
        auto *underline = new QCheckBox(QStringLiteral("Подчёркивание"), &dialog);
        underline->setChecked(first.underline);
        auto *strikeOut = new QCheckBox(QStringLiteral("Зачёркивание"), &dialog);
        strikeOut->setChecked(first.strikeOut);
        auto *script = new QComboBox(&dialog);
        script->addItems({QStringLiteral("Обычный"), QStringLiteral("Верхний индекс"),
                          QStringLiteral("Нижний индекс")});
        script->setCurrentIndex(qBound(0, first.script, 2));
        auto *tracking = new QDoubleSpinBox(&dialog);
        tracking->setRange(-20, 40);
        tracking->setValue(first.letterSpacing);
        tracking->setSuffix(QStringLiteral(" px"));
        auto *wordSpacing = new QDoubleSpinBox(&dialog);
        wordSpacing->setRange(-20, 60);
        wordSpacing->setValue(first.wordSpacing);
        wordSpacing->setSuffix(QStringLiteral(" px"));
        auto *hScale = new QDoubleSpinBox(&dialog);
        hScale->setRange(10, 400);
        hScale->setValue(first.horizontalScale * 100.0);
        hScale->setSuffix(QStringLiteral(" %"));
        auto *vScale = new QDoubleSpinBox(&dialog);
        vScale->setRange(10, 400);
        vScale->setValue(first.verticalScale * 100.0);
        vScale->setSuffix(QStringLiteral(" %"));
        auto *baseline = new QDoubleSpinBox(&dialog);
        baseline->setRange(-200, 200);
        baseline->setValue(first.baselineShift);
        baseline->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Шрифт"), family);
        form->addRow(QStringLiteral("Размер"), size);
        auto *styleRow = new QHBoxLayout;
        styleRow->addWidget(bold);
        styleRow->addWidget(italic);
        styleRow->addWidget(underline);
        styleRow->addWidget(strikeOut);
        form->addRow(QStringLiteral("Начертание"), styleRow);
        form->addRow(QStringLiteral("Индекс"), script);
        form->addRow(QStringLiteral("Трекинг"), tracking);
        form->addRow(QStringLiteral("Межсловный интервал"), wordSpacing);
        form->addRow(QStringLiteral("Горизонтальный масштаб"), hScale);
        form->addRow(QStringLiteral("Вертикальный масштаб"), vScale);
        form->addRow(QStringLiteral("Сдвиг базовой линии"), baseline);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        emit beginEdit();
        for (VectorItem *item : texts) {
            VectorObject object = item->object();
            QFont font = family->currentFont();
            font.setPixelSize(qRound(size->value()));
            font.setBold(bold->isChecked());
            font.setItalic(italic->isChecked());
            object.font = font;
            object.underline = underline->isChecked();
            object.strikeOut = strikeOut->isChecked();
            object.script = script->currentIndex();
            object.letterSpacing = tracking->value();
            object.wordSpacing = wordSpacing->value();
            object.horizontalScale = hScale->value() / 100.0;
            object.verticalScale = vScale->value() / 100.0;
            object.baselineShift = baseline->value();
            item->setObject(object);
        }
        emit endEdit();
    });
    connect(action(QStringLiteral("text-opentype")), &QAction::triggered, this, [this]() {
        QList<VectorItem *> texts;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Text)
                texts.append(item);
        }
        if (texts.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите текстовый объект."), 3000);
            return;
        }
        const VectorObject first = texts.first()->object();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("OpenType и вариативные оси"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *featuresLabel = new QLabel(QStringLiteral("OpenType-функции"), &dialog);
        featuresLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
        layout->addWidget(featuresLabel);

        struct Feature {
            const char *tag;
            const char *name;
        };
        static const Feature features[] = {
            {"liga", "Стандартные лигатуры"},
            {"dlig", "Дискреционные лигатуры"},
            {"smcp", "Капитель (small caps)"},
            {"onum", "Старостильные цифры"},
            {"tnum", "Табличные цифры"},
            {"frac", "Дроби"},
            {"zero", "Перечёркнутый ноль"},
        };
        QMap<QString, QCheckBox *> boxes;
        for (const Feature &feature : features) {
            auto *box = new QCheckBox(QString::fromUtf8(feature.name), &dialog);
            box->setChecked(first.fontFeatures.contains(QString::fromLatin1(feature.tag)));
            layout->addWidget(box);
            boxes.insert(QString::fromLatin1(feature.tag), box);
        }

        auto *axesLabel = new QLabel(QStringLiteral("Вариативные оси"), &dialog);
        axesLabel->setStyleSheet(QStringLiteral("font-weight:600; margin-top:8px;"));
        layout->addWidget(axesLabel);

        QMap<QString, QDoubleSpinBox *> axisSpins;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        const QList<QFontVariableAxis> axes =
            QFontInfo(first.font).variableAxes();
        if (axes.isEmpty()) {
            auto *none = new QLabel(
                QStringLiteral("У выбранного шрифта нет вариативных осей."), &dialog);
            none->setWordWrap(true);
            none->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
            layout->addWidget(none);
        }
        for (const QFontVariableAxis &axis : axes) {
            auto *row = new QHBoxLayout;
            auto *label = new QLabel(
                QStringLiteral("%1 (%2)").arg(axis.name(), axis.tag().toString()),
                &dialog);
            label->setMinimumWidth(180);
            auto *spin = new QDoubleSpinBox(&dialog);
            spin->setRange(axis.minimumValue(), axis.maximumValue());
            spin->setDecimals(1);
            const QString tag = axis.tag().toString();
            spin->setValue(first.variableAxes.value(tag, axis.defaultValue()));
            row->addWidget(label);
            row->addWidget(spin, 1);
            layout->addLayout(row);
            axisSpins.insert(tag, spin);
        }
#else
        auto *none = new QLabel(
            QStringLiteral("Вариативные оси доступны в сборках на Qt 6.7 и новее."),
            &dialog);
        none->setWordWrap(true);
        none->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
        layout->addWidget(none);
#endif

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;

        emit beginEdit();
        for (VectorItem *item : texts) {
            VectorObject object = item->object();
            object.fontFeatures.clear();
            for (auto it = boxes.constBegin(); it != boxes.constEnd(); ++it) {
                if (it.value()->isChecked())
                    object.fontFeatures.append(it.key());
            }
            object.variableAxes.clear();
            for (auto it = axisSpins.constBegin(); it != axisSpins.constEnd(); ++it)
                object.variableAxes.insert(it.key(), it.value()->value());
            item->setObject(object);
        }
        emit endEdit();
    });

    const auto showFontManager = [this]() {
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        QStringList favorites = settings.value(QStringLiteral("fonts/favorites"))
                                    .toStringList();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Менеджер шрифтов"));
        dialog.resize(760, 520);
        auto *layout = new QVBoxLayout(&dialog);

        auto *topRow = new QHBoxLayout;
        auto *search = new QLineEdit(&dialog);
        search->setPlaceholderText(QStringLiteral("Поиск шрифта…"));
        auto *category = new QComboBox(&dialog);
        category->addItems({QStringLiteral("Все шрифты"), QStringLiteral("Без засечек"),
                            QStringLiteral("С засечками"), QStringLiteral("Моноширинные"),
                            QStringLiteral("Избранные")});
        topRow->addWidget(search, 1);
        topRow->addWidget(category);
        layout->addLayout(topRow);

        auto *body = new QHBoxLayout;
        auto *fontList = new QListWidget(&dialog);
        auto *side = new QVBoxLayout;
        auto *preview = new QLabel(QStringLiteral("Аа Бб Вв 0123"), &dialog);
        preview->setMinimumHeight(90);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet(QStringLiteral(
            "background:#12141a; border:1px solid #3a3f47; border-radius:6px;"));
        auto *styles = new QListWidget(&dialog);
        auto *info = new QLabel(&dialog);
        info->setWordWrap(true);
        info->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
        auto *favoriteButton = new QPushButton(QStringLiteral("В избранное"), &dialog);
        side->addWidget(preview);
        side->addWidget(styles);
        side->addWidget(info);
        side->addWidget(favoriteButton);
        body->addWidget(fontList, 1);
        body->addLayout(side, 1);
        layout->addLayout(body, 1);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        const QStringList families = QFontDatabase::families();
        const auto rebuild = [&]() {
            const QString query = search->text().trimmed();
            const int mode = category->currentIndex();
            fontList->clear();
            for (const QString &family : families) {
                if (!query.isEmpty()
                    && !family.contains(query, Qt::CaseInsensitive))
                    continue;
                if (mode == 4 && !favorites.contains(family))
                    continue;
                if (mode > 0 && mode < 4) {
                    const QFont::StyleHint hint = QFont(family).styleHint();
                    if (mode == 1 && hint == QFont::Serif)
                        continue;
                    if (mode == 2 && hint != QFont::Serif)
                        continue;
                    if (mode == 3 && !QFontDatabase::isFixedPitch(family))
                        continue;
                }
                auto *entry = new QListWidgetItem(family, fontList);
                entry->setFont(QFont(family, 11));
                if (favorites.contains(family))
                    entry->setText(family + QStringLiteral("  ★"));
            }
        };
        const auto refreshSelection = [&]() {
            const QString family = fontList->currentItem()
                                       ? fontList->currentItem()->text().remove(QStringLiteral("  ★"))
                                       : QString();
            styles->clear();
            if (family.isEmpty())
                return;
            preview->setFont(QFont(family, 24));
            preview->setText(QStringLiteral("Аа Бб Вв 0123"));
            for (const QString &style : QFontDatabase::styles(family))
                styles->addItem(style);
            QStringList axes;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            const auto variableAxes = QFontInfo(QFont(family)).variableAxes();
            for (const auto &axis : variableAxes)
                axes << axis.name();
#endif
            info->setText(QStringLiteral("Семейство: %1\nНачертаний: %2\n%3")
                              .arg(family)
                              .arg(QFontDatabase::styles(family).size())
                              .arg(axes.isEmpty()
                                       ? QStringLiteral("Variable Fonts: нет осей")
                                       : QStringLiteral("Оси: %1").arg(axes.join(QStringLiteral(", ")))));
        };
        connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &) { rebuild(); });
        connect(category, &QComboBox::currentIndexChanged, &dialog,
                [&](int) { rebuild(); });
        connect(fontList, &QListWidget::currentRowChanged, &dialog,
                [&](int) { refreshSelection(); });
        connect(favoriteButton, &QPushButton::clicked, &dialog, [&]() {
            if (!fontList->currentItem())
                return;
            QString family = fontList->currentItem()->text();
            family.remove(QStringLiteral("  ★"));
            if (favorites.contains(family))
                favorites.removeAll(family);
            else
                favorites.append(family);
            rebuild();
        });

        rebuild();
        if (fontList->count() > 0)
            fontList->setCurrentRow(0);

        if (dialog.exec() != QDialog::Accepted)
            return;
        settings.setValue(QStringLiteral("fonts/favorites"), favorites);
        if (!fontList->currentItem())
            return;
        QString family = fontList->currentItem()->text();
        family.remove(QStringLiteral("  ★"));
        const QString style = styles->currentItem()
                                  ? styles->currentItem()->text()
                                  : QString();
        QList<VectorItem *> texts;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Text)
                texts.append(item);
        }
        if (texts.isEmpty())
            return;
        emit beginEdit();
        for (VectorItem *item : texts) {
            VectorObject object = item->object();
            QFont font(family);
            font.setPixelSize(object.font.pixelSize() > 0 ? object.font.pixelSize() : 18);
            if (!style.isEmpty()) {
                const QFont styled(family, font.pixelSize(), QFontDatabase::weight(family, style),
                                   QFontDatabase::italic(family, style));
                font = styled;
                font.setPixelSize(object.font.pixelSize() > 0 ? object.font.pixelSize() : 18);
            }
            object.font = font;
            item->setObject(object);
        }
        emit endEdit();
        settings.setValue(QStringLiteral("fonts/favorites"), favorites);
    };
    connect(action(QStringLiteral("text-styles")), &QAction::triggered, this,
            showFontManager);
    connect(action(QStringLiteral("text-fonts")), &QAction::triggered, this,
            showFontManager);
    connect(action(QStringLiteral("text-symbol")), &QAction::triggered, this, [this]() {
        const QString symbols = QStringLiteral(
            "© ® ™ ° ± × ÷ — – « » „ “ ” € ₽ № → ← ↑ ↓ • … † ‡ § ¶ µ ¼ ½ ¾ ⅓ ⅔ "
            "α β γ δ π Ω ∞ ≈ ≠ ≤ ≥ √ ∑ ∏ ∫ ∂ ∆ ∇ ∅ ★ ☆ ✦ ✓ ✗ ⚑ ☀ ☂ ❄ ♠ ♣ ♥ ♦");
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Вставить символ"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *list = new QListWidget(&dialog);
        list->setViewMode(QListView::IconMode);
        list->setResizeMode(QListView::Adjust);
        list->setGridSize(QSize(42, 42));
        for (const QChar &symbol : symbols) {
            if (symbol == QLatin1Char(' '))
                continue;
            auto *entry = new QListWidgetItem(QString(symbol), list);
            entry->setTextAlignment(Qt::AlignCenter);
            entry->setFont(QFont(QStringLiteral("Noto Sans"), 16));
        }
        layout->addWidget(list);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted || !list->currentItem())
            return;
        const QString symbol = list->currentItem()->text();
        QList<VectorItem *> texts;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Text)
                texts.append(item);
        }
        if (texts.isEmpty()) {
            QApplication::clipboard()->setText(symbol);
            statusBar()->showMessage(QStringLiteral("Символ скопирован в буфер."), 3000);
            return;
        }
        emit beginEdit();
        for (VectorItem *item : texts) {
            VectorObject object = item->object();
            object.text += symbol;
            item->setObject(object);
        }
        emit endEdit();
    });
    connect(action(QStringLiteral("text-paragraph")), &QAction::triggered, this, [this]() {
        QList<VectorItem *> texts;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Text)
                texts.append(item);
        }
        if (texts.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите текстовый объект."), 3000);
            return;
        }
        const VectorObject first = texts.first()->object();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Абзац"));
        auto *form = new QFormLayout(&dialog);
        auto *align = new QComboBox(&dialog);
        align->addItems({QStringLiteral("По левому краю"), QStringLiteral("По центру"),
                         QStringLiteral("По правому краю")});
        align->setCurrentIndex(qBound(0, first.textAlign, 2));
        auto *lineSpacing = new QDoubleSpinBox(&dialog);
        lineSpacing->setRange(0.5, 4.0);
        lineSpacing->setSingleStep(0.05);
        lineSpacing->setValue(first.lineSpacing);
        auto *paragraph = new QDoubleSpinBox(&dialog);
        paragraph->setRange(0, 300);
        paragraph->setValue(first.paragraphSpacing);
        paragraph->setSuffix(QStringLiteral(" px"));
        auto *indent = new QDoubleSpinBox(&dialog);
        indent->setRange(0, 300);
        indent->setValue(first.firstLineIndent);
        indent->setSuffix(QStringLiteral(" px"));
        auto *width = new QDoubleSpinBox(&dialog);
        width->setRange(20, 5000);
        width->setValue(first.textBox.width());
        width->setSuffix(QStringLiteral(" px"));
        auto *height = new QDoubleSpinBox(&dialog);
        height->setRange(20, 5000);
        height->setValue(first.textBox.height());
        height->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Выравнивание"), align);
        form->addRow(QStringLiteral("Межстрочный интервал"), lineSpacing);
        form->addRow(QStringLiteral("Межабзацный интервал"), paragraph);
        form->addRow(QStringLiteral("Отступ первой строки"), indent);
        form->addRow(QStringLiteral("Ширина блока"), width);
        form->addRow(QStringLiteral("Высота блока"), height);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        emit beginEdit();
        for (VectorItem *item : texts) {
            VectorObject object = item->object();
            object.textAlign = align->currentIndex();
            object.lineSpacing = lineSpacing->value();
            object.paragraphSpacing = paragraph->value();
            object.firstLineIndent = indent->value();
            QRectF box = object.textBox;
            box.setSize(QSizeF(width->value(), height->value()));
            object.textBox = box;
            item->setObject(object);
        }
        emit endEdit();
    });
    connect(action(QStringLiteral("fx-envelope")), &QAction::triggered, this,
            [this]() { setActiveTool(Tool::Envelope); });
    connect(action(QStringLiteral("contour")), &QAction::triggered, this, [this]() {
        if (m_view->selectedVectorItems().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите объекты для контура."), 3000);
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Контур"));
        auto *form = new QFormLayout(&dialog);
        auto *steps = new QSpinBox(&dialog);
        steps->setRange(1, 40);
        steps->setValue(3);
        auto *distance = new QDoubleSpinBox(&dialog);
        distance->setRange(0.5, 400);
        distance->setValue(4.0);
        distance->setSuffix(QStringLiteral(" px"));
        auto *direction = new QComboBox(&dialog);
        direction->addItems({QStringLiteral("Наружу"), QStringLiteral("Внутрь"),
                             QStringLiteral("В обе стороны")});
        auto *filled = new QCheckBox(QStringLiteral("Заливать контуры"), &dialog);
        QColor color = m_view->strokeColor();
        auto *colorButton = new QPushButton(&dialog);
        colorButton->setText(color.name().toUpper());
        colorButton->setStyleSheet(QStringLiteral("text-align: left; padding: 4px;"));
        QObject::connect(colorButton, &QPushButton::clicked, &dialog, [&color, colorButton]() {
            const QColor picked = QColorDialog::getColor(color, nullptr,
                                                         QStringLiteral("Цвет контура"));
            if (picked.isValid()) {
                color = picked;
                colorButton->setText(color.name().toUpper());
            }
        });
        form->addRow(QStringLiteral("Количество шагов"), steps);
        form->addRow(QStringLiteral("Расстояние"), distance);
        form->addRow(QStringLiteral("Направление"), direction);
        form->addRow(QStringLiteral("Цвет"), colorButton);
        form->addRow(QString(), filled);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            m_view->applyContour(steps->value(), distance->value(),
                                 direction->currentIndex(), color,
                                 filled->isChecked());
    });
    for (const auto &entry : {
             std::make_pair(QStringLiteral("bool-union"), CanvasView::BooleanOperation::Union),
             std::make_pair(QStringLiteral("bool-difference"), CanvasView::BooleanOperation::Difference),
             std::make_pair(QStringLiteral("bool-intersection"), CanvasView::BooleanOperation::Intersection),
             std::make_pair(QStringLiteral("bool-exclude"), CanvasView::BooleanOperation::Exclude),
             std::make_pair(QStringLiteral("bool-divide"), CanvasView::BooleanOperation::Divide),
             std::make_pair(QStringLiteral("bool-trim"), CanvasView::BooleanOperation::Trim)}) {
        connect(action(entry.first), &QAction::triggered, this,
                [this, op = entry.second]() { m_view->applyBooleanOperation(op); });
    }
    connect(action(QStringLiteral("group")), &QAction::triggered, this,
            [this]() { m_view->groupSelection(); });
    connect(action(QStringLiteral("ungroup")), &QAction::triggered, this,
            [this]() { m_view->ungroupSelection(); });
    connect(action(QStringLiteral("ungroup-all")), &QAction::triggered, this,
            [this]() { m_view->ungroupAll(); });
    connect(action(QStringLiteral("order-front")), &QAction::triggered, this,
            [this]() { m_view->bringToFront(); refreshQuickTransform(); });
    connect(action(QStringLiteral("order-forward")), &QAction::triggered, this,
            [this]() { m_view->bringForward(); refreshQuickTransform(); });
    connect(action(QStringLiteral("order-backward")), &QAction::triggered, this,
            [this]() { m_view->sendBackward(); refreshQuickTransform(); });
    connect(action(QStringLiteral("order-back")), &QAction::triggered, this,
            [this]() { m_view->sendToBack(); refreshQuickTransform(); });
    connect(action(QStringLiteral("flip-h")), &QAction::triggered, this,
            [this]() { m_view->flipSelection(true); });
    connect(action(QStringLiteral("flip-v")), &QAction::triggered, this,
            [this]() { m_view->flipSelection(false); });
    connect(action(QStringLiteral("rotate")), &QAction::triggered, this,
            [this]() { m_view->rotateSelection(90.0); refreshQuickTransform(); });
    const auto addEffectAndShow = [this](const QString &type) {
        if (m_view->selectedVectorItems().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выберите объекты."), 3000);
            return;
        }
        m_view->addEffectToSelection(makeEffect(type));
        showDockerTab(QStringLiteral("Эффекты"), 3);
    };
    connect(action(QStringLiteral("fx-shadow")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("shadow")); });
    connect(action(QStringLiteral("fx-transparency")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 1); });
    connect(action(QStringLiteral("fx-blend")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 2); });
    connect(action(QStringLiteral("fx-extrude")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("extrude")); });
    connect(action(QStringLiteral("fx-lens")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("blur")); });
    connect(action(QStringLiteral("fx-bevel")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("inner-shadow")); });
    connect(action(QStringLiteral("fx-glow")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("glow")); });
    connect(action(QStringLiteral("fx-blur")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("blur")); });
    connect(action(QStringLiteral("fx-distort")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("distortion")); });
    connect(action(QStringLiteral("fx-warp")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("warp")); });
    connect(action(QStringLiteral("fx-perspective")), &QAction::triggered, this,
            [addEffectAndShow]() { addEffectAndShow(QStringLiteral("perspective")); });
    connect(action(QStringLiteral("fx-clip")), &QAction::triggered, this,
            [this]() {
                m_view->applyMaskFromSelection(QStringLiteral("clip-mask"));
                showDockerTab(QStringLiteral("Эффекты"), 3);
            });
    connect(action(QStringLiteral("fx-opacity")), &QAction::triggered, this,
            [this]() {
                m_view->applyMaskFromSelection(QStringLiteral("opacity-mask"));
                showDockerTab(QStringLiteral("Эффекты"), 3);
            });
    connect(action(QStringLiteral("fx-clear")), &QAction::triggered, this,
            [this]() { m_view->setSelectionEffects({}); });
    connect(action(QStringLiteral("fx-panel")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 3); });

    const auto showLibrary = [this]() {
        if (m_assetDock) {
            m_assetDock->show();
            m_assetDock->raise();
        }
    };
    connect(action(QStringLiteral("symbol-create")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel) {
                    m_assetPanel->showSymbolsTab();
                    m_assetPanel->refreshSymbols();
                    m_assetPanel->createSymbol();
                }
            });
    connect(action(QStringLiteral("symbol-update")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel) {
                    m_assetPanel->showSymbolsTab();
                    m_assetPanel->updateSymbol();
                }
            });
    connect(action(QStringLiteral("symbol-detach")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel) {
                    m_assetPanel->showSymbolsTab();
                    m_assetPanel->detachSymbol();
                }
            });
    connect(action(QStringLiteral("symbol-rename")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->renameSymbol();
            });
    connect(action(QStringLiteral("symbol-delete")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->deleteSymbol();
            });
    connect(action(QStringLiteral("symbol-panel")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel) {
                    m_assetPanel->showSymbolsTab();
                    m_assetPanel->refreshSymbols();
                }
            });
    connect(action(QStringLiteral("library-add")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel) {
                    m_assetPanel->showAssetsTab();
                    m_assetPanel->addSelectionToLibrary();
                }
            });
    connect(action(QStringLiteral("library-panel")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->showAssetsTab();
            });
    connect(action(QStringLiteral("library-new")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->newLibrary();
            });
    connect(action(QStringLiteral("library-import")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->importLibrary();
            });
    connect(action(QStringLiteral("library-export")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->exportLibrary();
            });
    connect(action(QStringLiteral("library-undo")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                AssetStore::instance().undo();
            });
    connect(action(QStringLiteral("library-redo")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                AssetStore::instance().redo();
            });

    connect(action(QStringLiteral("history")), &QAction::triggered, this,
            &MainWindow::openHistoryDialog);
    connect(action(QStringLiteral("versions")), &QAction::triggered, this,
            &MainWindow::openVersionDialog);
    connect(action(QStringLiteral("templates")), &QAction::triggered, this,
            &MainWindow::openTemplateDialog);
    connect(action(QStringLiteral("search-objects")), &QAction::triggered, this,
            &MainWindow::openResourceSearch);
    connect(action(QStringLiteral("package")), &QAction::triggered, this,
            &MainWindow::packageCurrentDocument);
    connect(action(QStringLiteral("preferences")), &QAction::triggered, this,
            &MainWindow::showPreferences);

    connect(action(QStringLiteral("check-updates")), &QAction::triggered, this, [this]() {
        if (!m_updateChecker) {
            m_updateChecker = new UpdateChecker(this);
        }
        m_manualUpdateCheck = true;
        m_updateChecker->check(true);
        statusBar()->showMessage(QStringLiteral("Проверка обновлений…"), 3000);
    });

    // Drafting: dimensions
    const QStringList dimensionActions = {
        QStringLiteral("dimension-horizontal"), QStringLiteral("dimension-vertical"),
        QStringLiteral("dimension-aligned"), QStringLiteral("dimension-angular"),
        QStringLiteral("dimension-radius"), QStringLiteral("dimension-diameter"),
    };
    for (int i = 0; i < dimensionActions.size(); ++i) {
        connect(action(dimensionActions.at(i)), &QAction::triggered, this,
                [this, i]() { startDimensionTool(i); });
    }

    // Drafting: exact constructions
    connect(action(QStringLiteral("construct-line")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("X1"), centre.x(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Y1"), centre.y(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("X2"), centre.x() + 100.0, -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Y2"), centre.y() + 100.0, -100000, 100000, QStringLiteral(" px"), 1},
        };
        if (!askNumbers(this, QStringLiteral("Линия по координатам"), &rows))
            return;
        m_view->constructLine(QPointF(rows.at(0).value, rows.at(1).value),
                              QPointF(rows.at(2).value, rows.at(3).value));
    });
    connect(action(QStringLiteral("construct-angle")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("Начало X"), centre.x(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Начало Y"), centre.y(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Длина"), 100.0, 0.1, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Угол"), 45.0, -360, 360, QStringLiteral(" °"), 1},
        };
        if (!askNumbers(this, QStringLiteral("Линия по длине и углу"), &rows))
            return;
        m_view->constructLineFrom(QPointF(rows.at(0).value, rows.at(1).value),
                                  rows.at(2).value, rows.at(3).value);
    });
    connect(action(QStringLiteral("construct-parallel")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        QVector<DialogNumber> rows = {
            {QStringLiteral("Смещение"), 20.0, -10000, 10000, QStringLiteral(" px"), 1},
        };
        if (askNumbers(this, QStringLiteral("Параллельная линия"), &rows))
            m_view->createParallelFromSelection(rows.at(0).value);
    });
    connect(action(QStringLiteral("construct-perpendicular")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        QVector<DialogNumber> rows = {
            {QStringLiteral("Длина"), 100.0, 0.1, 100000, QStringLiteral(" px"), 1},
        };
        if (askNumbers(this, QStringLiteral("Перпендикуляр"), &rows))
            m_view->createPerpendicularFromSelection(rows.at(0).value);
    });
    connect(action(QStringLiteral("construct-tangent")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("Точка X"), centre.x() + 220.0, -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Точка Y"), centre.y() - 180.0, -100000, 100000, QStringLiteral(" px"), 1},
        };
        if (askNumbers(this, QStringLiteral("Касательная из точки"), &rows))
            m_view->createTangentFromSelection(QPointF(rows.at(0).value, rows.at(1).value));
    });
    connect(action(QStringLiteral("construct-circle")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("Центр X"), centre.x(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Центр Y"), centre.y(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Радиус"), 60.0, 0.1, 100000, QStringLiteral(" px"), 1},
        };
        if (askNumbers(this, QStringLiteral("Окружность по радиусу"), &rows))
            m_view->constructCircle(QPointF(rows.at(0).value, rows.at(1).value),
                                    rows.at(2).value);
    });
    connect(action(QStringLiteral("construct-arc")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("Центр X"), centre.x(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Центр Y"), centre.y(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Радиус"), 60.0, 0.1, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Начало"), 0.0, -360, 360, QStringLiteral(" °"), 1},
            {QStringLiteral("Разворот"), 120.0, -360, 360, QStringLiteral(" °"), 1},
        };
        if (askNumbers(this, QStringLiteral("Дуга по радиусу"), &rows))
            m_view->constructArc(QPointF(rows.at(0).value, rows.at(1).value),
                                 rows.at(2).value, rows.at(3).value, rows.at(4).value);
    });
    connect(action(QStringLiteral("construct-polygon")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        const QSizeF page = m_view->pageSize();
        const QPointF centre(page.width() / 2.0, page.height() / 2.0);
        QVector<DialogNumber> rows = {
            {QStringLiteral("Центр X"), centre.x(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Центр Y"), centre.y(), -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Сторон"), 6.0, 3, 64, QString(), 0},
            {QStringLiteral("Радиус"), 70.0, 0.1, 100000, QStringLiteral(" px"), 1},
        };
        if (askNumbers(this, QStringLiteral("Правильный многоугольник"), &rows))
            m_view->constructPolygon(QPointF(rows.at(0).value, rows.at(1).value),
                                     qRound(rows.at(2).value), rows.at(3).value);
    });
    connect(action(QStringLiteral("construct-copy")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        QVector<DialogNumber> rows = {
            {QStringLiteral("Смещение X"), 20.0, -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Смещение Y"), 20.0, -100000, 100000, QStringLiteral(" px"), 1},
            {QStringLiteral("Копий"), 3.0, 1, 200, QString(), 0},
        };
        if (askNumbers(this, QStringLiteral("Копия со смещением"), &rows))
            m_view->duplicateSelection(qRound(rows.at(2).value),
                                       QPointF(rows.at(0).value, rows.at(1).value));
    });
    connect(action(QStringLiteral("scale-selection")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        QVector<DialogNumber> rows = {
            {QStringLiteral("Коэффициент"), 1.5, 0.01, 1000, QString(), 3},
        };
        if (askNumbers(this, QStringLiteral("Масштабировать выделение"), &rows))
            m_view->scaleSelectionBy(rows.at(0).value);
    });
    for (int type = 0; type < 3; ++type) {
        const QString id = type == 0 ? QStringLiteral("grid-cartesian")
                                     : (type == 1 ? QStringLiteral("grid-polar")
                                                  : QStringLiteral("grid-isometric"));
        connect(action(id), &QAction::triggered, this, [this, type]() {
            if (!m_view)
                return;
            m_view->setGridType(type);
            m_view->setShowGrid(true);
            statusBar()->showMessage(QStringLiteral("Сетка: %1")
                                         .arg(type == 0 ? QStringLiteral("декартова")
                                              : (type == 1 ? QStringLiteral("полярная")
                                                           : QStringLiteral("изометрическая"))),
                                     3000);
        });
    }
    connect(action(QStringLiteral("drawing-scale")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        QVector<DialogNumber> rows = {
            {QStringLiteral("Масштаб 1 :"), m_view->drawingScale(), 0.01, 1000, QString(), 2},
        };
        if (!askNumbers(this, QStringLiteral("Масштаб чертежа"), &rows))
            return;
        m_view->setDrawingScale(rows.at(0).value);
        updateScaleLabel();
        updateRulers();
    });

    connect(action(QStringLiteral("pattern-create")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        if (m_view->selectedVectorItems().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Выделите объекты для узора."), 3000);
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Новый узор"), QStringLiteral("Название узора:"),
            QLineEdit::Normal,
            QStringLiteral("Узор %1").arg(m_view->patterns().size() + 1), &ok);
        if (!ok)
            return;
        const int id = m_view->createPatternFromSelection(name);
        if (id <= 0)
            return;
        PatternDialog dialog(m_view, m_view->patternDefinition(id), this);
        dialog.exec();
        if (m_assetPanel)
            m_assetPanel->refreshPatterns();
    });

    connect(action(QStringLiteral("pattern-edit")), &QAction::triggered, this, [this]() {
        if (!m_view)
            return;
        int patternId = m_view->selectedPatternId();
        if (patternId <= 0 && !m_view->patterns().isEmpty()) {
            QStringList names;
            for (const PatternDefinition &pattern : m_view->patterns()) {
                names.append(QStringLiteral("%1 (%2)")
                                 .arg(pattern.name, patternTypeName(pattern.settings.type)));
            }
            bool ok = false;
            const QString chosen = QInputDialog::getItem(
                this, QStringLiteral("Редактировать узор"), QStringLiteral("Узор:"),
                names, 0, false, &ok);
            if (!ok)
                return;
            patternId = m_view->patterns().at(names.indexOf(chosen)).id;
        }
        if (patternId <= 0) {
            statusBar()->showMessage(
                QStringLiteral("Выделите объект с узорной заливкой или создайте узор."),
                3500);
            return;
        }
        PatternDialog dialog(m_view, m_view->patternDefinition(patternId), this);
        dialog.exec();
        if (m_assetPanel)
            m_assetPanel->refreshPatterns();
    });

    connect(action(QStringLiteral("pattern-update")), &QAction::triggered, this,
            [this]() {
                if (!m_view)
                    return;
                int patternId = m_view->selectedPatternId();
                if (patternId <= 0 && !m_view->patterns().isEmpty()) {
                    QStringList names;
                    for (const PatternDefinition &pattern : m_view->patterns()) {
                        names.append(QStringLiteral("%1 (%2)").arg(
                            pattern.name, patternTypeName(pattern.settings.type)));
                    }
                    bool ok = false;
                    const QString chosen = QInputDialog::getItem(
                        this, QStringLiteral("Обновить узор"),
                        QStringLiteral("Узор:"), names, 0, false, &ok);
                    if (!ok)
                        return;
                    patternId = m_view->patterns().at(names.indexOf(chosen)).id;
                }
                if (patternId > 0)
                    m_view->updatePatternFromSelection(patternId);
            });

    connect(action(QStringLiteral("pattern-expand")), &QAction::triggered, this,
            [this]() {
                if (m_view)
                    m_view->expandSelectedPatterns();
            });

    connect(action(QStringLiteral("pattern-save-library")), &QAction::triggered, this,
            [this]() {
                if (!m_view)
                    return;
                const int patternId = m_view->selectedPatternId();
                if (patternId <= 0) {
                    statusBar()->showMessage(
                        QStringLiteral("Выделите объект с узорной заливкой."), 3000);
                    return;
                }
                PatternDefinition pattern = m_view->patternDefinition(patternId);
                pattern.id = 0;
                PatternStore::instance().load();
                if (PatternStore::instance().add(pattern) >= 0) {
                    statusBar()->showMessage(
                        QStringLiteral("Узор «%1» сохранён в библиотеку.")
                            .arg(pattern.name),
                        3000);
                    if (m_assetPanel)
                        m_assetPanel->refreshPatterns();
                }
            });

    connect(action(QStringLiteral("pattern-library")), &QAction::triggered, this,
            [this, showLibrary]() {
                showLibrary();
                if (m_assetPanel)
                    m_assetPanel->showPatternsTab();
            });

    const QStringList codeActions = {
        QStringLiteral("code-qr"), QStringLiteral("code-ean13"),
        QStringLiteral("code-ean8"), QStringLiteral("code-upca"),
        QStringLiteral("code-code128"), QStringLiteral("code-code39"),
        QStringLiteral("code-datamatrix"),
    };
    for (int kind = 0; kind < codeActions.size(); ++kind) {
        connect(action(codeActions.at(kind)), &QAction::triggered, this,
                [this, kind]() { openCodeDialog(nullptr, kind); });
    }
    connect(action(QStringLiteral("code-edit")), &QAction::triggered, this, [this]() {
        VectorItem *target = nullptr;
        if (m_view) {
            for (VectorItem *item : m_view->selectedVectorItems()) {
                if (item->object().type == ShapeType::Code) {
                    target = item;
                    break;
                }
            }
        }
        if (!target) {
            statusBar()->showMessage(
                QStringLiteral("Выделите объект кода для редактирования."), 3000);
            return;
        }
        openCodeDialog(target, -1);
    });
    connect(m_view, &CanvasView::codeEditRequested, this,
            [this](VectorItem *item) { openCodeDialog(item, -1); });
    connect(action(QStringLiteral("copy")), &QAction::triggered, this,
            &MainWindow::copySelection);
    connect(action(QStringLiteral("cut")), &QAction::triggered, this,
            &MainWindow::cutSelection);
    connect(action(QStringLiteral("paste")), &QAction::triggered, this,
            &MainWindow::pasteClipboard);
    connect(action(QStringLiteral("shortcuts")), &QAction::triggered, this,
            &MainWindow::showShortcuts);
    connect(action(QStringLiteral("about")), &QAction::triggered, this,
            &MainWindow::showAbout);

    auto *gridAction = action(QStringLiteral("grid"));
    gridAction->setCheckable(true);
    gridAction->setChecked(false);
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
    connect(snapAction, &QAction::toggled, this, [this](bool on) {
        if (!m_view)
            return;
        m_view->setSnapEnabled(on);
        statusBar()->showMessage(on ? QStringLiteral("Привязка к сетке и направляющим включена")
                                    : QStringLiteral("Привязка выключена"), 3000);
    });

    auto *guidesAction = action(QStringLiteral("guides"));
    guidesAction->setCheckable(true);
    guidesAction->setChecked(true);
    connect(guidesAction, &QAction::toggled, this, [this](bool on) {
        if (!m_view)
            return;
        m_view->setGuidesVisible(on);
    });
    connect(printAreaAction, &QAction::toggled, this, [this](bool on) {
        if (m_view)
            m_view->setShowPrintableArea(on);
    });

    connect(action(QStringLiteral("clear-guides")), &QAction::triggered, this,
            [this]() { m_view->clearGuides(); });
    connect(action(QStringLiteral("snap-settings")), &QAction::triggered, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Сетка и привязка"));
        auto *form = new QFormLayout(&dialog);

        auto *gridType = new QComboBox(&dialog);
        gridType->addItems({QStringLiteral("Декартова"), QStringLiteral("Полярная"),
                            QStringLiteral("Изометрическая")});
        gridType->setCurrentIndex(m_view->gridType());
        auto *step = new QSpinBox(&dialog);
        step->setRange(2, 400);
        step->setValue(m_view->gridStep());
        step->setSuffix(QStringLiteral(" px"));
        auto *angle = new QDoubleSpinBox(&dialog);
        angle->setRange(1.0, 90.0);
        angle->setValue(m_view->gridAngle());
        angle->setSuffix(QStringLiteral(" °"));
        auto *originX = new QDoubleSpinBox(&dialog);
        originX->setRange(-10000.0, 10000.0);
        originX->setValue(m_view->gridOrigin().x());
        originX->setSuffix(QStringLiteral(" px"));
        auto *originY = new QDoubleSpinBox(&dialog);
        originY->setRange(-10000.0, 10000.0);
        originY->setValue(m_view->gridOrigin().y());
        originY->setSuffix(QStringLiteral(" px"));
        auto *threshold = new QDoubleSpinBox(&dialog);
        threshold->setRange(1, 60);
        threshold->setValue(m_view->snapThreshold());
        threshold->setSuffix(QStringLiteral(" px"));

        form->addRow(QStringLiteral("Тип сетки"), gridType);
        form->addRow(QStringLiteral("Шаг сетки"), step);
        form->addRow(QStringLiteral("Угол полярной сетки"), angle);
        form->addRow(QStringLiteral("Начало координат X"), originX);
        form->addRow(QStringLiteral("Начало координат Y"), originY);
        form->addRow(QStringLiteral("Чувствительность привязки"), threshold);

        struct SnapToggle {
            QString key;
            QString label;
            QCheckBox *box = nullptr;
        };
        QVector<SnapToggle> toggles = {
            {QStringLiteral("grid"), QStringLiteral("Сетка"), nullptr},
            {QStringLiteral("guides"), QStringLiteral("Направляющие"), nullptr},
            {QStringLiteral("objects"), QStringLiteral("Края объектов"), nullptr},
            {QStringLiteral("nodes"), QStringLiteral("Узлы"), nullptr},
            {QStringLiteral("centers"), QStringLiteral("Центры"), nullptr},
            {QStringLiteral("midpoints"), QStringLiteral("Середины"), nullptr},
            {QStringLiteral("intersections"), QStringLiteral("Пересечения"), nullptr},
            {QStringLiteral("edges"), QStringLiteral("Ближайшие края"), nullptr},
            {QStringLiteral("tangents"), QStringLiteral("Касания"), nullptr},
        };
        auto *snapGrid = new QGridLayout;
        int row = 0;
        for (SnapToggle &toggle : toggles) {
            toggle.box = new QCheckBox(toggle.label, &dialog);
            toggle.box->setChecked(m_view->snapOption(toggle.key));
            snapGrid->addWidget(toggle.box, row / 3, row % 3);
            ++row;
        }
        form->addRow(QStringLiteral("Привязка к"), snapGrid);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;

        m_view->setGridType(gridType->currentIndex());
        m_view->setGridStep(step->value());
        m_view->setGridAngle(angle->value());
        m_view->setGridOrigin(QPointF(originX->value(), originY->value()));
        m_view->setSnapThreshold(threshold->value());
        for (const SnapToggle &toggle : toggles)
            m_view->setSnapOption(toggle.key, toggle.box->isChecked());
        statusBar()->showMessage(
            QStringLiteral("Сетка: %1 px, привязка: %2 px")
                .arg(step->value())
                .arg(threshold->value(), 0, 'f', 1), 4000);
    });

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
    fileMenu->addAction(action(QStringLiteral("home")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("import")));
    fileMenu->addAction(action(QStringLiteral("export")));
    fileMenu->addAction(action(QStringLiteral("pdf")));
    fileMenu->addAction(action(QStringLiteral("export-pages")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("templates")));
    fileMenu->addAction(action(QStringLiteral("versions")));
    fileMenu->addAction(action(QStringLiteral("package")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("print")));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("quit")));

    auto *editMenu = menuBar()->addMenu(QStringLiteral("Правка"));
    editMenu->addAction(action(QStringLiteral("undo")));
    editMenu->addAction(action(QStringLiteral("redo")));
    editMenu->addAction(action(QStringLiteral("history")));
    editMenu->addSeparator();
    editMenu->addAction(action(QStringLiteral("cut")));
    editMenu->addAction(action(QStringLiteral("copy")));
    editMenu->addAction(action(QStringLiteral("paste")));
    editMenu->addAction(action(QStringLiteral("duplicate")));
    editMenu->addAction(action(QStringLiteral("step-repeat")));
    editMenu->addAction(action(QStringLiteral("clone")));
    editMenu->addAction(action(QStringLiteral("delete")));
    editMenu->addSeparator();
    editMenu->addAction(action(QStringLiteral("select-all")));
    editMenu->addAction(action(QStringLiteral("search-objects")));
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
    viewMenu->addAction(action(QStringLiteral("clear-guides")));
    viewMenu->addAction(action(QStringLiteral("grid")));
    viewMenu->addAction(action(QStringLiteral("print-area")));
    viewMenu->addAction(action(QStringLiteral("snap")));
    viewMenu->addAction(action(QStringLiteral("snap-settings")));
    viewMenu->addSeparator();
    viewMenu->addAction(action(QStringLiteral("fullscreen")));

    auto *objectMenu = menuBar()->addMenu(QStringLiteral("Объект"));
    objectMenu->addAction(action(QStringLiteral("group")));
    objectMenu->addAction(action(QStringLiteral("ungroup")));
    objectMenu->addAction(action(QStringLiteral("ungroup-all")));
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
    objectMenu->addAction(action(QStringLiteral("skew")));
    objectMenu->addSeparator();
    auto *booleanMenu = objectMenu->addMenu(QStringLiteral("Логические операции"));
    booleanMenu->addAction(action(QStringLiteral("bool-union")));
    booleanMenu->addAction(action(QStringLiteral("bool-difference")));
    booleanMenu->addAction(action(QStringLiteral("bool-intersection")));
    booleanMenu->addAction(action(QStringLiteral("bool-exclude")));
    booleanMenu->addAction(action(QStringLiteral("bool-divide")));
    booleanMenu->addAction(action(QStringLiteral("bool-trim")));
    booleanMenu->addSeparator();
    booleanMenu->addAction(action(QStringLiteral("contour")));
    booleanMenu->addAction(action(QStringLiteral("shape-builder-tool")));
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
    effectsMenu->addSeparator();
    auto *vectorEffects = effectsMenu->addMenu(QStringLiteral("Векторные эффекты"));
    vectorEffects->addAction(action(QStringLiteral("fx-glow")));
    vectorEffects->addAction(action(QStringLiteral("fx-blur")));
    vectorEffects->addAction(action(QStringLiteral("fx-distort")));
    vectorEffects->addAction(action(QStringLiteral("fx-warp")));
    vectorEffects->addAction(action(QStringLiteral("fx-perspective")));
    vectorEffects->addAction(action(QStringLiteral("fx-clip")));
    vectorEffects->addAction(action(QStringLiteral("fx-opacity")));
    vectorEffects->addSeparator();
    vectorEffects->addAction(action(QStringLiteral("fx-clear")));
    vectorEffects->addAction(action(QStringLiteral("fx-panel")));

    auto *symbolMenu = menuBar()->addMenu(QStringLiteral("Символы"));
    symbolMenu->addAction(action(QStringLiteral("symbol-create")));
    auto *insertSymbolMenu = symbolMenu->addMenu(QStringLiteral("Вставить символ"));
    connect(insertSymbolMenu, &QMenu::aboutToShow, this, [this, insertSymbolMenu]() {
        insertSymbolMenu->clear();
        if (!m_view || m_view->symbols().isEmpty()) {
            QAction *empty = insertSymbolMenu->addAction(QStringLiteral("Нет символов"));
            empty->setEnabled(false);
            return;
        }
        for (const SymbolDefinition &symbol : m_view->symbols()) {
            QAction *entry = insertSymbolMenu->addAction(symbol.name);
            const int id = symbol.id;
            connect(entry, &QAction::triggered, this, [this, id]() {
                m_view->insertSymbol(id);
                if (m_assetPanel)
                    m_assetPanel->refreshSymbols();
            });
        }
    });
    symbolMenu->addAction(action(QStringLiteral("symbol-update")));
    symbolMenu->addAction(action(QStringLiteral("symbol-detach")));
    symbolMenu->addSeparator();
    symbolMenu->addAction(action(QStringLiteral("symbol-rename")));
    symbolMenu->addAction(action(QStringLiteral("symbol-delete")));
    symbolMenu->addSeparator();
    symbolMenu->addAction(action(QStringLiteral("symbol-panel")));

    auto *draftingMenu = menuBar()->addMenu(QStringLiteral("Черчение"));
    auto *dimensionsMenu = draftingMenu->addMenu(QStringLiteral("Размеры"));
    dimensionsMenu->addAction(action(QStringLiteral("dimension-horizontal")));
    dimensionsMenu->addAction(action(QStringLiteral("dimension-vertical")));
    dimensionsMenu->addAction(action(QStringLiteral("dimension-aligned")));
    dimensionsMenu->addSeparator();
    dimensionsMenu->addAction(action(QStringLiteral("dimension-angular")));
    dimensionsMenu->addAction(action(QStringLiteral("dimension-radius")));
    dimensionsMenu->addAction(action(QStringLiteral("dimension-diameter")));
    draftingMenu->addSeparator();
    draftingMenu->addAction(action(QStringLiteral("construct-line")));
    draftingMenu->addAction(action(QStringLiteral("construct-angle")));
    draftingMenu->addAction(action(QStringLiteral("construct-parallel")));
    draftingMenu->addAction(action(QStringLiteral("construct-perpendicular")));
    draftingMenu->addAction(action(QStringLiteral("construct-tangent")));
    draftingMenu->addAction(action(QStringLiteral("construct-circle")));
    draftingMenu->addAction(action(QStringLiteral("construct-arc")));
    draftingMenu->addAction(action(QStringLiteral("construct-polygon")));
    draftingMenu->addAction(action(QStringLiteral("construct-copy")));
    draftingMenu->addSeparator();
    draftingMenu->addAction(action(QStringLiteral("scale-selection")));
    draftingMenu->addAction(action(QStringLiteral("drawing-scale")));
    auto *gridMenu = draftingMenu->addMenu(QStringLiteral("Сетка"));
    gridMenu->addAction(action(QStringLiteral("grid-cartesian")));
    gridMenu->addAction(action(QStringLiteral("grid-polar")));
    gridMenu->addAction(action(QStringLiteral("grid-isometric")));
    gridMenu->addSeparator();
    gridMenu->addAction(action(QStringLiteral("snap-settings")));

    auto *patternMenu = menuBar()->addMenu(QStringLiteral("Узор"));
    patternMenu->addAction(action(QStringLiteral("pattern-create")));
    patternMenu->addAction(action(QStringLiteral("pattern-edit")));
    patternMenu->addAction(action(QStringLiteral("pattern-update")));
    auto *applyPatternMenu = patternMenu->addMenu(QStringLiteral("Применить узор"));
    connect(applyPatternMenu, &QMenu::aboutToShow, this,
            [this, applyPatternMenu]() {
                applyPatternMenu->clear();
                if (!m_view || m_view->patterns().isEmpty()) {
                    QAction *empty = applyPatternMenu->addAction(
                        QStringLiteral("В документе нет узоров"));
                    empty->setEnabled(false);
                    return;
                }
                for (const PatternDefinition &pattern : m_view->patterns()) {
                    QAction *entry = applyPatternMenu->addAction(
                        QStringLiteral("%1 — %2").arg(pattern.name,
                                                      patternTypeName(pattern.settings.type)));
                    const int id = pattern.id;
                    connect(entry, &QAction::triggered, this, [this, id]() {
                        m_view->applyPatternToSelection(id);
                    });
                }
            });
    patternMenu->addAction(action(QStringLiteral("pattern-expand")));
    patternMenu->addSeparator();
    patternMenu->addAction(action(QStringLiteral("pattern-save-library")));
    patternMenu->addAction(action(QStringLiteral("pattern-library")));

    auto *codeMenu = menuBar()->addMenu(QStringLiteral("Код"));
    codeMenu->addAction(action(QStringLiteral("code-qr")));
    codeMenu->addSeparator();
    codeMenu->addAction(action(QStringLiteral("code-ean13")));
    codeMenu->addAction(action(QStringLiteral("code-ean8")));
    codeMenu->addAction(action(QStringLiteral("code-upca")));
    codeMenu->addSeparator();
    codeMenu->addAction(action(QStringLiteral("code-code128")));
    codeMenu->addAction(action(QStringLiteral("code-code39")));
    codeMenu->addAction(action(QStringLiteral("code-datamatrix")));
    codeMenu->addSeparator();
    codeMenu->addAction(action(QStringLiteral("code-edit")));

    auto *textMenu = menuBar()->addMenu(QStringLiteral("Текст"));
    textMenu->addAction(action(QStringLiteral("text-character")));
    textMenu->addAction(action(QStringLiteral("text-paragraph")));
    textMenu->addSeparator();
    textMenu->addAction(action(QStringLiteral("text-on-path")));
    textMenu->addAction(action(QStringLiteral("text-in-shape")));
    textMenu->addAction(action(QStringLiteral("text-detach")));
    textMenu->addSeparator();
    textMenu->addAction(action(QStringLiteral("text-styles")));
    textMenu->addAction(action(QStringLiteral("text-symbol")));
    textMenu->addAction(action(QStringLiteral("text-fonts")));
    textMenu->addAction(action(QStringLiteral("text-opentype")));
    textMenu->addSeparator();
    textMenu->addAction(action(QStringLiteral("convert-curves")));

    auto *paletteMenu = menuBar()->addMenu(QStringLiteral("Палитры"));
    auto *addPaletteColor = paletteMenu->addAction(QStringLiteral("Добавить цвет заливки"));
    auto *addSpotColor = paletteMenu->addAction(QStringLiteral("Добавить Spot-цвет"));
    auto *addGlobalColor = paletteMenu->addAction(QStringLiteral("Сделать цвет глобальным"));
    paletteMenu->addSeparator();
    auto *savePalette = paletteMenu->addAction(QStringLiteral("Сохранить палитру…"));
    auto *loadPalette = paletteMenu->addAction(QStringLiteral("Загрузить палитру…"));
    auto *exportGpl = paletteMenu->addAction(QStringLiteral("Экспорт в GPL…"));
    auto *resetPalette = paletteMenu->addAction(QStringLiteral("Сбросить палитру"));
    paletteMenu->addSeparator();
    auto *manageGlobals = paletteMenu->addAction(QStringLiteral("Глобальные цвета…"));

    const auto paletteFromSettings = [this]() {
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        QVector<PaletteEntry> entries;
        const QJsonArray array = QJsonDocument::fromJson(
                                     settings.value(QStringLiteral("palette/custom"))
                                         .toByteArray())
                                     .array();
        for (const QJsonValue &value : array) {
            const QJsonObject entry = value.toObject();
            PaletteEntry item;
            item.name = entry.value(QStringLiteral("name")).toString();
            item.color = QColor(entry.value(QStringLiteral("color")).toString());
            item.spot = entry.value(QStringLiteral("spot")).toBool(false);
            item.global = entry.value(QStringLiteral("global")).toBool(false);
            if (item.color.isValid())
                entries.append(item);
        }
        return entries;
    };
    const auto storePaletteInSettings = [](const QVector<PaletteEntry> &custom) {
        QJsonArray array;
        for (const PaletteEntry &entry : custom) {
            QJsonObject object;
            object.insert(QStringLiteral("name"), entry.name);
            object.insert(QStringLiteral("color"), entry.color.name(QColor::HexArgb));
            object.insert(QStringLiteral("spot"), entry.spot);
            object.insert(QStringLiteral("global"), entry.global);
            array.append(object);
        }
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        settings.setValue(QStringLiteral("palette/custom"),
                          QJsonDocument(array).toJson(QJsonDocument::Compact));
    };
    const auto mergeCustom = [this]() {
        if (!m_palette)
            return;
        QVector<PaletteEntry> entries = m_palette->entries();
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        const QJsonArray array = QJsonDocument::fromJson(
                                     settings.value(QStringLiteral("palette/custom"))
                                         .toByteArray())
                                     .array();
        for (const QJsonValue &value : array) {
            const QJsonObject entry = value.toObject();
            PaletteEntry item;
            item.name = entry.value(QStringLiteral("name")).toString();
            item.color = QColor(entry.value(QStringLiteral("color")).toString());
            item.spot = entry.value(QStringLiteral("spot")).toBool(false);
            item.global = entry.value(QStringLiteral("global")).toBool(false);
            if (!item.color.isValid())
                continue;
            bool exists = false;
            for (const PaletteEntry &existing : entries) {
                if (existing.color == item.color && existing.name == item.name) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                entries.append(item);
        }
        m_palette->setEntries(entries);
    };
    const auto addColorToPalette = [this, mergeCustom](bool spot, bool global) {
        if (!m_palette)
            return;
        const QColor color = m_view->fillColor();
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Новый цвет палитры"),
            QStringLiteral("Название:"), QLineEdit::Normal,
            color.name(QColor::HexRgb).toUpper(), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        PaletteEntry entry;
        entry.name = name.trimmed();
        entry.color = color;
        entry.spot = spot;
        entry.global = global;
        m_palette->addEntry(entry);
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        QJsonArray array = QJsonDocument::fromJson(
                               settings.value(QStringLiteral("palette/custom"))
                                   .toByteArray())
                               .array();
        QJsonObject object;
        object.insert(QStringLiteral("name"), entry.name);
        object.insert(QStringLiteral("color"), entry.color.name(QColor::HexArgb));
        object.insert(QStringLiteral("spot"), entry.spot);
        object.insert(QStringLiteral("global"), entry.global);
        array.append(object);
        settings.setValue(QStringLiteral("palette/custom"),
                          QJsonDocument(array).toJson(QJsonDocument::Compact));
        statusBar()->showMessage(
            QStringLiteral("Цвет «%1» добавлен в палитру.").arg(entry.name), 3000);
    };
    connect(addPaletteColor, &QAction::triggered, this,
            [addColorToPalette]() { addColorToPalette(false, false); });
    connect(addSpotColor, &QAction::triggered, this,
            [addColorToPalette]() { addColorToPalette(true, false); });
    connect(addGlobalColor, &QAction::triggered, this,
            [addColorToPalette]() { addColorToPalette(false, true); });

    connect(savePalette, &QAction::triggered, this, [this]() {
        if (!m_palette)
            return;
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Сохранить палитру"),
            QDir::homePath() + QStringLiteral("/palette.vpal"),
            QStringLiteral("Палитра Vectora (*.vpal);;JSON (*.json)"));
        if (path.isEmpty())
            return;
        QJsonArray array;
        for (const PaletteEntry &entry : m_palette->entries()) {
            QJsonObject object;
            object.insert(QStringLiteral("name"), entry.name);
            object.insert(QStringLiteral("color"), entry.color.name(QColor::HexArgb));
            object.insert(QStringLiteral("spot"), entry.spot);
            object.insert(QStringLiteral("global"), entry.global);
            array.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("vectora-palette"));
        root.insert(QStringLiteral("colors"), array);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, QStringLiteral("Палитра"),
                                  QStringLiteral("Не удалось записать файл."));
            return;
        }
        file.write(QJsonDocument(root).toJson());
        file.commit();
        statusBar()->showMessage(QStringLiteral("Палитра сохранена."), 3000);
    });
    connect(loadPalette, &QAction::triggered, this, [this]() {
        if (!m_palette)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Загрузить палитру"), QDir::homePath(),
            QStringLiteral("Палитры (*.vpal *.json *.gpl);;Все файлы (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, QStringLiteral("Палитра"),
                                  QStringLiteral("Не удалось открыть файл."));
            return;
        }
        QVector<PaletteEntry> entries;
        if (path.endsWith(QLatin1String(".gpl"), Qt::CaseInsensitive)) {
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                     Qt::SkipEmptyParts);
                if (parts.size() >= 4 && parts.at(0).toInt() >= 0) {
                    PaletteEntry entry;
                    entry.color = QColor(parts.at(1).toInt(), parts.at(2).toInt(),
                                         parts.at(3).toInt());
                    entry.name = parts.mid(4).join(QLatin1Char(' '));
                    if (entry.color.isValid())
                        entries.append(entry);
                }
            }
        } else {
            const QJsonArray array = QJsonDocument::fromJson(file.readAll())
                                         .object()
                                         .value(QStringLiteral("colors"))
                                         .toArray();
            for (const QJsonValue &value : array) {
                const QJsonObject object = value.toObject();
                PaletteEntry entry;
                entry.name = object.value(QStringLiteral("name")).toString();
                entry.color = QColor(object.value(QStringLiteral("color")).toString());
                entry.spot = object.value(QStringLiteral("spot")).toBool(false);
                entry.global = object.value(QStringLiteral("global")).toBool(false);
                if (entry.color.isValid())
                    entries.append(entry);
            }
        }
        if (entries.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Палитра"),
                                 QStringLiteral("В файле нет цветов."));
            return;
        }
        m_palette->setEntries(entries);
        statusBar()->showMessage(
            QStringLiteral("Загружено цветов: %1").arg(entries.size()), 3000);
    });
    connect(exportGpl, &QAction::triggered, this, [this]() {
        if (!m_palette)
            return;
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Экспорт GPL"),
            QDir::homePath() + QStringLiteral("/palette.gpl"),
            QStringLiteral("GIMP Palette (*.gpl)"));
        if (path.isEmpty())
            return;
        QString text = QStringLiteral("GIMP Palette\nName: Vectora\nColumns: 8\n#\n");
        for (const PaletteEntry &entry : m_palette->entries())
            text += QStringLiteral("%1 %2 %3\t%4\n")
                        .arg(entry.color.red(), 3)
                        .arg(entry.color.green(), 3)
                        .arg(entry.color.blue(), 3)
                        .arg(entry.name);
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(text.toUtf8());
            file.commit();
            statusBar()->showMessage(QStringLiteral("GPL сохранён."), 3000);
        }
    });
    connect(resetPalette, &QAction::triggered, this, [this, mergeCustom]() {
        if (!m_palette)
            return;
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        settings.remove(QStringLiteral("palette/custom"));
        m_view->setFillColor(QColor("#4f7cc0"));
        m_palette->setEntries(ColorPalette::defaultEntries());
        mergeCustom();
    });
    connect(manageGlobals, &QAction::triggered, this,
            [addColorToPalette]() { addColorToPalette(false, true); });
    Q_UNUSED(paletteFromSettings)
    Q_UNUSED(storePaletteInSettings)

    auto *libraryMenu = menuBar()->addMenu(QStringLiteral("Библиотека"));
    libraryMenu->addAction(action(QStringLiteral("library-add")));
    libraryMenu->addAction(action(QStringLiteral("library-panel")));
    libraryMenu->addSeparator();
    libraryMenu->addAction(action(QStringLiteral("library-new")));
    libraryMenu->addAction(action(QStringLiteral("library-import")));
    libraryMenu->addAction(action(QStringLiteral("library-export")));
    libraryMenu->addSeparator();
    libraryMenu->addAction(action(QStringLiteral("library-undo")));
    libraryMenu->addAction(action(QStringLiteral("library-redo")));

    auto *windowMenu = menuBar()->addMenu(QStringLiteral("Окно"));
    windowMenu->addAction(m_toolsDock->toggleViewAction());
    windowMenu->addAction(m_paletteDock->toggleViewAction());
    if (m_assetDock)
        windowMenu->addAction(m_assetDock->toggleViewAction());
    auto *dockersMenu = windowMenu->addMenu(QStringLiteral("Докеры"));
    if (m_dockPanels) {
        for (QDockWidget *dock : m_dockPanels->docks())
            dockersMenu->addAction(dock->toggleViewAction());
    }
    windowMenu->addSeparator();
    windowMenu->addAction(action(QStringLiteral("preferences")));

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("Справка"));
    helpMenu->addAction(action(QStringLiteral("check-updates")));
    helpMenu->addSeparator();
    helpMenu->addAction(action(QStringLiteral("shortcuts")));
    helpMenu->addAction(action(QStringLiteral("about")));
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void MainWindow::createCanvas()
{
    m_centralStack = new QStackedWidget(this);
    m_editorPage = new QWidget(m_centralStack);
    auto *grid = new QGridLayout(m_editorPage);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    auto *corner = new QLabel(QStringLiteral("мм"), m_editorPage);
    m_rulerCorner = corner;
    corner->setAlignment(Qt::AlignCenter);
    corner->setFixedSize(22, 22);
    corner->setStyleSheet(QStringLiteral(
        "background:#202226; color:#8b929d; font-size:9px;"
        "border-right:1px solid #2c2f35; border-bottom:1px solid #2c2f35;"));

    m_hRuler = new Ruler(Qt::Horizontal, m_editorPage);
    m_vRuler = new Ruler(Qt::Vertical, m_editorPage);
    m_view = new CanvasView(m_editorPage);
    m_scene = m_view->scene();

    m_hRuler->setView(m_view);
    m_vRuler->setView(m_view);

    grid->addWidget(corner, 0, 0);
    grid->addWidget(m_hRuler, 0, 1);
    grid->addWidget(m_vRuler, 1, 0);
    grid->addWidget(m_view, 1, 1);

    m_welcome = new WelcomeWidget(m_centralStack);
    m_centralStack->addWidget(m_welcome);
    m_centralStack->addWidget(m_editorPage);
    setCentralWidget(m_centralStack);
    m_centralStack->setCurrentWidget(m_welcome);

    connect(m_welcome, &WelcomeWidget::createRequested, this, &MainWindow::newDocument);
    connect(m_welcome, &WelcomeWidget::openRequested, this, &MainWindow::openDocument);
    connect(m_welcome, &WelcomeWidget::sampleRequested, this,
            &MainWindow::loadSampleDocument);
    connect(m_welcome, &WelcomeWidget::recentRequested, this,
            [this](const QString &path) {
                if (!maybeSave())
                    return;
                if (openPath(path))
                    showEditor();
            });

    connect(m_view, &CanvasView::zoomChanged, this, &MainWindow::updateZoomUi);
    connect(m_view, &CanvasView::scenePosChanged, this, &MainWindow::updateCoordinates);
    connect(m_view, &CanvasView::statusHint, this, [this](const QString &text) {
        statusBar()->showMessage(text, 5000);
    });
    connect(m_view, &CanvasView::beginEdit, this, &MainWindow::beginEdit);
    connect(m_view, &CanvasView::endEdit, this, &MainWindow::endEdit);
    connect(m_view, &CanvasView::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(m_view, &CanvasView::pageChanged, this,
            [this](int) { updatePageBadge(); });
    connect(m_view, &CanvasView::contextMenuRequested, this,
            &MainWindow::showCanvasContextMenu);
    connect(m_view, &CanvasView::colorPicked, this, [this](const QColor &color) {
        m_view->setFillColor(color);
        m_toolPalette->setFillColor(color);
        statusBar()->showMessage(
            QStringLiteral("Пипетка: %1").arg(color.name().toUpper()), 5000);
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
    // The tool list is taller than a laptop viewport now, so keep it scrollable
    // instead of clipping the lower groups.
    auto *toolsScroll = new QScrollArea(m_toolsDock);
    toolsScroll->setObjectName(QStringLiteral("ToolsScroll"));
    toolsScroll->setWidgetResizable(true);
    toolsScroll->setFrameShape(QFrame::NoFrame);
    toolsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolsScroll->setWidget(m_toolPalette);
    m_toolsDock->setWidget(toolsScroll);
    m_toolsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_toolsDock->setTitleBarWidget(new QWidget(m_toolsDock));
    m_toolsDock->setFixedWidth(64);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);
    connect(m_toolPalette, &ToolPalette::toolSelected, this, &MainWindow::applyTool);
    connect(m_toolPalette, &ToolPalette::toolDoubleClicked, this, [this](Tool tool) {
        if (tool == Tool::Select) {
            m_view->selectAllObjects();
            statusBar()->showMessage(
                QStringLiteral("Двойной клик по «Выделению»: выбраны все объекты."), 3000);
        }
    });

    // Right dockers grouped into tabs.
    m_dockPanels = new DockPanels(this, m_view, this);
    const QList<QDockWidget *> dockers = m_dockPanels->docks();
    if (dockers.size() >= 7) {
        QDockWidget *objects = dockers.at(0);
        QDockWidget *properties = dockers.at(1);
        addDockWidget(Qt::RightDockWidgetArea, objects);
        addDockWidget(Qt::RightDockWidgetArea, properties);
        splitDockWidget(objects, properties, Qt::Vertical);

        for (QDockWidget *dock : {dockers.at(3), dockers.at(4)}) {
            addDockWidget(Qt::RightDockWidgetArea, dock);
            tabifyDockWidget(objects, dock);
        }
        for (QDockWidget *dock : {dockers.at(2), dockers.at(5), dockers.at(6)}) {
            addDockWidget(Qt::RightDockWidgetArea, dock);
            tabifyDockWidget(properties, dock);
        }

        objects->raise();
        properties->raise();
        resizeDocks({objects, properties}, {280, 460}, Qt::Vertical);
        resizeDocks({objects}, {380}, Qt::Horizontal);
    }

    connect(m_dockPanels, &DockPanels::beginEdit, this, &MainWindow::beginEdit);
    connect(m_dockPanels, &DockPanels::endEdit, this, &MainWindow::endEdit);
    connect(m_dockPanels, &DockPanels::status, this, [this](const QString &text) {
        statusBar()->showMessage(text, 5000);
    });

    // Bottom: colour palette
    AssetStore::instance().load();
    PatternStore::instance().load();
    m_assetDock = new QDockWidget(QStringLiteral("Библиотека"), this);
    m_assetDock->setObjectName(QStringLiteral("AssetDock"));
    m_assetPanel = new AssetPanel(m_view, m_assetDock);
    m_assetDock->setWidget(m_assetPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_assetDock);
    if (!dockers.isEmpty()) {
        tabifyDockWidget(dockers.at(0), m_assetDock);
        dockers.at(0)->raise();
    }

    m_paletteDock = new QDockWidget(QStringLiteral("Палитра"), this);
    m_paletteDock->setObjectName(QStringLiteral("PaletteDock"));
    auto *bottomPanel = new QWidget(m_paletteDock);
    auto *bottomLayout = new QVBoxLayout(bottomPanel);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);
    m_pageBar = new PageBar(bottomPanel);
    bottomLayout->addWidget(m_pageBar);

    m_palette = new ColorPalette(bottomPanel);
    {
        // Restore user, global and spot colours saved between sessions.
        QSettings settings(QStringLiteral("Vectora"), QStringLiteral("Vectora"));
        const QJsonArray custom = QJsonDocument::fromJson(
                                      settings.value(QStringLiteral("palette/custom"))
                                          .toByteArray())
                                      .array();
        QVector<PaletteEntry> entries = m_palette->entries();
        for (const QJsonValue &value : custom) {
            const QJsonObject object = value.toObject();
            PaletteEntry entry;
            entry.name = object.value(QStringLiteral("name")).toString();
            entry.color = QColor(object.value(QStringLiteral("color")).toString());
            entry.spot = object.value(QStringLiteral("spot")).toBool(false);
            entry.global = object.value(QStringLiteral("global")).toBool(false);
            if (entry.color.isValid())
                entries.append(entry);
        }
        m_palette->setEntries(entries);
    }
    bottomLayout->addWidget(m_palette);
    m_paletteDock->setWidget(bottomPanel);
    m_paletteDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_paletteDock->setTitleBarWidget(new QWidget(m_paletteDock));
    m_paletteDock->setFixedHeight(66);
    addDockWidget(Qt::BottomDockWidgetArea, m_paletteDock);

    m_pageBar->setView(m_view);
    connect(m_pageBar, &PageBar::status, this, [this](const QString &text) {
        statusBar()->showMessage(text, 3000);
    });
    connect(m_pageBar, &PageBar::pageSetupRequested, this,
            &MainWindow::changePageSize);

    connect(m_palette, &ColorPalette::statusHint, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });
    connect(m_palette, &ColorPalette::colorPicked, this,
            [this](const QColor &color, const QString &name) {
                Q_UNUSED(name)
                onPaletteColor(color);
            });
    connect(m_palette, &ColorPalette::outlineColorPicked, this,
            [this](const QColor &color, const QString &name) {
                Q_UNUSED(name)
                m_view->setStrokeColor(color);
                m_toolPalette->setStrokeColor(color);
                const QList<VectorItem *> selected = m_view->selectedVectorItems();
                if (selected.isEmpty())
                    return;
                beginEdit();
                for (VectorItem *item : selected)
                    item->setStrokeColor(color);
                endEdit();
                statusBar()->showMessage(
                    QStringLiteral("Цвет обводки: %1").arg(color.name().toUpper()), 4000);
            });

    connect(m_toolPalette, &ToolPalette::fillColorClicked, this, [this]() {
        const QColor color = QColorDialog::getColor(m_view->fillColor(), this,
                                                    QStringLiteral("Цвет заливки"));
        if (color.isValid())
            onPaletteColor(color);
    });
    connect(m_toolPalette, &ToolPalette::strokeColorClicked, this, [this]() {
        const QColor color = QColorDialog::getColor(m_view->strokeColor(), this,
                                                    QStringLiteral("Цвет обводки"));
        if (!color.isValid())
            return;
        m_view->setStrokeColor(color);
        m_toolPalette->setStrokeColor(color);
        const QList<VectorItem *> selected = m_view->selectedVectorItems();
        if (!selected.isEmpty()) {
            beginEdit();
            for (VectorItem *item : selected)
                item->setStrokeColor(color);
            endEdit();
        }
        statusBar()->showMessage(
            QStringLiteral("Цвет обводки: %1").arg(color.name().toUpper()), 4000);
    });

    m_toolPalette->setFillColor(m_view->fillColor());
    m_toolPalette->setStrokeColor(m_view->strokeColor());
}

// ---------------------------------------------------------------------------
// Tool bars
// ---------------------------------------------------------------------------

void MainWindow::createMainToolBar()
{
    auto *bar = addToolBar(QStringLiteral("Основная"));
    m_mainToolBar = bar;
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

    m_pageBadge = new QLabel(bar);
    m_pageBadge->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px; padding-right:8px;"));
    bar->addWidget(m_pageBadge);
    updatePageBadge();
}

void MainWindow::createPropertyBar()
{
    auto *bar = addToolBar(QStringLiteral("Свойства инструмента"));
    m_propertyBar = bar;
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
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &MainWindow::applyQuickTransform);
    };
    m_quickX = numberField(transformPage, -10000, 10000, 0.0, QStringLiteral(" px"), 84, 0);
    m_quickY = numberField(transformPage, -10000, 10000, 0.0, QStringLiteral(" px"), 84, 0);
    m_quickW = numberField(transformPage, 1, 10000, 100.0, QStringLiteral(" px"), 84, 0);
    m_quickH = numberField(transformPage, 1, 10000, 100.0, QStringLiteral(" px"), 84, 0);
    m_quickAngle = numberField(transformPage, -360, 360, 0.0, QStringLiteral(" °"), 70);
    addPair(QStringLiteral("X"), m_quickX);
    addPair(QStringLiteral("Y"), m_quickY);
    addPair(QStringLiteral("Ш"), m_quickW);
    addPair(QStringLiteral("В"), m_quickH);
    addPair(QStringLiteral("∠"), m_quickAngle);
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

    // 4 — Bézier
    auto *bezierPage = new QWidget(m_propertyStack);
    auto *bezierLayout = new QHBoxLayout(bezierPage);
    bezierLayout->setContentsMargins(2, 0, 2, 0);
    bezierLayout->setSpacing(6);

    bezierLayout->addWidget(fieldLabel(QStringLiteral("Узел")));
    auto *modeGroup = new QButtonGroup(bezierPage);
    modeGroup->setExclusive(true);
    const QString modeNames[] = {QStringLiteral("Тупой"),
                                 QStringLiteral("Сглаженный"),
                                 QStringLiteral("Симметричный")};
    const QString modeTips[] = {
        QStringLiteral("Излом: направляющие независимы"),
        QStringLiteral("Сглаженный: направляющие на одной линии"),
        QStringLiteral("Симметричный: направляющие равной длины"),
    };
    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(bezierPage);
        button->setText(modeNames[i]);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(modeTips[i]);
        modeGroup->addButton(button, i);
        bezierLayout->addWidget(button);
    }
    modeGroup->button(2)->setChecked(true);
    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_view->setBezierNodeMode(static_cast<CanvasView::BezierNodeMode>(id));
        statusBar()->showMessage(
            QStringLiteral("Тип нового узла: %1")
                .arg(id == 0 ? QStringLiteral("тупой")
                             : (id == 1 ? QStringLiteral("сглаженный")
                                        : QStringLiteral("симметричный"))),
            3000);
    });

    auto *autoClose = new QCheckBox(QStringLiteral("Автозамыкание"), bezierPage);
    connect(autoClose, &QCheckBox::toggled, this, [this](bool on) {
        m_view->setBezierAutoClose(on);
        statusBar()->showMessage(on ? QStringLiteral("Кривая будет замыкаться "
                                                     "автоматически")
                                    : QStringLiteral("Автозамыкание выключено"),
                                 3000);
    });
    bezierLayout->addWidget(autoClose);

    bezierLayout->addWidget(verticalRule(bezierPage));

    auto *closeButton = new QToolButton(bezierPage);
    closeButton->setText(QStringLiteral("Замкнуть"));
    closeButton->setToolTip(QStringLiteral("Замкнуть кривую по первому узлу"));
    connect(closeButton, &QToolButton::clicked, this,
            [this]() { m_view->closeBezier(); });
    bezierLayout->addWidget(closeButton);

    auto *deleteNodeButton = new QToolButton(bezierPage);
    deleteNodeButton->setText(QStringLiteral("Удалить узел"));
    deleteNodeButton->setToolTip(QStringLiteral("Убрать последний узел (Backspace)"));
    connect(deleteNodeButton, &QToolButton::clicked, this,
            [this]() { m_view->deleteLastBezierNode(); });
    bezierLayout->addWidget(deleteNodeButton);

    auto *finishButton = new QToolButton(bezierPage);
    finishButton->setText(QStringLiteral("Завершить"));
    finishButton->setToolTip(QStringLiteral("Завершить кривую (Enter)"));
    connect(finishButton, &QToolButton::clicked, this,
            [this]() { m_view->finishBezier(); });
    bezierLayout->addWidget(finishButton);

    bezierLayout->addWidget(verticalRule(bezierPage));
    auto *bezierHint = new QLabel(
        QStringLiteral("Тяните от узла для кривизны · Alt — излом · "
                       "Ctrl — шаг 15° · Backspace — удалить узел"),
        bezierPage);
    bezierHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    bezierLayout->addWidget(bezierHint);
    bezierLayout->addStretch(1);

    // 5 — rectangle options
    auto *rectPage = new QWidget(m_propertyStack);
    auto *rectLayout = new QHBoxLayout(rectPage);
    rectLayout->setContentsMargins(2, 0, 2, 0);
    rectLayout->setSpacing(8);
    rectLayout->addWidget(fieldLabel(QStringLiteral("Скругление углов")));
    m_rectRadius = numberField(rectPage, 0, 500, 0, QStringLiteral(" px"), 84, 0);
    rectLayout->addWidget(m_rectRadius);
    rectLayout->addWidget(verticalRule(rectPage));
    auto *rectHint = new QLabel(
        QStringLiteral("Применяется к выделенным прямоугольникам и к новым фигурам."),
        rectPage);
    rectHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    rectLayout->addWidget(rectHint);
    rectLayout->addStretch(1);
    connect(m_rectRadius, &QDoubleSpinBox::editingFinished, this,
            &MainWindow::applyShapeOptions);

    // 6 — polygon options
    auto *polygonPage = new QWidget(m_propertyStack);
    auto *polygonLayout = new QHBoxLayout(polygonPage);
    polygonLayout->setContentsMargins(2, 0, 2, 0);
    polygonLayout->setSpacing(8);
    polygonLayout->addWidget(fieldLabel(QStringLiteral("Стороны")));
    m_polygonSides = numberField(polygonPage, 3, 64, 6, QString(), 70, 0);
    polygonLayout->addWidget(m_polygonSides);
    polygonLayout->addWidget(verticalRule(polygonPage));
    auto *polygonHint = new QLabel(
        QStringLiteral("От 3 до 64 сторон. Новые многоугольники используют это значение."),
        polygonPage);
    polygonHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    polygonLayout->addWidget(polygonHint);
    polygonLayout->addStretch(1);
    connect(m_polygonSides, &QDoubleSpinBox::editingFinished, this,
            &MainWindow::applyShapeOptions);

    // 7 — star options
    auto *starPage = new QWidget(m_propertyStack);
    auto *starLayout = new QHBoxLayout(starPage);
    starLayout->setContentsMargins(2, 0, 2, 0);
    starLayout->setSpacing(8);
    starLayout->addWidget(fieldLabel(QStringLiteral("Лучи")));
    m_starPoints = numberField(starPage, 3, 50, 5, QString(), 64, 0);
    starLayout->addWidget(m_starPoints);
    starLayout->addWidget(fieldLabel(QStringLiteral("Острота")));
    m_starRatio = numberField(starPage, 5, 95, 45, QStringLiteral(" %"), 70, 0);
    starLayout->addWidget(m_starRatio);
    starLayout->addWidget(verticalRule(starPage));
    auto *starHint = new QLabel(
        QStringLiteral("Острота — доля внутреннего радиуса звезды."),
        starPage);
    starHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    starLayout->addWidget(starHint);
    starLayout->addStretch(1);
    connect(m_starPoints, &QDoubleSpinBox::editingFinished, this,
            &MainWindow::applyShapeOptions);
    connect(m_starRatio, &QDoubleSpinBox::editingFinished, this,
            &MainWindow::applyShapeOptions);

    // 8 — spiral options
    auto *spiralPage = new QWidget(m_propertyStack);
    auto *spiralLayout = new QHBoxLayout(spiralPage);
    spiralLayout->setContentsMargins(2, 0, 2, 0);
    spiralLayout->setSpacing(8);
    spiralLayout->addWidget(fieldLabel(QStringLiteral("Витки")));
    m_spiralTurns = numberField(spiralPage, 1, 20, 3, QString(), 70, 1);
    m_spiralTurns->setSingleStep(0.5);
    spiralLayout->addWidget(m_spiralTurns);
    spiralLayout->addWidget(verticalRule(spiralPage));
    auto *spiralHint = new QLabel(
        QStringLiteral("Количество оборотов новой спирали."),
        spiralPage);
    spiralHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    spiralLayout->addWidget(spiralHint);
    spiralLayout->addStretch(1);
    connect(m_spiralTurns, &QDoubleSpinBox::editingFinished, this,
            &MainWindow::applyShapeOptions);

    m_propertyStack->addWidget(zoomPage);      // 0
    m_propertyStack->addWidget(transformPage); // 1
    m_propertyStack->addWidget(textPage);      // 2
    m_propertyStack->addWidget(paintPage);     // 3
    m_propertyStack->addWidget(bezierPage);    // 4
    m_propertyStack->addWidget(rectPage);      // 5
    m_propertyStack->addWidget(polygonPage);   // 6
    m_propertyStack->addWidget(starPage);      // 7
    m_propertyStack->addWidget(spiralPage);    // 8

    // 9 — node editing
    auto *nodePage = new QWidget(m_propertyStack);
    auto *nodeLayout = new QHBoxLayout(nodePage);
    nodeLayout->setContentsMargins(2, 0, 2, 0);
    nodeLayout->setSpacing(5);

    nodeLayout->addWidget(fieldLabel(QStringLiteral("Узел")));
    auto *nodeTypeGroup = new QButtonGroup(nodePage);
    nodeTypeGroup->setExclusive(true);
    const QString nodeTypeNames[] = {QStringLiteral("Тупой"),
                                     QStringLiteral("Сглаженный"),
                                     QStringLiteral("Симметричный")};
    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(nodePage);
        button->setText(nodeTypeNames[i]);
        button->setCheckable(true);
        button->setAutoRaise(true);
        nodeTypeGroup->addButton(button, i);
        connect(button, &QToolButton::clicked, this, [this, i]() {
            m_view->setSelectedNodeType(i);
        });
        nodeLayout->addWidget(button);
    }

    nodeLayout->addWidget(verticalRule(nodePage));

    const auto nodeButton = [&](const QString &text, const QString &tip,
                                const std::function<void()> &action) {
        auto *button = new QToolButton(nodePage);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, action);
        nodeLayout->addWidget(button);
    };
    nodeButton(QStringLiteral("Добавить"), QStringLiteral("Добавить узел после выбранного"),
               [this]() { m_view->addNodeAtSelectedSegment(); });
    nodeButton(QStringLiteral("Удалить"), QStringLiteral("Удалить выбранные узлы"),
               [this]() { m_view->deleteSelectedNodes(); });
    nodeButton(QStringLiteral("Замкнуть"), QStringLiteral("Замкнуть контур"),
               [this]() { m_view->closeSelectedPath(); });
    nodeButton(QStringLiteral("Разомкнуть"), QStringLiteral("Разомкнуть контур"),
               [this]() { m_view->openSelectedPath(); });
    nodeButton(QStringLiteral("Соединить"), QStringLiteral("Соединить две кривые"),
               [this]() { m_view->joinSelectedEndpoints(); });
    nodeButton(QStringLiteral("Разорвать"), QStringLiteral("Разорвать кривую в узле"),
               [this]() { m_view->breakAtSelectedNode(); });
    nodeButton(QStringLiteral("Оптимизировать"), QStringLiteral("Удалить лишние узлы"),
               [this]() { m_view->simplifySelectedPath(); });

    nodeLayout->addWidget(verticalRule(nodePage));
    nodeLayout->addWidget(fieldLabel(QStringLiteral("Выровнять")));
    const char *nodeAlignIcons[] = {"align-left", "align-hcenter", "align-right",
                                   "align-top", "align-vcenter", "align-bottom"};
    const char *nodeAlignTips[] = {"По левому краю", "По центру", "По правому краю",
                                   "По верху", "По середине", "По низу"};
    for (int i = 0; i < 6; ++i) {
        auto *button = new QToolButton(nodePage);
        button->setIcon(Icons::icon(QString::fromLatin1(nodeAlignIcons[i])));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(QString::fromUtf8(nodeAlignTips[i]));
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, [this, i]() {
            m_view->alignSelectedNodes(i);
        });
        nodeLayout->addWidget(button);
    }

    nodeLayout->addWidget(verticalRule(nodePage));
    auto *nodeHint = new QLabel(
        QStringLiteral("Shift+клик — несколько узлов · двойной клик по сегменту — "
                       "добавить узел · Ctrl — независимые направляющие"),
        nodePage);
    nodeHint->setStyleSheet(QStringLiteral("color:#757b85; font-size:11px;"));
    nodeLayout->addWidget(nodeHint);
    nodeLayout->addStretch(1);

    m_propertyStack->addWidget(nodePage);      // 9

    // 10 — dimensions
    auto *dimensionPage = new QWidget(m_propertyStack);
    auto *dimensionLayout = new QHBoxLayout(dimensionPage);
    dimensionLayout->setContentsMargins(2, 0, 2, 0);
    dimensionLayout->setSpacing(6);

    dimensionLayout->addWidget(fieldLabel(QStringLiteral("Размер")));
    m_dimensionTypeCombo = new QComboBox(dimensionPage);
    m_dimensionTypeCombo->addItems({QStringLiteral("Горизонтальный"),
                                    QStringLiteral("Вертикальный"),
                                    QStringLiteral("Наклонный"),
                                    QStringLiteral("Угловой"),
                                    QStringLiteral("Радиус"),
                                    QStringLiteral("Диаметр")});
    m_dimensionTypeCombo->setFixedWidth(132);
    dimensionLayout->addWidget(m_dimensionTypeCombo);

    dimensionLayout->addWidget(fieldLabel(QStringLiteral("Единицы")));
    m_dimensionUnitsCombo = new QComboBox(dimensionPage);
    m_dimensionUnitsCombo->addItems({QStringLiteral("мм"), QStringLiteral("см"),
                                     QStringLiteral("дюймы"), QStringLiteral("px")});
    m_dimensionUnitsCombo->setFixedWidth(82);
    dimensionLayout->addWidget(m_dimensionUnitsCombo);

    dimensionLayout->addWidget(fieldLabel(QStringLiteral("Точность")));
    m_dimensionPrecisionSpin = new QSpinBox(dimensionPage);
    m_dimensionPrecisionSpin->setRange(0, 4);
    m_dimensionPrecisionSpin->setValue(m_view->dimensionPrecision());
    m_dimensionPrecisionSpin->setFixedWidth(58);
    dimensionLayout->addWidget(m_dimensionPrecisionSpin);

    dimensionLayout->addWidget(fieldLabel(QStringLiteral("Смещение")));
    m_dimensionOffsetSpin = new QDoubleSpinBox(dimensionPage);
    m_dimensionOffsetSpin->setRange(-300.0, 300.0);
    m_dimensionOffsetSpin->setDecimals(1);
    m_dimensionOffsetSpin->setValue(m_view->dimensionOffset());
    m_dimensionOffsetSpin->setSuffix(QStringLiteral(" px"));
    m_dimensionOffsetSpin->setFixedWidth(88);
    dimensionLayout->addWidget(m_dimensionOffsetSpin);

    m_dimensionAssocCheck = new QCheckBox(QStringLiteral("Связь с объектом"), dimensionPage);
    m_dimensionAssocCheck->setChecked(m_view->dimensionAssociative());
    dimensionLayout->addWidget(m_dimensionAssocCheck);

    auto *applyDimensionButton = new QToolButton(dimensionPage);
    applyDimensionButton->setText(QStringLiteral("Применить к выделенным"));
    dimensionLayout->addWidget(applyDimensionButton);
    dimensionLayout->addStretch(1);

    const auto applyLiveDimensionStyle = [this]() {
        if (!m_view)
            return;
        for (VectorItem *item : m_view->selectedVectorItems()) {
            if (item->object().type == ShapeType::Dimension) {
                m_view->applyDimensionSettingsToSelection();
                break;
            }
        }
    };
    connect(m_dimensionTypeCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_view)
                    m_view->setDimensionMode(
                        static_cast<CanvasView::DimensionMode>(qBound(0, index, 5)));
            });
    connect(m_dimensionUnitsCombo, &QComboBox::currentIndexChanged, this,
            [this, applyLiveDimensionStyle](int index) {
                if (m_view)
                    m_view->setDimensionUnits(index);
                applyLiveDimensionStyle();
            });
    connect(m_dimensionPrecisionSpin, &QSpinBox::valueChanged, this,
            [this, applyLiveDimensionStyle](int value) {
                if (m_view)
                    m_view->setDimensionPrecision(value);
                applyLiveDimensionStyle();
            });
    connect(m_dimensionOffsetSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                if (m_view)
                    m_view->setDimensionOffset(value);
            });
    connect(m_dimensionOffsetSpin, &QDoubleSpinBox::editingFinished, this,
            applyLiveDimensionStyle);
    connect(m_dimensionAssocCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_view)
            m_view->setDimensionAssociative(on);
    });
    connect(applyDimensionButton, &QToolButton::clicked, this, [this]() {
        if (m_view)
            m_view->applyDimensionSettingsToSelection();
    });

    m_propertyStack->addWidget(dimensionPage); // 10
    m_dimensionPageIndex = 10;
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

    m_selectionLabel = new QLabel(QStringLiteral("Выделено: 0"), this);
    m_selectionLabel->setStyleSheet(QStringLiteral("color:#8b929d; padding:0 10px;"));
    statusBar()->addPermanentWidget(m_selectionLabel);

    m_pageLabel = new QLabel(QStringLiteral("Страница 1 из 1"), this);
    m_pageLabel->setStyleSheet(QStringLiteral("color:#8b929d; padding:0 10px;"));
    statusBar()->addPermanentWidget(m_pageLabel);

    m_unitsCombo = new QComboBox(this);
    m_unitsCombo->addItems({QStringLiteral("px"), QStringLiteral("мм"),
                            QStringLiteral("см"), QStringLiteral("in")});
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

    m_scaleLabel = new QLabel(QStringLiteral("1:1"), this);
    m_scaleLabel->setStyleSheet(QStringLiteral("color:#8b929d; padding:0 10px;"));
    m_scaleLabel->setToolTip(QStringLiteral("Масштаб чертежа"));
    statusBar()->addPermanentWidget(m_scaleLabel);

    connect(m_zoomCombo, &QComboBox::activated, this, [this](int) {
        applyZoomText(m_zoomCombo->currentText());
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        applyZoomText(m_zoomCombo->currentText());
    });
    connect(m_unitsCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &) { updateRulers(); });
}

// ---------------------------------------------------------------------------
// Sample document
// ---------------------------------------------------------------------------

void MainWindow::populateSample()
{
    loadSampleDocument();
}

void MainWindow::showWelcome()
{
    if (!m_centralStack || !m_welcome)
        return;

    // The start screen is a clean cover: hide the editing chrome.
    if (m_mainToolBar)
        m_mainToolBar->hide();
    if (m_propertyBar)
        m_propertyBar->hide();
    if (m_toolsDock)
        m_toolsDock->hide();
    if (m_paletteDock)
        m_paletteDock->hide();
    if (m_dockPanels) {
        for (QDockWidget *dock : m_dockPanels->docks())
            dock->hide();
    }
    if (m_assetDock)
        m_assetDock->hide();
    statusBar()->hide();

    m_welcome->setRecentFiles(recentFiles());
    m_centralStack->setCurrentWidget(m_welcome);
}

void MainWindow::showEditor()
{
    if (!m_centralStack || !m_editorPage)
        return;

    m_centralStack->setCurrentWidget(m_editorPage);

    if (m_mainToolBar)
        m_mainToolBar->show();
    if (m_propertyBar)
        m_propertyBar->show();
    if (m_toolsDock)
        m_toolsDock->show();
    if (m_paletteDock)
        m_paletteDock->show();
    if (m_dockPanels) {
        for (QDockWidget *dock : m_dockPanels->docks())
            dock->show();
    }
    if (m_assetDock) {
        m_assetDock->show();
        m_assetDock->raise();
    }
    statusBar()->show();
}

void MainWindow::openCodeDialog(VectorItem *item, int kind)
{
    if (!m_view)
        return;
    if (item && item->object().type != ShapeType::Code)
        item = nullptr;
    CodeDialog dialog(m_view, item, this);
    if (!item && kind >= 0)
        dialog.setKind(codeKindFromIndex(kind));
    dialog.exec();
}

void MainWindow::showCanvasContextMenu(const QPoint &globalPos, VectorItem *item)
{
    QMenu menu(this);

    if (item) {
        if (item->isLocked()) {
            QAction *unlock = menu.addAction(QStringLiteral("Разблокировать объект"));
            connect(unlock, &QAction::triggered, this, [this, item]() {
                m_view->setItemLocked(item, false);
            });
            menu.addSeparator();
            QAction *properties = menu.addAction(QStringLiteral("Свойства"));
            connect(properties, &QAction::triggered, this, [this]() {
                showDockerTab(QStringLiteral("Свойства"), 0);
            });
        } else {
            if (item->object().type == ShapeType::Code) {
                QAction *editCode = menu.addAction(QStringLiteral("Редактировать код…"));
                connect(editCode, &QAction::triggered, this,
                        [this, item]() { openCodeDialog(item, -1); });
                menu.addSeparator();
            }
            menu.addAction(action(QStringLiteral("cut")));
            menu.addAction(action(QStringLiteral("copy")));
            menu.addAction(action(QStringLiteral("duplicate")));
            menu.addAction(action(QStringLiteral("clone")));
            menu.addSeparator();
            menu.addAction(action(QStringLiteral("delete")));
            menu.addSeparator();
            menu.addAction(action(QStringLiteral("group")));
            menu.addAction(action(QStringLiteral("ungroup")));
            menu.addAction(action(QStringLiteral("ungroup-all")));

            QMenu *order = menu.addMenu(QStringLiteral("Порядок"));
            order->addAction(action(QStringLiteral("order-front")));
            order->addAction(action(QStringLiteral("order-forward")));
            order->addAction(action(QStringLiteral("order-backward")));
            order->addAction(action(QStringLiteral("order-back")));

            menu.addSeparator();
            menu.addAction(action(QStringLiteral("flip-h")));
            menu.addAction(action(QStringLiteral("flip-v")));
            menu.addAction(action(QStringLiteral("rotate")));
            menu.addAction(action(QStringLiteral("skew")));
            menu.addAction(action(QStringLiteral("convert-curves")));
            menu.addSeparator();

            QAction *renameObject = menu.addAction(QStringLiteral("Переименовать объект…"));
            connect(renameObject, &QAction::triggered, this, [this, item]() {
                bool ok = false;
                const QString name = QInputDialog::getText(
                    this, QStringLiteral("Имя объекта"), QStringLiteral("Название:"),
                    QLineEdit::Normal, item->object().name, &ok);
                if (ok)
                    m_view->renameItem(item, name);
            });
            QAction *lock = menu.addAction(QStringLiteral("Заблокировать объект"));
            connect(lock, &QAction::triggered, this, [this, item]() {
                m_view->setItemLocked(item, true);
            });
            QAction *properties = menu.addAction(QStringLiteral("Свойства"));
            connect(properties, &QAction::triggered, this, [this]() {
                showDockerTab(QStringLiteral("Свойства"), 0);
            });
        }
    } else {
        menu.addAction(action(QStringLiteral("undo")));
        menu.addAction(action(QStringLiteral("redo")));
        menu.addSeparator();
        menu.addAction(action(QStringLiteral("paste")));
        menu.addAction(action(QStringLiteral("select-all")));
        menu.addSeparator();
        menu.addAction(action(QStringLiteral("grid")));
        menu.addAction(action(QStringLiteral("guides")));
        menu.addAction(action(QStringLiteral("print-area")));
        menu.addAction(action(QStringLiteral("snap")));
        menu.addAction(action(QStringLiteral("rulers")));
        menu.addSeparator();
        menu.addAction(action(QStringLiteral("fit")));
        menu.addAction(action(QStringLiteral("actual")));
        menu.addSeparator();
        menu.addAction(action(QStringLiteral("snap-settings")));
        QAction *pageSize = menu.addAction(QStringLiteral("Размер страницы…"));
        connect(pageSize, &QAction::triggered, this, &MainWindow::changePageSize);
    }

    menu.exec(globalPos);
}

void MainWindow::changePageSize()
{
    PageDialog dialog(this);
    if (m_view->currentPage() >= 0)
        dialog.setPageInfo(m_view->pageList().value(m_view->currentPage()));
    if (dialog.exec() != QDialog::Accepted)
        return;

    beginEdit();
    m_view->setPageSize(dialog.pageSizePx());
    if (m_view->currentPage() >= 0) {
        PageInfo info = m_view->pageList().value(m_view->currentPage());
        info.size = dialog.pageSizePx();
        info.background = dialog.pageBackground();
        info.margin = dialog.marginPx();
        info.bleed = dialog.bleedPx();
        m_view->setPageInfo(m_view->currentPage(), info);
    }
    endEdit();
    m_view->fitPage();
    updatePageBadge();
    statusBar()->showMessage(
        QStringLiteral("Размер страницы: %1 × %2 px")
            .arg(qRound(m_view->pageSize().width()))
            .arg(qRound(m_view->pageSize().height())), 4000);
}

void MainWindow::showDockerTab(const QString &title, int tab)
{
    if (!m_dockPanels)
        return;
    if (m_centralStack && m_editorPage
        && m_centralStack->currentWidget() != m_editorPage)
        showEditor();

    for (QDockWidget *dock : m_dockPanels->docks()) {
        if (dock->windowTitle() != title)
            continue;
        dock->show();
        dock->raise();
        if (auto *tabs = dock->findChild<QTabWidget *>()) {
            if (tab >= 0 && tab < tabs->count())
                tabs->setCurrentIndex(tab);
        }
        break;
    }
}

void MainWindow::loadSampleDocument()
{
    if (!maybeSave())
        return;
    applyDocument(Document::sample());
    m_filePath.clear();
    m_modified = false;
    resetHistory();
    updateTitle();
    showEditor();
    m_view->fitPage();
    statusBar()->showMessage(QStringLiteral("Открыт демонстрационный документ."), 4000);
}

QStringList MainWindow::recentFiles() const
{
    QSettings settings;
    return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void MainWindow::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList files = recentFiles();
    files.removeAll(absolute);
    files.prepend(absolute);
    while (files.size() > 8)
        files.removeLast();

    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), files);
    if (m_welcome)
        m_welcome->setRecentFiles(files);
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
    m_currentTool = tool;

    int page = m_transformPageIndex;
    switch (tool) {
    case Tool::Zoom:
    case Tool::Pan:
        page = m_zoomPageIndex;
        break;
    case Tool::Bezier:
        page = m_bezierPageIndex;
        break;
    case Tool::Node:
        page = m_nodePageIndex;
        break;
    case Tool::Text:
        page = m_textPageIndex;
        break;
    case Tool::Rectangle:
        page = m_rectPageIndex;
        break;
    case Tool::Polygon:
        page = m_polygonPageIndex;
        break;
    case Tool::Star:
        page = m_starPageIndex;
        break;
    case Tool::Spiral:
        page = m_spiralPageIndex;
        break;
    case Tool::Eyedropper:
    case Tool::Fill:
    case Tool::Outline:
    case Tool::Transparency:
    case Tool::Shadow:
    case Tool::Blend:
    case Tool::Extrude:
    case Tool::Perspective:
        page = m_paintPageIndex;
        break;
    case Tool::Dimension:
        page = m_dimensionPageIndex;
        break;
    default:
        page = m_transformPageIndex;
        break;
    }
    m_propertyStack->setCurrentIndex(page);
    syncShapeOptions();

    m_hintLabel->setText(QStringLiteral("%1 · %2").arg(info->name, info->hint));
    statusBar()->showMessage(QStringLiteral("Инструмент: %1 (%2)")
                                 .arg(info->name, info->shortcut),
                             3500);
}

void MainWindow::startDimensionTool(int type)
{
    applyTool(Tool::Dimension);
    if (m_view)
        m_view->setDimensionMode(
            static_cast<CanvasView::DimensionMode>(qBound(0, type, 5)));
    if (m_dimensionTypeCombo) {
        const QSignalBlocker blocker(m_dimensionTypeCombo);
        m_dimensionTypeCombo->setCurrentIndex(qBound(0, type, 5));
    }
}

void MainWindow::updateRulers()
{
    if (!m_hRuler || !m_vRuler)
        return;
    const QString unit = m_unitsCombo ? m_unitsCombo->currentText()
                                      : QStringLiteral("мм");
    qreal base = 1.0;
    if (unit == QLatin1String("мм"))
        base = kPxPerMm;
    else if (unit == QLatin1String("см"))
        base = kPxPerMm * 10.0;
    else if (unit == QLatin1String("in"))
        base = 96.0;
    const qreal scale = m_view ? qMax<qreal>(0.01, m_view->drawingScale()) : 1.0;
    m_hRuler->setUnit(unit, base / scale);
    m_vRuler->setUnit(unit, base / scale);
    if (m_rulerCorner)
        m_rulerCorner->setText(unit);
    updateCoordinates(m_cursorScenePos);
}

void MainWindow::updateScaleLabel()
{
    if (!m_scaleLabel || !m_view)
        return;
    m_scaleLabel->setText(QStringLiteral("1:%1")
                              .arg(m_view->drawingScale(), 0, 'g', 6));
    m_scaleLabel->setVisible(m_view->drawingScale() > 0.0);
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
        else if (unit == QLatin1String("in"))
            factor = 1.0 / 96.0;
        else if (unit == QLatin1String("см"))
            factor = 1.0 / (kPxPerMm * 10.0);
        else
            factor = 1.0 / kPxPerMm;
    }

    if (unit != QLatin1String("px") && m_view)
        factor *= m_view->drawingScale();

    m_coordsLabel->setText(QStringLiteral("X: %1  Y: %2 %3")
                               .arg(scenePos.x() * factor, 0, 'f', 1)
                               .arg(scenePos.y() * factor, 0, 'f', 1)
                               .arg(unit));
}

void MainWindow::selectAllObjects()
{
    m_view->selectAllObjects();
    statusBar()->showMessage(QStringLiteral("Выделены все объекты."), 3000);
}

void MainWindow::deleteSelection()
{
    if (m_view->hasSelectedNodes())
        m_view->deleteSelectedNodes();
    else
        m_view->deleteSelectedObjects();
}

// ---------------------------------------------------------------------------
// Document and file handling
// ---------------------------------------------------------------------------

Document MainWindow::currentDocument() const
{
    Document document;
    if (!m_view)
        return document;
    document.pageSize = m_view->pageSize();
    document.drawingScale = m_view->drawingScale();
    document.pages = m_view->pageList();
    document.masters = m_view->masterList();
    document.currentPage = m_view->currentPage();
    if (!document.pages.isEmpty() && document.currentPage >= 0
        && document.currentPage < document.pages.size())
        document.pages[document.currentPage].size = m_view->pageSize();
    document.layers = m_view->layers();
    document.groups = m_view->groups();
    document.blends = m_view->blends();
    document.symbols = m_view->symbols();
    document.instances = m_view->instances();
    document.patterns = m_view->patterns();
    document.guidesX = m_view->guidesX();
    document.guidesY = m_view->guidesY();
    document.layerVisible.clear();
    document.layerLocked.clear();
    for (int i = 0; i < document.layers.size(); ++i) {
        document.layerVisible.append(m_view->layerVisible(i));
        document.layerLocked.append(m_view->layerLocked(i));
    }
    document.objects = m_view->objects();
    return document;
}

void MainWindow::applyDocument(const Document &document)
{
    m_restoring = true;
    m_view->setDocument(document);
    m_restoring = false;
    if (m_dockPanels)
        m_dockPanels->refresh();
    updatePageBadge();
    updateScaleLabel();
    updateRulers();
    updateHistoryActions();
}

void MainWindow::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    updateTitle();
}

void MainWindow::updateTitle()
{
    QString name = QStringLiteral("Без имени");
    if (!m_filePath.isEmpty())
        name = QFileInfo(m_filePath).fileName();
    setWindowTitle(QStringLiteral("%1%2 — Vectora")
                       .arg(name, m_modified ? QStringLiteral(" *") : QString()));
}

bool MainWindow::maybeSave()
{
    if (!m_modified || m_view->objects().isEmpty())
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, QStringLiteral("Несохранённые изменения"),
        QStringLiteral("Документ изменён. Сохранить изменения?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        saveDocument();
        return !m_modified;
    }
    return true;
}

void MainWindow::newDocument()
{
    if (!maybeSave())
        return;

    PageDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    Document document;
    document.pageSize = dialog.pageSizePx();
    PageInfo info;
    info.name = QStringLiteral("Страница 1");
    info.size = document.pageSize;
    info.background = dialog.pageBackground();
    info.margin = dialog.marginPx();
    info.bleed = dialog.bleedPx();
    document.pages.append(info);
    applyDocument(document);
    m_filePath.clear();
    m_versions.clear();
    m_modified = false;
    resetHistory();
    clearAutosave();
    updateTitle();
    showEditor();
    m_view->fitPage();

    m_view->setShowGrid(false);
    if (QAction *gridAction = action(QStringLiteral("grid")))
        gridAction->setChecked(false);
    m_view->setShowPrintableArea(false);
    if (QAction *printAction = action(QStringLiteral("print-area")))
        printAction->setChecked(false);

    if (m_unitsCombo) {
        const int index = m_unitsCombo->findText(dialog.unitName());
        if (index >= 0)
            m_unitsCombo->setCurrentIndex(index);
    }
    updatePageBadge();
    statusBar()->showMessage(
        QStringLiteral("Создан документ %1 × %2 px")
            .arg(qRound(document.pageSize.width()))
            .arg(qRound(document.pageSize.height())), 4000);
}

void MainWindow::updatePageBadge()
{
    if (!m_pageBadge || !m_view)
        return;
    const QSizeF size = m_view->pageSize();
    const qreal mmWidth = size.width() / 3.7795275591;
    const qreal mmHeight = size.height() / 3.7795275591;
    QString label;
    const auto close = [](qreal a, qreal b) { return qAbs(a - b) < 2.0; };
    if (close(mmWidth, 210.0) && close(mmHeight, 297.0))
        label = QStringLiteral("A4  ·  210 × 297 мм");
    else if (close(mmWidth, 297.0) && close(mmHeight, 420.0))
        label = QStringLiteral("A3  ·  297 × 420 мм");
    else if (close(mmWidth, 420.0) && close(mmHeight, 594.0))
        label = QStringLiteral("A2  ·  420 × 594 мм");
    else if (close(size.width(), 1920.0) && close(size.height(), 1080.0))
        label = QStringLiteral("1920 × 1080 px");
    else
        label = QStringLiteral("%1 × %2 px")
                    .arg(qRound(size.width()))
                    .arg(qRound(size.height()));
    m_pageBadge->setText(label);
    if (m_pageLabel) {
        const int current = m_view->currentPage();
        const int total = m_view->pageList().size();
        if (current >= 0) {
            m_pageLabel->setText(QStringLiteral("%1  ·  страница %2 из %3")
                                     .arg(m_view->pageLabel(current))
                                     .arg(current + 1)
                                     .arg(total));
        } else {
            m_pageLabel->setText(QStringLiteral("Мастер: %1")
                                     .arg(m_view->pageLabel(current).section(
                                         QStringLiteral(": "), 1)));
        }
    }
}

void MainWindow::openDocument()
{
    if (!maybeSave())
        return;

    QString directory = m_filePath.isEmpty() ? QDir::homePath()
                                             : QFileInfo(m_filePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Открыть документ"), directory,
        QStringLiteral("Документы Vectora (*.vct);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    if (openPath(path)) {
        statusBar()->showMessage(
            QStringLiteral("Открыт документ: %1").arg(QFileInfo(path).fileName()), 5000);
    }
}

bool MainWindow::openPath(const QString &path)
{
    Document document;
    QString error;
    if (!document.load(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка открытия"), error);
        return false;
    }

    applyDocument(document);
    m_filePath = path;
    m_modified = false;
    m_versions = loadVersions(path);
    resetHistory();
    loadHistorySidecar();
    clearAutosave();
    updateTitle();
    addRecentFile(path);
    showEditor();
    m_view->fitPage();
    const QStringList absent = missingFonts(document);
    if (!absent.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("Отсутствуют шрифты: %1").arg(absent.join(QStringLiteral(", "))),
            8000);
    }
    return true;
}

void MainWindow::saveDocument()
{
    if (m_filePath.isEmpty()) {
        saveDocumentAs();
        return;
    }

    QString error;
    if (!currentDocument().save(m_filePath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"), error);
        return;
    }

    m_modified = false;
    clearAutosave();
    saveHistorySidecar();
    saveVersions(m_filePath, m_versions, nullptr);
    updateTitle();
    addRecentFile(m_filePath);
    statusBar()->showMessage(
        QStringLiteral("Сохранено: %1").arg(QFileInfo(m_filePath).fileName()), 4000);
}

void MainWindow::saveDocumentAs()
{
    QString suggested = m_filePath;
    if (suggested.isEmpty())
        suggested = QDir::homePath() + QStringLiteral("/без имени.vct");

    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Сохранить как"), suggested,
        QStringLiteral("Документы Vectora (*.vct)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".vct"), Qt::CaseInsensitive))
        path += QStringLiteral(".vct");

    QString error;
    if (!currentDocument().save(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"), error);
        return;
    }

    m_filePath = path;
    m_modified = false;
    clearAutosave();
    saveHistorySidecar();
    saveVersions(m_filePath, m_versions, nullptr);
    updateTitle();
    addRecentFile(path);
    statusBar()->showMessage(
        QStringLiteral("Сохранено: %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Импорт"), QDir::homePath(),
        QStringLiteral("Изображения и SVG (*.png *.jpg *.jpeg *.bmp *.svg);;"
                       "Все файлы (*)"));
    if (path.isEmpty())
        return;

    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        Document imported;
        QString error;
        if (!importSvgFile(path, &imported, &error)) {
            QMessageBox::critical(this, QStringLiteral("Импорт SVG"), error);
            return;
        }

        const int layer = m_view->addLayer(
            QFileInfo(path).completeBaseName().left(32));
        QHash<int, int> groupMap;
        for (const VectorGroup &group : imported.groups) {
            const int parent = group.parent > 0 ? groupMap.value(group.parent, 0)
                                                : 0;
            groupMap.insert(group.id, m_view->addGroupRecord(parent));
        }

        beginEdit();
        m_view->scene()->clearSelection();
        int importedCount = 0;
        for (VectorObject object : imported.objects) {
            object.layer = layer;
            object.group = groupMap.value(object.group, 0);
            object.id = 0;
            object.masterId = 0;
            VectorItem *item = m_view->addVectorObject(object, false, false);
            if (item) {
                item->setSelected(true);
                ++importedCount;
            }
        }
        endEdit();
        statusBar()->showMessage(
            QStringLiteral("Импортировано объектов SVG: %1").arg(importedCount), 5000);
        return;
    }

    QImage image;
    image.load(path);

    if (image.isNull()) {
        QMessageBox::critical(this, QStringLiteral("Импорт"),
                              QStringLiteral("Не удалось прочитать изображение."));
        return;
    }

    const QSizeF page = m_view->pageSize();
    QSizeF size = image.size();
    const qreal maxWidth = page.width() * 0.8;
    const qreal maxHeight = page.height() * 0.8;
    const qreal scale = qMin<qreal>(1.0, qMin(maxWidth / size.width(),
                                              maxHeight / size.height()));
    size *= scale;

    VectorObject object;
    object.type = ShapeType::Image;
    object.image = image;
    object.textBox = QRectF(0.0, 0.0, size.width(), size.height());
    object.pos = QPointF((page.width() - size.width()) / 2.0,
                         (page.height() - size.height()) / 2.0);
    object.hasFill = false;
    object.hasStroke = false;

    beginEdit();
    m_view->addVectorObject(object);
    endEdit();
    statusBar()->showMessage(
        QStringLiteral("Импортировано: %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::exportFile()
{
    QString suggested = m_filePath;
    if (suggested.isEmpty())
        suggested = QDir::homePath() + QStringLiteral("/без имени.png");
    else
        suggested = QFileInfo(suggested).absolutePath() + QLatin1Char('/')
                    + QFileInfo(suggested).completeBaseName() + QStringLiteral(".png");

    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Экспорт"), suggested,
        QStringLiteral("PNG-изображение (*.png);;SVG-вектор (*.svg)"), &selectedFilter);
    if (path.isEmpty())
        return;
    if (!path.contains(QLatin1Char('.'))) {
        if (selectedFilter.contains(QLatin1String("SVG")))
            path += QStringLiteral(".svg");
        else
            path += QStringLiteral(".png");
    }

    const Document document = currentDocument();

    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, QStringLiteral("Экспорт"),
                                  QStringLiteral("Не удалось открыть файл: %1")
                                      .arg(file.errorString()));
            return;
        }
        file.write(document.toSvg().toUtf8());
        if (!file.commit()) {
            QMessageBox::critical(this, QStringLiteral("Экспорт"),
                                  QStringLiteral("Не удалось сохранить файл: %1")
                                      .arg(file.errorString()));
            return;
        }
    } else {
        constexpr qreal scale = 2.0;
        QImage image(QSize(qRound(document.pageSize.width() * scale),
                           qRound(document.pageSize.height() * scale)),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.scale(scale, scale);
        document.render(&painter);
        painter.end();
        if (!image.save(path)) {
            QMessageBox::critical(this, QStringLiteral("Экспорт"),
                                  QStringLiteral("Не удалось сохранить PNG."));
            return;
        }
    }

    statusBar()->showMessage(
        QStringLiteral("Экспортировано: %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::exportPdf()
{
    QString suggested = m_filePath;
    if (suggested.isEmpty())
        suggested = QDir::homePath() + QStringLiteral("/без имени.pdf");
    else
        suggested = QFileInfo(suggested).absolutePath() + QLatin1Char('/')
                    + QFileInfo(suggested).completeBaseName() + QStringLiteral(".pdf");

    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Экспорт в PDF"), suggested,
        QStringLiteral("PDF-документ (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");

    const Document source = currentDocument();
    const QVector<PageInfo> pages = source.pages;
    QPdfWriter writer(path);
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter;
    bool started = false;
    const int count = qMax(1, pages.size());
    for (int index = 0; index < count; ++index) {
        Document pageDocument = source;
        pageDocument.currentPage = index < pages.size() ? index : 0;
        if (index < pages.size())
            pageDocument.pageSize = pages.at(index).size;
        const QSizeF millimeters(pageDocument.pageSize.width() * 25.4 / 96.0,
                                 pageDocument.pageSize.height() * 25.4 / 96.0);
        writer.setPageSize(QPageSize(millimeters, QPageSize::Millimeter,
                                     QStringLiteral("Vectora")));
        if (!started) {
            if (!painter.begin(&writer)) {
                QMessageBox::critical(this, QStringLiteral("Экспорт"),
                                      QStringLiteral("Не удалось создать PDF."));
                return;
            }
            started = true;
        } else {
            writer.newPage();
        }
        painter.save();
        const qreal scaleX = writer.width() / pageDocument.pageSize.width();
        const qreal scaleY = writer.height() / pageDocument.pageSize.height();
        painter.scale(scaleX, scaleY);
        pageDocument.render(&painter);
        painter.restore();
    }
    if (started)
        painter.end();

    statusBar()->showMessage(
        QStringLiteral("PDF сохранён: %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::exportAllPages()
{
    const QVector<PageInfo> pages = m_view->pageList();
    if (pages.isEmpty())
        return;
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Куда сохранить страницы"), QDir::homePath());
    if (directory.isEmpty())
        return;
    bool ok = false;
    const int scale = QInputDialog::getInt(
        this, QStringLiteral("Разрешение"),
        QStringLiteral("Масштаб (1 = 96 dpi, 2 = 192 dpi):"), 2, 1, 8, 1, &ok);
    if (!ok)
        return;

    int saved = 0;
    for (int index = 0; index < pages.size(); ++index) {
        const QSizeF size = pages.at(index).size;
        const QSize target(qMax(1, qRound(size.width() * scale)),
                           qMax(1, qRound(size.height() * scale)));
        const QImage image = m_view->renderPageImage(index, target);
        const QString path = QStringLiteral("%1/page-%2.png")
                                 .arg(directory)
                                 .arg(index + 1, 3, 10, QLatin1Char('0'));
        if (image.save(path))
            ++saved;
    }
    statusBar()->showMessage(
        QStringLiteral("Сохранено страниц в PNG: %1").arg(saved), 5000);
}

void MainWindow::printDocument()
{
    const Document document = currentDocument();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(m_filePath.isEmpty() ? QStringLiteral("Vectora")
                                            : QFileInfo(m_filePath).fileName());

    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::critical(this, QStringLiteral("Печать"),
                              QStringLiteral("Не удалось начать печать."));
        return;
    }

    const QVector<PageInfo> pages = document.pages;
    const int count = qMax(1, pages.size());
    for (int index = 0; index < count; ++index) {
        Document pageDocument = document;
        pageDocument.currentPage = index < pages.size() ? index : 0;
        if (index < pages.size())
            pageDocument.pageSize = pages.at(index).size;
        if (index > 0)
            printer.newPage();
        const QRectF page = printer.pageRect(QPrinter::DevicePixel);
        const qreal scale = qMin(page.width() / pageDocument.pageSize.width(),
                                 page.height() / pageDocument.pageSize.height());
        painter.save();
        painter.translate(page.topLeft());
        painter.scale(scale, scale);
        pageDocument.render(&painter);
        painter.restore();
    }
    painter.end();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void MainWindow::beginEdit()
{
    if (m_restoring || m_editOpen || !m_view)
        return;
    m_editOpen = true;
    m_pendingSnapshot = currentDocument().toJson();
}

void MainWindow::endEdit()
{
    if (m_restoring) {
        m_editOpen = false;
        return;
    }
    if (!m_editOpen)
        return;
    m_editOpen = false;

    m_view->syncClones();

    const QJsonObject current = currentDocument().toJson();
    if (current == m_pendingSnapshot)
        return;

    QString label = m_lastHint;
    if (label.isEmpty() || !m_lastHintTime.isValid()
        || m_lastHintTime.secsTo(QDateTime::currentDateTime()) > 4) {
        label = QStringLiteral("Изменение документа");
    }
    m_undoStack.append(m_pendingSnapshot);
    m_undoLabels.append(label);
    m_undoTimes.append(QDateTime::currentDateTime());
    if (m_undoStack.size() > 100) {
        m_undoStack.removeFirst();
        m_undoLabels.removeFirst();
        m_undoTimes.removeFirst();
    }
    m_redoStack.clear();
    m_redoLabels.clear();
    m_redoTimes.clear();

    m_modified = true;
    updateTitle();
    if (m_dockPanels)
        m_dockPanels->refresh();
    refreshQuickTransform();
    updateHistoryActions();
}

void MainWindow::undo()
{
    if (m_undoStack.isEmpty())
        return;

    const QJsonObject previous = m_undoStack.takeLast();
    const QString label = m_undoLabels.isEmpty()
                              ? QStringLiteral("Действие")
                              : m_undoLabels.takeLast();
    const QDateTime time = m_undoTimes.isEmpty()
                               ? QDateTime::currentDateTime()
                               : m_undoTimes.takeLast();
    m_redoStack.append(currentDocument().toJson());
    m_redoLabels.append(label);
    m_redoTimes.append(time);

    QString error;
    const Document document = Document::fromJson(previous, &error);
    m_restoring = true;
    m_view->setDocument(document);
    m_restoring = false;

    m_modified = true;
    updateTitle();
    if (m_dockPanels)
        m_dockPanels->refresh();
    if (m_assetPanel) {
        m_assetPanel->refreshSymbols();
        m_assetPanel->refreshPatterns();
    }
    updateHistoryActions();
    statusBar()->showMessage(QStringLiteral("Действие отменено: %1").arg(label), 2500);
}

void MainWindow::redo()
{
    if (m_redoStack.isEmpty())
        return;

    const QJsonObject next = m_redoStack.takeLast();
    const QString label = m_redoLabels.isEmpty()
                              ? QStringLiteral("Действие")
                              : m_redoLabels.takeLast();
    const QDateTime time = m_redoTimes.isEmpty()
                               ? QDateTime::currentDateTime()
                               : m_redoTimes.takeLast();
    m_undoStack.append(currentDocument().toJson());
    m_undoLabels.append(label);
    m_undoTimes.append(time);

    QString error;
    const Document document = Document::fromJson(next, &error);
    m_restoring = true;
    m_view->setDocument(document);
    m_restoring = false;

    m_modified = true;
    updateTitle();
    if (m_dockPanels)
        m_dockPanels->refresh();
    if (m_assetPanel) {
        m_assetPanel->refreshSymbols();
        m_assetPanel->refreshPatterns();
    }
    updateHistoryActions();
    statusBar()->showMessage(QStringLiteral("Действие повторено: %1").arg(label), 2500);
}

void MainWindow::resetHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoLabels.clear();
    m_redoLabels.clear();
    m_undoTimes.clear();
    m_redoTimes.clear();
    m_editOpen = false;
    updateHistoryActions();
}

void MainWindow::updateHistoryActions()
{
    QAction *undoAction = action(QStringLiteral("undo"));
    QAction *redoAction = action(QStringLiteral("redo"));
    if (undoAction)
        undoAction->setEnabled(!m_undoStack.isEmpty());
    if (redoAction)
        redoAction->setEnabled(!m_redoStack.isEmpty());
}

void MainWindow::setupAutosave()
{
    if (!m_autosaveTimer) {
        m_autosaveTimer = new QTimer(this);
        connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
    }
    QSettings settings;
    const bool enabled = settings.value(QStringLiteral("autosave/enabled"), true).toBool();
    const int interval = qBound(10, settings.value(QStringLiteral("autosave/interval"), 120).toInt(),
                                3600);
    if (enabled)
        m_autosaveTimer->start(interval * 1000);
    else
        m_autosaveTimer->stop();
}

void MainWindow::performAutosave()
{
    if (!m_modified || !m_view || m_view->objects().isEmpty())
        return;
    QSettings settings;
    const int keep = qBound(1, settings.value(QStringLiteral("autosave/backups"), 5).toInt(), 50);
    const QString title = m_filePath.isEmpty()
                              ? QStringLiteral("Без имени")
                              : QFileInfo(m_filePath).fileName();
    QString path;
    QString error;
    if (AutoSaveManager::saveSnapshot(m_autosaveSession, m_filePath, title,
                                      currentDocument().toJson(), keep, &path,
                                      &error)) {
        statusBar()->showMessage(QStringLiteral("Автосохранение выполнено."), 1500);
    }
}

void MainWindow::clearAutosave()
{
    if (!m_autosaveSession.isEmpty())
        AutoSaveManager::discardSession(m_autosaveSession);
}

void MainWindow::recoverAutosaves()
{
    static bool checked = false;
    if (checked)
        return;
    checked = true;
    const QVector<AutoSaveEntry> entries = AutoSaveManager::pending();
    if (entries.isEmpty())
        return;

    QString details;
    for (const AutoSaveEntry &entry : entries) {
        details += QStringLiteral("%1 · %2\n%3\n\n")
                       .arg(entry.title,
                            entry.time.toLocalTime().toString(
                                QStringLiteral("dd.MM.yyyy hh:mm")),
                            entry.originalPath.isEmpty()
                                ? QStringLiteral("(без исходного файла)")
                                : entry.originalPath);
    }
    QMessageBox box(QMessageBox::Question, QStringLiteral("Восстановление документа"),
                    QStringLiteral("Найдены автосохранения после аварийного завершения."),
                    QMessageBox::NoButton, this);
    box.setDetailedText(details);
    auto *restore = box.addButton(QStringLiteral("Восстановить последнее"),
                                  QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("Удалить"), QMessageBox::DestructiveRole);
    auto *later = box.addButton(QStringLiteral("Позже"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == later || box.clickedButton() == nullptr)
        return;

    if (box.clickedButton() == restore) {
        const AutoSaveEntry entry = entries.first();
        QString originalPath;
        const QJsonObject snapshot = AutoSaveManager::loadSnapshot(entry.snapshotPath,
                                                                   &originalPath);
        if (snapshot.isEmpty())
            return;
        QString error;
        const Document document = Document::fromJson(snapshot, &error);
        applyDocument(document);
        m_filePath = originalPath;
        m_modified = true;
        resetHistory();
        updateTitle();
        statusBar()->showMessage(QStringLiteral("Документ восстановлен из автосохранения."),
                                 5000);
        QFile::remove(entry.snapshotPath);
        QFile::remove(entry.snapshotPath + QStringLiteral(".meta.json"));
    } else if (box.clickedButton() == discard) {
        AutoSaveManager::discardAll();
    }
}

void MainWindow::saveHistorySidecar()
{
    if (m_filePath.isEmpty() || m_restoring)
        return;
    QSettings settings;
    if (!settings.value(QStringLiteral("history/persist"), true).toBool())
        return;
    const int limit = qBound(1, settings.value(QStringLiteral("history/limit"), 20).toInt(),
                             100);
    QJsonArray states;
    const int start = qMax(0, m_undoStack.size() - limit);
    for (int i = start; i < m_undoStack.size(); ++i) {
        QJsonObject entry;
        entry.insert(QStringLiteral("label"),
                     m_undoLabels.value(i, QStringLiteral("Изменение документа")));
        entry.insert(QStringLiteral("time"),
                     m_undoTimes.value(i).toString(Qt::ISODate));
        entry.insert(QStringLiteral("snapshot"), m_undoStack.at(i));
        states.append(entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-history"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("states"), states);
    QSaveFile file(m_filePath + QStringLiteral(".history.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void MainWindow::loadHistorySidecar()
{
    resetHistory();
    if (m_filePath.isEmpty())
        return;
    QSettings settings;
    if (!settings.value(QStringLiteral("history/persist"), true).toBool())
        return;
    QFile file(m_filePath + QStringLiteral(".history.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value :
         root.value(QStringLiteral("states")).toArray()) {
        const QJsonObject entry = value.toObject();
        m_undoStack.append(entry.value(QStringLiteral("snapshot")).toObject());
        m_undoLabels.append(entry.value(QStringLiteral("label"))
                                .toString(QStringLiteral("Изменение документа")));
        m_undoTimes.append(QDateTime::fromString(
            entry.value(QStringLiteral("time")).toString(), Qt::ISODate));
    }
    updateHistoryActions();
}

void MainWindow::restoreHistoryIndex(int index)
{
    if (index < 0 || index >= m_undoStack.size())
        return;

    const QJsonObject current = currentDocument().toJson();
    QVector<QJsonObject> redoStates;
    QStringList redoLabels;
    QVector<QDateTime> redoTimes;
    redoStates.append(current);
    redoLabels.append(m_undoLabels.value(m_undoStack.size() - 1,
                                         QStringLiteral("Изменение документа")));
    redoTimes.append(QDateTime::currentDateTime());
    for (int i = m_undoStack.size() - 1; i > index; --i) {
        redoStates.append(m_undoStack.at(i));
        redoLabels.append(m_undoLabels.value(i - 1,
                                             QStringLiteral("Изменение документа")));
        redoTimes.append(m_undoTimes.value(i));
    }

    const QJsonObject target = m_undoStack.at(index);
    m_undoStack = m_undoStack.mid(0, index);
    m_undoLabels = m_undoLabels.mid(0, index);
    m_undoTimes = m_undoTimes.mid(0, index);
    m_redoStack = redoStates;
    m_redoLabels = redoLabels;
    m_redoTimes = redoTimes;

    QString error;
    const Document document = Document::fromJson(target, &error);
    if (error.isEmpty()) {
        m_restoring = true;
        m_view->setDocument(document);
        m_restoring = false;
        m_modified = true;
        updateTitle();
        if (m_dockPanels)
            m_dockPanels->refresh();
        if (m_assetPanel) {
            m_assetPanel->refreshSymbols();
            m_assetPanel->refreshPatterns();
        }
        statusBar()->showMessage(QStringLiteral("Восстановлено состояние истории."), 3000);
    }
    updateHistoryActions();
}

void MainWindow::openHistoryDialog()
{
    HistoryDialog dialog(m_undoLabels, m_undoTimes, m_undoStack.size(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int index = dialog.selectedIndex();
    if (index >= 0 && index < m_undoStack.size())
        restoreHistoryIndex(index);
}

void MainWindow::openVersionDialog()
{
    const QString display = m_filePath.isEmpty()
                                ? QStringLiteral("Без имени")
                                : QFileInfo(m_filePath).fileName();
    VersionDialog dialog(&m_versions, currentDocument().toJson(), display, this);
    dialog.exec();

    if (dialog.versionsChanged()) {
        QString error;
        if (!saveVersions(m_filePath, m_versions, &error)) {
            QMessageBox::warning(this, QStringLiteral("Версии документа"), error);
        }
    }
    if (!dialog.restoreRequested())
        return;
    QString error;
    const Document document = Document::fromJson(dialog.restoreSnapshot(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Версии документа"), error);
        return;
    }
    beginEdit();
    applyDocument(document);
    endEdit();
    m_modified = true;
    updateTitle();
    statusBar()->showMessage(QStringLiteral("Версия документа восстановлена."), 4000);
}

void MainWindow::openTemplateDialog()
{
    if (!maybeSave())
        return;
    TemplateDialog dialog(currentDocument().toJson(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QJsonObject snapshot = dialog.selectedDocument();
    if (snapshot.isEmpty())
        return;
    QString error;
    const Document document = Document::fromJson(snapshot, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Шаблоны"), error);
        return;
    }
    m_filePath.clear();
    m_versions.clear();
    applyDocument(document);
    m_modified = true;
    resetHistory();
    clearAutosave();
    updateTitle();
    showEditor();
    m_view->fitPage();
    const QStringList absent = missingFonts(document);
    statusBar()->showMessage(
        absent.isEmpty()
            ? QStringLiteral("Документ создан из шаблона.")
            : QStringLiteral("Документ создан из шаблона. Отсутствуют шрифты: %1")
                  .arg(absent.join(QStringLiteral(", "))),
        absent.isEmpty() ? 4000 : 8000);
}

void MainWindow::openResourceSearch()
{
    ResourceSearchDialog dialog(m_view, this);
    dialog.exec();
}

void MainWindow::packageCurrentDocument()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Куда упаковать документ"), QDir::homePath());
    if (directory.isEmpty())
        return;
    const QString name = m_filePath.isEmpty()
                             ? QStringLiteral("vectora-document")
                             : QFileInfo(m_filePath).completeBaseName();
    const PackageReport report = packageDocument(currentDocument(), directory, name);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Упаковка документа"));
    dialog.resize(620, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setPlainText(report.ok ? report.text
                                 : (report.error.isEmpty() ? report.text
                                                           : report.error + QLatin1Char('\n')
                                                                 + report.text));
    layout->addWidget(text, 1);
    auto *buttons = new QHBoxLayout;
    auto *openFolder = new QPushButton(QStringLiteral("Открыть папку"), &dialog);
    openFolder->setEnabled(report.ok);
    auto *close = new QPushButton(QStringLiteral("Закрыть"), &dialog);
    buttons->addWidget(openFolder);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(openFolder, &QPushButton::clicked, &dialog, [&report]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(report.folder));
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
    if (report.ok)
        statusBar()->showMessage(QStringLiteral("Документ упакован: %1").arg(report.folder),
                                 5000);
}

void MainWindow::showPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Настройки"));
    auto *form = new QFormLayout(&dialog);

    QSettings settings;
    auto *autosaveEnabled = new QCheckBox(QStringLiteral("Автосохранение"), &dialog);
    autosaveEnabled->setChecked(
        settings.value(QStringLiteral("autosave/enabled"), true).toBool());
    auto *autosaveInterval = new QSpinBox(&dialog);
    autosaveInterval->setRange(10, 3600);
    autosaveInterval->setSuffix(QStringLiteral(" с"));
    autosaveInterval->setValue(
        settings.value(QStringLiteral("autosave/interval"), 120).toInt());
    auto *autosaveBackups = new QSpinBox(&dialog);
    autosaveBackups->setRange(1, 50);
    autosaveBackups->setValue(
        settings.value(QStringLiteral("autosave/backups"), 5).toInt());
    auto *persistHistory = new QCheckBox(
        QStringLiteral("Сохранять историю между сеансами"), &dialog);
    persistHistory->setChecked(
        settings.value(QStringLiteral("history/persist"), true).toBool());
    auto *historyLimit = new QSpinBox(&dialog);
    historyLimit->setRange(5, 100);
    historyLimit->setValue(settings.value(QStringLiteral("history/limit"), 20).toInt());
    auto *checkUpdates = new QCheckBox(
        QStringLiteral("Проверять обновления при запуске"), &dialog);
    checkUpdates->setChecked(
        settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());

    form->addRow(autosaveEnabled);
    form->addRow(QStringLiteral("Интервал автосохранения"), autosaveInterval);
    form->addRow(QStringLiteral("Резервных копий"), autosaveBackups);
    form->addRow(persistHistory);
    form->addRow(QStringLiteral("Глубина истории"), historyLimit);
    form->addRow(checkUpdates);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue(QStringLiteral("autosave/enabled"), autosaveEnabled->isChecked());
    settings.setValue(QStringLiteral("autosave/interval"), autosaveInterval->value());
    settings.setValue(QStringLiteral("autosave/backups"), autosaveBackups->value());
    settings.setValue(QStringLiteral("history/persist"), persistHistory->isChecked());
    settings.setValue(QStringLiteral("history/limit"), historyLimit->value());
    settings.setValue(QStringLiteral("updates/checkOnStartup"), checkUpdates->isChecked());
    setupAutosave();
    statusBar()->showMessage(QStringLiteral("Настройки сохранены."), 3000);
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

void MainWindow::onPaletteColor(const QColor &color)
{
    m_view->setFillColor(color);
    m_toolPalette->setFillColor(color);
    applyFillToSelection(color);
    statusBar()->showMessage(
        QStringLiteral("Цвет заливки: %1").arg(color.name().toUpper()), 4000);
}

void MainWindow::applyFillToSelection(const QColor &color)
{
    const QList<VectorItem *> selected = m_view->selectedVectorItems();
    if (selected.isEmpty())
        return;

    beginEdit();
    for (VectorItem *item : selected) {
        if (item->object().type == ShapeType::Text)
            item->setFillColor(color);
        else {
            item->setFillColor(color);
            if (!item->object().hasStroke)
                item->setStrokeWidth(m_view->strokeWidth());
        }
    }
    endEdit();
}

void MainWindow::onSelectionChanged(int count)
{
    if (m_selectionLabel)
        m_selectionLabel->setText(QStringLiteral("Выделено: %1").arg(count));
    refreshQuickTransform();
    syncShapeOptions();
}

void MainWindow::syncShapeOptions()
{
    if (!m_view || !m_rectRadius)
        return;

    m_updatingShapeOptions = true;

    m_rectRadius->setValue(m_view->cornerRadius());
    m_polygonSides->setValue(m_view->polygonSides());
    m_starPoints->setValue(m_view->starPoints());
    m_starRatio->setValue(qRound(m_view->starRatio() * 100.0));
    m_spiralTurns->setValue(m_view->spiralTurns());

    for (VectorItem *item : m_view->selectedVectorItems()) {
        const VectorObject object = item->object();
        switch (object.type) {
        case ShapeType::Rectangle:
            m_rectRadius->setValue(object.cornerRadius);
            break;
        case ShapeType::Polygon:
            m_polygonSides->setValue(object.sides);
            break;
        case ShapeType::Star:
            m_starPoints->setValue(object.starPoints);
            m_starRatio->setValue(qRound(object.starRatio * 100.0));
            break;
        case ShapeType::Spiral:
            m_spiralTurns->setValue(object.spiralTurns);
            break;
        default:
            break;
        }
    }

    m_updatingShapeOptions = false;
}

void MainWindow::applyShapeOptions()
{
    if (m_updatingShapeOptions || !m_view)
        return;

    m_view->setCornerRadius(m_rectRadius->value());
    m_view->setPolygonSides(qRound(m_polygonSides->value()));
    m_view->setStarPoints(qRound(m_starPoints->value()));
    m_view->setStarRatio(m_starRatio->value() / 100.0);
    m_view->setSpiralTurns(m_spiralTurns->value());

    const QList<VectorItem *> selected = m_view->selectedVectorItems();
    if (selected.isEmpty())
        return;

    beginEdit();
    int changed = 0;
    for (VectorItem *item : selected) {
        switch (item->object().type) {
        case ShapeType::Rectangle:
            item->setCornerRadius(m_rectRadius->value());
            ++changed;
            break;
        case ShapeType::Polygon:
            item->setSides(qRound(m_polygonSides->value()));
            ++changed;
            break;
        case ShapeType::Star:
            item->setStarPoints(qRound(m_starPoints->value()));
            item->setStarRatio(m_starRatio->value() / 100.0);
            ++changed;
            break;
        case ShapeType::Spiral:
            item->setSpiralTurns(m_spiralTurns->value());
            ++changed;
            break;
        default:
            break;
        }
    }
    endEdit();

    if (changed > 0)
        statusBar()->showMessage(
            QStringLiteral("Параметры фигуры обновлены: %1").arg(changed), 3000);
}

void MainWindow::refreshQuickTransform()
{
    if (!m_quickX || !m_view)
        return;

    const QList<VectorItem *> selected = m_view->selectedVectorItems();
    const QSignalBlocker blockX(m_quickX);
    const QSignalBlocker blockY(m_quickY);
    const QSignalBlocker blockW(m_quickW);
    const QSignalBlocker blockH(m_quickH);
    const QSignalBlocker blockAngle(m_quickAngle);

    const bool enabled = !selected.isEmpty();
    const bool single = selected.size() == 1;
    m_quickX->setEnabled(enabled);
    m_quickY->setEnabled(enabled);
    m_quickW->setEnabled(single);
    m_quickH->setEnabled(single);
    m_quickAngle->setEnabled(single);

    if (!enabled)
        return;

    if (single) {
        const VectorObject object = selected.first()->object();
        const QRectF bounds = object.localBounds();
        m_quickX->setValue(object.pos.x());
        m_quickY->setValue(object.pos.y());
        m_quickW->setValue(bounds.width());
        m_quickH->setValue(bounds.height());
        m_quickAngle->setValue(object.rotation);
    } else {
        QRectF groupBounds;
        for (VectorItem *item : selected) {
            const QRectF visual = item->mapRectToScene(item->localBounds());
            groupBounds = groupBounds.isNull()
                              ? visual
                              : groupBounds.united(visual);
        }
        m_quickX->setValue(groupBounds.x());
        m_quickY->setValue(groupBounds.y());
        m_quickW->setValue(groupBounds.width());
        m_quickH->setValue(groupBounds.height());
        m_quickAngle->setValue(selected.first()->object().rotation);
    }
}

void MainWindow::applyQuickTransform()
{
    if (!m_view || !m_quickX || !m_quickX->isEnabled())
        return;

    const QList<VectorItem *> selected = m_view->selectedVectorItems();
    if (selected.isEmpty())
        return;

    const QPointF position(m_quickX->value(), m_quickY->value());
    const QSizeF size(m_quickW->value(), m_quickH->value());
    const qreal angle = m_quickAngle->value();

    beginEdit();
    if (selected.size() == 1) {
        VectorItem *item = selected.first();
        item->setFrameSize(size);
        item->setRotationAngle(angle);
        item->setPos(position);
    } else {
        const QPointF delta = position - selected.first()->pos();
        for (VectorItem *item : selected)
            item->setPos(item->pos() + delta);
    }
    endEdit();
}

void MainWindow::copySelection()
{
    const QList<VectorItem *> selected = m_view->selectedVectorItems();
    if (selected.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Нет выделенных объектов."), 3000);
        return;
    }

    m_clipboard.clear();
    for (VectorItem *item : selected)
        m_clipboard.append(item->object());
    statusBar()->showMessage(
        QStringLiteral("Скопировано объектов: %1").arg(m_clipboard.size()), 3000);
}

void MainWindow::cutSelection()
{
    copySelection();
    if (m_clipboard.isEmpty())
        return;
    m_view->deleteSelectedObjects();
    statusBar()->showMessage(
        QStringLiteral("Вырезано объектов: %1").arg(m_clipboard.size()), 3000);
}

void MainWindow::pasteClipboard()
{
    if (m_clipboard.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Буфер обмена пуст."), 3000);
        return;
    }

    beginEdit();
    m_view->scene()->clearSelection();
    for (VectorObject object : m_clipboard) {
        object.layer = m_view->activeLayer();
        object.group = 0;
        object.id = 0;
        object.masterId = 0;
        VectorItem *item = m_view->addVectorObject(object, false, false);
        item->setSelected(true);
    }
    endEdit();
    statusBar()->showMessage(
        QStringLiteral("Вставлено объектов: %1").arg(m_clipboard.size()), 3000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    clearAutosave();
    event->accept();
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
        "<tr><td><b>C</b></td><td>Обрезка</td><td><b>K</b></td><td>Нож</td></tr>"
        "<tr><td><b>X</b></td><td>Ластик</td><td><b>Пробел</b></td><td>Панорама (удерживать)</td></tr>"
        "<tr><td><b>S</b></td><td>Создание фигур</td><td><b>Shift+E</b></td><td>Конверт (двойной клик — сброс)</td></tr>"
        "</table>"
        "<h3 style='color:#e3e7ee'>Кривая Безье</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>Клик</b></td><td>добавить узел</td>"
        "<td><b>Протяжка</b></td><td>изогнуть сегмент</td></tr>"
        "<tr><td><b>Alt</b></td><td>излом (независимые направляющие)</td>"
        "<td><b>Ctrl</b></td><td>шаг направляющей 15°</td></tr>"
        "<tr><td><b>Backspace</b></td><td>удалить последний узел</td>"
        "<td><b>Клик по первому узлу</b></td><td>замкнуть кривую</td></tr>"
        "<tr><td><b>Enter</b> / двойной клик</td><td>завершить</td>"
        "<td><b>Esc</b></td><td>отменить кривую</td></tr>"
        "<tr><td><b>Ctrl+Q</b></td><td>текст в кривые</td>"
        "<td><b>Ctrl+Enter</b></td><td>применить текст на холсте</td></tr>"
        "</table>"
        "<h3 style='color:#e3e7ee'>Файл и правка</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>Ctrl+N</b></td><td>Создать</td><td><b>Ctrl+O</b></td><td>Открыть</td></tr>"
        "<tr><td><b>Ctrl+S</b></td><td>Сохранить</td><td><b>Ctrl+Shift+S</b></td><td>Сохранить как</td></tr>"
        "<tr><td><b>Ctrl+I</b></td><td>Импорт</td><td><b>Ctrl+E</b></td><td>Экспорт</td></tr>"
        "<tr><td><b>Ctrl+Z</b></td><td>Отменить</td><td><b>Ctrl+Shift+Z</b></td><td>Повторить</td></tr>"
        "<tr><td><b>Ctrl+D</b></td><td>Дублировать</td><td><b>Ctrl+Shift+D</b></td><td>Шаг и повтор</td></tr>"
        "<tr><td><b>Ctrl+G</b></td><td>Сгруппировать</td><td><b>Ctrl+U</b></td><td>Разгруппировать</td></tr>"
        "<tr><td><b>Ctrl+Shift+U</b></td><td>Разгруппировать всё</td><td><b>Del</b></td><td>Удалить</td></tr>"
        "</table>"
        "<h3 style='color:#e3e7ee'>Выделение и группы</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>Alt+клик</b></td><td>объект под верхним</td>"
        "<td><b>Ctrl+клик</b></td><td>объект внутри группы</td></tr>"
        "<tr><td><b>Shift+клик</b></td><td>несколько узлов (Форма)</td>"
        "<td><b>Двойной клик</b></td><td>добавить узел на сегменте</td></tr>"
        "<tr><td><b>Tab</b></td><td>следующий объект</td>"
        "<td><b>Ctrl+перетаскивание</b></td><td>центр вращения</td></tr>"
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
            "<h3>Vectora 0.2.4</h3>"
            "<p>Кроссплатформенный векторный редактор на C++ и Qt 6.</p>"
            "<p>Логические операции, нож, ластик, Shape Builder, контур и "
            "конверт; градиенты всех типов, цветовые пространства и палитры; "
            "текст по контуру и в фигуре, OpenType и Variable Fonts; "
            "многостраничные документы с мастер-страницами; экспорт всех "
            "страниц в PDF/PNG и печать.</p>"
            "<p style='color:#666'>Linux · Windows · macOS</p>"));
}
