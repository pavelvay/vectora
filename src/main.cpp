#include <QApplication>
#include <QTimer>

#include "Icons.h"
#include "MainWindow.h"
#include "SelfTest.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Vectora"));
    app.setApplicationDisplayName(QStringLiteral("Vectora"));
    app.setOrganizationName(QStringLiteral("Vectora"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));
    app.setDesktopFileName(QStringLiteral("vectora"));
    app.setWindowIcon(Icons::appIcon());

    Theme::apply(app);

    const QStringList args = app.arguments();

    const int selfTestIndex = args.indexOf(QStringLiteral("--self-test"));
    if (selfTestIndex > 0 && selfTestIndex + 1 < args.size())
        return SelfTest::run(args.at(selfTestIndex + 1));

    MainWindow window;
    window.show();

    // Development helper: open a document passed on the command line.
    const int openIndex = args.indexOf(QStringLiteral("--open"));
    if (openIndex > 0 && openIndex + 1 < args.size())
        window.openPath(args.at(openIndex + 1));

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
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
