#include <QApplication>
#include <QTimer>

#include "Icons.h"
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Vectora"));
    app.setApplicationDisplayName(QStringLiteral("Vectora"));
    app.setOrganizationName(QStringLiteral("Vectora"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setDesktopFileName(QStringLiteral("vectora"));
    app.setWindowIcon(Icons::appIcon());

    Theme::apply(app);

    MainWindow window;
    window.show();

    // Development helpers: render the window to a PNG and exit, optionally
    // starting with a specific tool selected.
    const QStringList args = app.arguments();
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
