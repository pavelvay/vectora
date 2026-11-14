#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
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
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QSettings>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
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
#include "DockPanels.h"
#include "Icons.h"
#include "Ruler.h"
#include "SvgImporter.h"
#include "Theme.h"
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
               QKeySequence(QStringLiteral("Ctrl+2")), QStringLiteral("Приблизить выделенные объекты"), false);
    makeAction(QStringLiteral("rulers"), QStringLiteral("Линейки"),
               QKeySequence(QStringLiteral("Ctrl+R")), QStringLiteral("Показать или скрыть линейки"));
    makeAction(QStringLiteral("guides"), QStringLiteral("Направляющие"),
               QKeySequence(), QStringLiteral("Показать или скрыть направляющие"));
    makeAction(QStringLiteral("clear-guides"), QStringLiteral("Удалить направляющие"),
               QKeySequence(), QStringLiteral("Убрать все направляющие с холста"), false);
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
            [this]() { m_view->convertTextToCurves(); });
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
    connect(action(QStringLiteral("fx-shadow")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 0); });
    connect(action(QStringLiteral("fx-transparency")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 1); });
    connect(action(QStringLiteral("fx-blend")), &QAction::triggered, this,
            [this]() { showDockerTab(QStringLiteral("Эффекты"), 2); });
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

    connect(action(QStringLiteral("clear-guides")), &QAction::triggered, this,
            [this]() { m_view->clearGuides(); });
    connect(action(QStringLiteral("snap-settings")), &QAction::triggered, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Сетка и привязка"));
        auto *form = new QFormLayout(&dialog);
        auto *step = new QSpinBox(&dialog);
        step->setRange(2, 400);
        step->setValue(m_view->gridStep());
        step->setSuffix(QStringLiteral(" px"));
        auto *threshold = new QDoubleSpinBox(&dialog);
        threshold->setRange(1, 60);
        threshold->setValue(m_view->snapThreshold());
        threshold->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Шаг сетки"), step);
        form->addRow(QStringLiteral("Чувствительность привязки"), threshold);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                 | QDialogButtonBox::Cancel,
                                             &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_view->setGridStep(step->value());
        m_view->setSnapThreshold(threshold->value());
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
    editMenu->addAction(action(QStringLiteral("step-repeat")));
    editMenu->addAction(action(QStringLiteral("clone")));
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
    viewMenu->addAction(action(QStringLiteral("clear-guides")));
    viewMenu->addAction(action(QStringLiteral("grid")));
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
    textMenu->addSeparator();
    textMenu->addAction(action(QStringLiteral("convert-curves")));

    auto *windowMenu = menuBar()->addMenu(QStringLiteral("Окно"));
    windowMenu->addAction(m_toolsDock->toggleViewAction());
    windowMenu->addAction(m_paletteDock->toggleViewAction());
    auto *dockersMenu = windowMenu->addMenu(QStringLiteral("Докеры"));
    if (m_dockPanels) {
        for (QDockWidget *dock : m_dockPanels->docks())
            dockersMenu->addAction(dock->toggleViewAction());
    }
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
    m_centralStack = new QStackedWidget(this);
    m_editorPage = new QWidget(m_centralStack);
    auto *grid = new QGridLayout(m_editorPage);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    auto *corner = new QLabel(QStringLiteral("мм"), m_editorPage);
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
    m_toolsDock->setWidget(m_toolPalette);
    m_toolsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_toolsDock->setTitleBarWidget(new QWidget(m_toolsDock));
    m_toolsDock->setFixedWidth(54);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);
    connect(m_toolPalette, &ToolPalette::toolSelected, this, &MainWindow::applyTool);
    connect(m_toolPalette, &ToolPalette::toolDoubleClicked, this, [this](Tool tool) {
        if (tool == Tool::Select) {
            m_view->selectAllObjects();
            statusBar()->showMessage(
                QStringLiteral("Двойной клик по «Выделению»: выбраны все объекты."), 3000);
        }
    });

    // Right dockers, tabbed like CorelDRAW's docker group.
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

    auto *pageBadge = new QLabel(QStringLiteral("A4  ·  210 × 297 мм"), bar);
    pageBadge->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px; padding-right:8px;"));
    bar->addWidget(pageBadge);
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
    statusBar()->show();
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
        page = m_paintPageIndex;
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
    document.layers = m_view->layers();
    document.groups = m_view->groups();
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
    applyDocument(Document());
    m_filePath.clear();
    m_modified = false;
    resetHistory();
    updateTitle();
    showEditor();
    m_view->fitPage();
    statusBar()->showMessage(QStringLiteral("Создан новый документ A4."), 4000);
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
    resetHistory();
    updateTitle();
    addRecentFile(path);
    showEditor();
    m_view->fitPage();
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

    const Document document = currentDocument();
    const QSizeF millimeters(document.pageSize.width() * 25.4 / 96.0,
                             document.pageSize.height() * 25.4 / 96.0);
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(millimeters, QPageSize::Millimeter,
                                 QStringLiteral("Vectora")));
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    const qreal scaleX = writer.width() / document.pageSize.width();
    const qreal scaleY = writer.height() / document.pageSize.height();
    painter.scale(scaleX, scaleY);
    document.render(&painter);
    painter.end();

    statusBar()->showMessage(
        QStringLiteral("PDF сохранён: %1").arg(QFileInfo(path).fileName()), 5000);
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

    const QRectF page = printer.pageRect(QPrinter::DevicePixel);
    const qreal scale = qMin(page.width() / document.pageSize.width(),
                             page.height() / document.pageSize.height());
    painter.translate(page.topLeft());
    painter.scale(scale, scale);
    document.render(&painter);
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

    m_undoStack.append(m_pendingSnapshot);
    if (m_undoStack.size() > 100)
        m_undoStack.removeFirst();
    m_redoStack.clear();

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
    m_redoStack.append(currentDocument().toJson());

    QString error;
    const Document document = Document::fromJson(previous, &error);
    m_restoring = true;
    m_view->setDocument(document);
    m_restoring = false;

    m_modified = true;
    updateTitle();
    if (m_dockPanels)
        m_dockPanels->refresh();
    updateHistoryActions();
    statusBar()->showMessage(QStringLiteral("Действие отменено."), 2500);
}

void MainWindow::redo()
{
    if (m_redoStack.isEmpty())
        return;

    const QJsonObject next = m_redoStack.takeLast();
    m_undoStack.append(currentDocument().toJson());

    QString error;
    const Document document = Document::fromJson(next, &error);
    m_restoring = true;
    m_view->setDocument(document);
    m_restoring = false;

    m_modified = true;
    updateTitle();
    if (m_dockPanels)
        m_dockPanels->refresh();
    updateHistoryActions();
    statusBar()->showMessage(QStringLiteral("Действие повторено."), 2500);
}

void MainWindow::resetHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
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
    if (maybeSave())
        event->accept();
    else
        event->ignore();
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
            "<h3>Vectora 0.1.9</h3>"
            "<p>Кроссплатформенный векторный редактор на C++ и Qt 6.</p>"
            "<p>Работают: документы .vct, экспорт PNG/SVG/PDF, печать, "
            "рисование фигур и кривых, Bezier с типами узлов, докеры "
            "свойств, цвета, выравнивания, эффектов и текста, слои, Undo/Redo.</p>"
            "<p style='color:#666'>Linux · Windows · macOS</p>"));
}
