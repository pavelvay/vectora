#include <QApplication>

#include <cstdio>
#include <QDockWidget>
#include <QTabWidget>
#include <QTimer>

#include <cstdlib>

#include "CodeGenerator.h"
#include "DocumentDialogs.h"
#include "UpdateChecker.h"
#include "Effects.h"
#include "PatternStore.h"
#include "Patterns.h"

#include "CanvasView.h"
#include "Icons.h"
#include "MainWindow.h"
#include "VectorItem.h"
#include "SelfTest.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Vectora"));
    app.setApplicationDisplayName(QStringLiteral("Vectora"));
    app.setOrganizationName(QStringLiteral("Vectora"));
    app.setApplicationVersion(QStringLiteral("0.2.4"));
    app.setDesktopFileName(QStringLiteral("vectora"));
    app.setWindowIcon(Icons::appIcon());

    Theme::apply(app);

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--check-updates-cli"))) {
        auto *checker = new UpdateChecker(&app);
        QObject::connect(checker, &UpdateChecker::upToDate, &app,
                         [](const QString &version) {
                             std::fprintf(stdout, "UP_TO_DATE %s\n",
                                          qPrintable(version));
                             std::fflush(stdout);
                             std::_Exit(0);
                         });
        QObject::connect(checker, &UpdateChecker::updateAvailable, &app,
                         [](const UpdateInfo &info) {
                             std::fprintf(stdout, "UPDATE %s assets=%d\n",
                                          qPrintable(info.version),
                                          int(info.assets.size()));
                             std::fflush(stdout);
                             std::_Exit(0);
                         });
        QObject::connect(checker, &UpdateChecker::failed, &app,
                         [](const QString &error) {
                             std::fprintf(stdout, "ERROR %s\n", qPrintable(error));
                             std::fflush(stdout);
                             std::_Exit(0);
                         });
        QTimer::singleShot(25000, &app, []() {
            std::fprintf(stdout, "TIMEOUT\n");
            std::fflush(stdout);
            std::_Exit(0);
        });
        checker->check(false);
        return app.exec();
    }

    if (args.contains(QStringLiteral("--update-demo"))) {
        UpdateInfo info;
        info.version = QStringLiteral("0.3.0");
        info.notes = QStringLiteral(
            "Новая версия Vectora: улучшены эффекты, добавлены шаблоны и "
            "проверка обновлений.");
        info.pageUrl = QUrl(QStringLiteral("https://github.com/pavelvay/vectora/releases"));
        info.assets.append({QStringLiteral("Vectora-0.3.0-x86_64.AppImage"),
                            QUrl(QStringLiteral("https://example.com/Vectora.AppImage")),
                            87000000});
        info.assets.append({QStringLiteral("Vectora-0.3.0-Setup.exe"),
                            QUrl(QStringLiteral("https://example.com/Vectora.exe")),
                            75000000});
        auto *dialog = new UpdateDialog(info);
        dialog->show();
        QTimer::singleShot(800, dialog, [dialog]() {
            dialog->grab().save(QStringLiteral("/tmp/opencode/update-demo.png"));
            std::_Exit(0);
        });
        return app.exec();
    }

    if (args.contains(QStringLiteral("--templates-demo"))) {
        auto *dialog = new TemplateDialog(Document::sample().toJson());
        dialog->resize(1000, 680);
        dialog->show();
        QTimer::singleShot(900, dialog, [dialog]() {
            dialog->grab().save(QStringLiteral("/tmp/opencode/templates-demo.png"));
            std::_Exit(0);
        });
        return app.exec();
    }

    const int selfTestIndex = args.indexOf(QStringLiteral("--self-test"));
    if (selfTestIndex > 0 && selfTestIndex + 1 < args.size())
        return SelfTest::run(args.at(selfTestIndex + 1));

    MainWindow window;
    window.show();

    // Development helper: open a document passed on the command line.
    const int openIndex = args.indexOf(QStringLiteral("--open"));
    if (openIndex > 0 && openIndex + 1 < args.size())
        window.openPath(args.at(openIndex + 1));

    // Development helper: raise a docker by its title.
    const int dockerIndex = args.indexOf(QStringLiteral("--docker"));
    if (dockerIndex > 0 && dockerIndex + 1 < args.size()) {
        const QString title = args.at(dockerIndex + 1);
        for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
            if (dock->windowTitle() == title) {
                dock->show();
                dock->raise();
                break;
            }
        }
    }

    // Development helper: switch to the given page.
    const int pageIndex = args.indexOf(QStringLiteral("--page"));
    if (pageIndex > 0 && pageIndex + 1 < args.size()) {
        if (auto *canvas = window.findChild<CanvasView *>())
            canvas->setCurrentPage(args.at(pageIndex + 1).toInt());
    }

    // Development helper: select the n-th object (bottom to top).
    const int selectIndex = args.indexOf(QStringLiteral("--select"));
    if (selectIndex > 0 && selectIndex + 1 < args.size()) {
        auto *canvas = window.findChild<CanvasView *>();
        if (canvas) {
            const QList<VectorItem *> items = canvas->vectorItems();
            // Accept a single index or a comma separated list ("3,5").
            const QStringList parts = args.at(selectIndex + 1).split(QLatin1Char(','));
            for (const QString &part : parts) {
                const int index = part.trimmed().toInt();
                if (index >= 0 && index < items.size())
                    items.at(index)->setSelected(true);
            }
        }
    }

    // Development helper: turn the selection into a symbol and show the panel.
    if (args.contains(QStringLiteral("--symbol"))) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            canvas->createSymbolFromSelection(QStringLiteral("Символ"));
            for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                if (dock->windowTitle() != QStringLiteral("Библиотека"))
                    continue;
                dock->show();
                dock->raise();
                if (auto *tabs = dock->findChild<QTabWidget *>())
                    tabs->setCurrentIndex(1);
                break;
            }
        }
    }

    // Development helper: place a QR code on the page.
    if (args.contains(QStringLiteral("--code"))) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            const CodeSymbol symbol = generateCode(
                CodeKind::Qr, QStringLiteral("https://github.com/pavelvay/vectora"), 2);
            if (symbol.valid) {
                VectorObject object;
                object.type = ShapeType::Code;
                object.codeKind = static_cast<int>(CodeKind::Qr);
                object.codeData = QStringLiteral("https://github.com/pavelvay/vectora");
                object.codeEcc = 2;
                object.codeMargin = 4;
                object.codeColor = QColor("#102030");
                object.codeBackground = QColor("#ffffff");
                object.codeModulesX = symbol.modulesX;
                object.codeModulesY = symbol.modulesY;
                object.codeCells = symbol.cells;
                object.textBox = QRectF(0, 0, 220, 220);
                const QSizeF page = canvas->pageSize();
                object.pos = QPointF((page.width() - 220) / 2.0,
                                     (page.height() - 220) / 2.0);
                canvas->addCodeObject(object);
            }
        }
    }

    // Development helper: place an EAN-13 barcode on the page.
    if (args.contains(QStringLiteral("--barcode"))) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            const CodeSymbol symbol = generateCode(CodeKind::Ean13,
                                                   QStringLiteral("590123412345"));
            if (symbol.valid) {
                VectorObject object;
                object.type = ShapeType::Code;
                object.codeKind = static_cast<int>(CodeKind::Ean13);
                object.codeData = symbol.data;
                object.codeMargin = 9;
                object.codeColor = QColor("#101010");
                object.codeBackground = QColor("#ffffff");
                object.codeShowText = true;
                object.codeHumanText = symbol.humanText;
                object.codeModulesX = symbol.modulesX;
                object.codeModulesY = symbol.modulesY;
                object.codeCells = symbol.cells;
                object.font = QFont(QStringLiteral("Adwaita Sans"));
                object.textBox = QRectF(0, 0, 340, 130);
                const QSizeF page = canvas->pageSize();
                object.pos = QPointF((page.width() - 340) / 2.0,
                                     page.height() - 220);
                canvas->addCodeObject(object);
            }
        }
    }

    // Development helper: create a pattern from the selection and apply it to
    // a fresh rectangle, then show the pattern library tab.
    if (args.contains(QStringLiteral("--pattern"))) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            const QList<VectorItem *> selected = canvas->selectedVectorItems();
            if (!selected.isEmpty()) {
                const int patternId =
                    canvas->createPatternFromSelection(QStringLiteral("Узор"));
                PatternDefinition definition = canvas->patternDefinition(patternId);
                definition.settings.tile = QSizeF(54, 54);
                definition.settings.spacingX = 6.0;
                definition.settings.spacingY = 6.0;
                definition.settings.scale = 0.75;
                canvas->storePatternDefinition(definition);
                PatternDefinition libraryPattern = canvas->patternDefinition(patternId);
                libraryPattern.id = 0;
                PatternStore::instance().load();
                PatternStore::instance().add(libraryPattern);
                const QSizeF page = canvas->pageSize();
                VectorObject target;
                target.type = ShapeType::Rectangle;
                target.path.addRect(QRectF(0, 0, 260, 180));
                target.pos = QPointF(page.width() / 2.0 - 130.0,
                                     page.height() / 2.0 - 40.0);
                target.fill = QColor("#ffffff");
                target.stroke = QColor("#1d2127");
                target.strokeWidth = 1.5;
                target.page = canvas->currentPage();
                target.layer = canvas->activeLayer();
                VectorItem *item = canvas->addVectorObject(target, false, false);
                canvas->scene()->clearSelection();
                item->setSelected(true);
                canvas->applyPatternToSelection(patternId);
                for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                    if (dock->windowTitle() != QStringLiteral("Библиотека"))
                        continue;
                    dock->show();
                    dock->raise();
                    if (auto *tabs = dock->findChild<QTabWidget *>())
                        tabs->setCurrentIndex(2);
                    break;
                }
            }
        }
    }

    // Development helper: attach a horizontal dimension to the first object.
    if (args.contains(QStringLiteral("--dimension"))) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            const QList<VectorItem *> items = canvas->vectorItems();
            if (!items.isEmpty()) {
                const QRectF bounds = items.first()->mapRectToScene(
                    items.first()->localBounds());
                VectorObject dimension;
                dimension.type = ShapeType::Dimension;
                dimension.dimensionType = 0;
                dimension.dimA = bounds.topLeft();
                dimension.dimB = bounds.topRight();
                dimension.dimOffset = 30.0;
                dimension.dimUnits = 0;
                dimension.dimPrecision = 1;
                dimension.dimScale = canvas->drawingScale();
                dimension.stroke = QColor("#1d2127");
                dimension.fill = QColor("#1d2127");
                dimension.strokeWidth = 1.0;
                dimension.hasStroke = true;
                dimension.hasFill = false;
                dimension.font = QFont(QStringLiteral("Adwaita Sans"));
                dimension.font.setPixelSize(11);
                dimension.page = canvas->currentPage();
                dimension.layer = canvas->activeLayer();
                canvas->addVectorObject(dimension, true, true);
            }
        }
    }

    // Development helper: switch the drafting grid.
    const int gridIndex = args.indexOf(QStringLiteral("--grid"));
    if (gridIndex > 0 && gridIndex + 1 < args.size()) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            const QString kind = args.at(gridIndex + 1);
            canvas->setGridType(kind == QLatin1String("polar") ? 1
                                : (kind == QLatin1String("iso") ? 2 : 0));
            canvas->setShowGrid(true);
        }
    }

    // Development helper: blend the selected objects and show the blend tab.
    const int blendIndex = args.indexOf(QStringLiteral("--blend"));
    if (blendIndex > 0) {
        int steps = 7;
        if (blendIndex + 1 < args.size() && !args.at(blendIndex + 1).startsWith(QLatin1Char('-')))
            steps = args.at(blendIndex + 1).toInt();
        if (auto *canvas = window.findChild<CanvasView *>()) {
            canvas->blendSelected(steps);
            for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                if (dock->windowTitle() != QStringLiteral("Эффекты"))
                    continue;
                dock->show();
                dock->raise();
                if (auto *tabs = dock->findChild<QTabWidget *>())
                    tabs->setCurrentIndex(2);
                break;
            }
        }
    }

    // Development helper: add an effect to the selected object and show the
    // effect stack tab, so the panel can be inspected without manual clicking.
    const int effectIndex = args.indexOf(QStringLiteral("--effect"));
    if (effectIndex > 0 && effectIndex + 1 < args.size()) {
        if (auto *canvas = window.findChild<CanvasView *>()) {
            canvas->addEffectToSelection(makeEffect(args.at(effectIndex + 1)));
            for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                if (dock->windowTitle() != QStringLiteral("Эффекты"))
                    continue;
                dock->show();
                dock->raise();
                if (auto *tabs = dock->findChild<QTabWidget *>())
                    tabs->setCurrentIndex(3);
                break;
            }
        }
    }

    QApplication::processEvents();

    // Development help: render the window to a PNG and exit, optionally
    // starting with a specific tool selected.
    const int toolIndex = args.indexOf(QStringLiteral("--tool"));
    if (toolIndex > 0 && toolIndex + 1 < args.size()) {
        const QString id = args.at(toolIndex + 1);
        for (const ToolInfo &info : toolInfos()) {
            if (info.id == id)
                window.setActiveTool(info.tool);
        }
    }

    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex > 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        QTimer::singleShot(1200, &window, [&window, path]() {
            window.grab().save(path);
            // The screenshot is a development aid; exit without running the
            // "save changes" prompt that a modified document would raise.
            std::exit(0);
        });
    }

    return app.exec();
}
