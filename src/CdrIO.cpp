#include "CdrIO.h"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "Document.h"
#include "SvgImporter.h"

#if defined(VECTORA_HAVE_LIBCDR)
#include <librevenge/librevenge.h>
#include <librevenge-stream/librevenge-stream.h>
#include <libcdr/libcdr.h>
#endif

namespace CdrIO {
namespace {

QString findTool(const QStringList &names, const QStringList &fallbackPaths = {})
{
    for (const QString &name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    for (const QString &candidate : fallbackPaths) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

QString findCdr2xhtml()
{
    return findTool({QStringLiteral("cdr2xhtml")});
}

QString findInkscape()
{
    QStringList paths;
#if defined(Q_OS_WIN)
    paths << QStringLiteral("C:/Program Files/Inkscape/bin/inkscape.exe")
          << QStringLiteral("C:/Program Files/Inkscape/inkscape.exe")
          << QStringLiteral("C:/Program Files (x86)/Inkscape/bin/inkscape.exe");
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/Applications/Inkscape.app/Contents/MacOS/inkscape");
#endif
    return findTool({QStringLiteral("inkscape"), QStringLiteral("inkscape.exe")}, paths);
}

QString findLibreOffice()
{
    QStringList paths;
#if defined(Q_OS_WIN)
    paths << QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe")
          << QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe");
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS/soffice");
#endif
    return findTool({QStringLiteral("soffice"), QStringLiteral("libreoffice"),
                     QStringLiteral("soffice.exe")}, paths);
}

bool importSvgPagesImpl(const QVector<QByteArray> &pages, Document *document,
                        QString *error, const QString &layerName)
{
    if (pages.isEmpty()) {
        if (error)
            *error = QStringLiteral("CDR не содержит страниц.");
        return false;
    }

    Document result;
    result.layers = {layerName};
    result.layerVisible = {true};
    result.layerLocked = {false};
    result.currentPage = 0;

    for (int index = 0; index < pages.size(); ++index) {
        Document pageDocument;
        QString pageError;
        if (!importSvgData(pages.at(index), &pageDocument, &pageError, true)) {
            if (error)
                *error = QStringLiteral("Не удалось разобрать страницу CDR: %1")
                             .arg(pageError);
            return false;
        }

        PageInfo info;
        info.name = QStringLiteral("Страница %1").arg(index + 1);
        info.size = pageDocument.pageSize;
        if (info.size.width() < 1.0 || info.size.height() < 1.0)
            info.size = QSizeF(794.0, 1123.0);
        info.background = QColor(Qt::white);
        result.pages.append(info);

        for (VectorObject object : pageDocument.objects) {
            object.page = index;
            object.layer = 0;
            object.id = 0;
            object.group = 0;
            object.masterId = 0;
            object.symbolId = 0;
            object.instanceId = 0;
            object.blendId = 0;
            result.objects.append(object);
        }
    }

    result.pageSize = result.pages.first().size;
    *document = result;
    if (error)
        error->clear();
    return true;
}

QVector<QByteArray> splitSvgPages(const QByteArray &data)
{
    QVector<QByteArray> pages;
    const QString text = QString::fromUtf8(data);
    QRegularExpression prefixed(QStringLiteral("<svg:svg[\\s\\S]*?</svg:svg>"));
    auto it = prefixed.globalMatch(text);
    while (it.hasNext())
        pages.append(it.next().captured(0).toUtf8());
    if (!pages.isEmpty())
        return pages;

    QRegularExpression plain(QStringLiteral("<svg[\\s>][\\s\\S]*?</svg>"));
    it = plain.globalMatch(text);
    while (it.hasNext())
        pages.append(it.next().captured(0).toUtf8());
    return pages;
}

#if defined(VECTORA_HAVE_LIBCDR)
bool importWithLibcdr(const QString &path, Document *document, QString *error)
{
    try {
        librevenge::RVNGFileStream input(path.toUtf8().constData());
        if (!libcdr::CDRDocument::isSupported(&input))
            return false;

        librevenge::RVNGStringVector output;
        librevenge::RVNGSVGDrawingGenerator painter(
            output, librevenge::RVNGString("svg"));
        if (!libcdr::CDRDocument::parse(&input, &painter))
            return false;

        QVector<QByteArray> pages;
        pages.reserve(int(output.size()));
        for (unsigned index = 0; index < output.size(); ++index)
            pages.append(QByteArray(output[index].cstr()));
        return importSvgPagesImpl(pages, document, error, QStringLiteral("Импорт CDR"));
    } catch (...) {
        return false;
    }
}
#endif

bool runConverter(const QString &program, const QStringList &arguments,
                  QByteArray *output, int timeout = 120000)
{
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(program, arguments);
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;
    if (output)
        *output = process.readAllStandardOutput();
    return true;
}

bool importWithCdr2xhtml(const QString &path, Document *document, QString *error)
{
    const QString tool = findCdr2xhtml();
    if (tool.isEmpty())
        return false;
    QByteArray output;
    if (!runConverter(tool, {path}, &output))
        return false;
    return importSvgPagesImpl(splitSvgPages(output), document, error, QStringLiteral("Импорт CDR"));
}

bool importWithInkscape(const QString &path, Document *document, QString *error)
{
    const QString tool = findInkscape();
    if (tool.isEmpty())
        return false;
    QTemporaryDir temp;
    if (!temp.isValid())
        return false;
    const QString output = temp.filePath(QStringLiteral("cdr.svg"));
    if (!runConverter(tool, {path, QStringLiteral("--export-filename=%1").arg(output)},
                      nullptr)
        && !runConverter(tool, {path, QStringLiteral("--export-plain-svg=%1").arg(output)},
                         nullptr))
        return false;
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return importSvgPagesImpl({file.readAll()}, document, error, QStringLiteral("Импорт CDR"));
}

bool importWithLibreOffice(const QString &path, Document *document, QString *error)
{
    const QString tool = findLibreOffice();
    if (tool.isEmpty())
        return false;

    QTemporaryDir temp;
    if (!temp.isValid())
        return false;
    const QString profile = temp.filePath(QStringLiteral("profile"));
    const QString outDir = temp.filePath(QStringLiteral("out"));
    QDir().mkpath(outDir);
    if (!runConverter(tool,
                      {QStringLiteral("--headless"),
                       QStringLiteral("-env:UserInstallation=file://%1").arg(profile),
                       QStringLiteral("--convert-to"), QStringLiteral("svg"),
                       QStringLiteral("--outdir"), outDir, path},
                      nullptr, 180000))
        return false;

    const QString expected = outDir + QLatin1Char('/')
                             + QFileInfo(path).completeBaseName()
                             + QStringLiteral(".svg");
    QFile file(expected);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return importSvgPagesImpl({file.readAll()}, document, error, QStringLiteral("Импорт CDR"));
}

} // namespace

bool importSvgPages(const QVector<QByteArray> &pages, Document *document,
                    QString *error, const QString &layerName)
{
    return importSvgPagesImpl(pages, document, error, layerName);
}

bool isAvailable()
{
#if defined(VECTORA_HAVE_LIBCDR)
    return true;
#else
    return !findCdr2xhtml().isEmpty() || !findInkscape().isEmpty()
           || !findLibreOffice().isEmpty();
#endif
}

bool isCdrFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(16);
    if (head.startsWith("RIFF") && head.mid(8, 3) == "CDR")
        return true;
    if (head.startsWith("PK\x03\x04"))
        return true;
    if (head.startsWith("WL"))
        return true;
    return false;
}

bool importFile(const QString &path, Document *document, QString *error)
{
    if (error)
        error->clear();
#if defined(VECTORA_HAVE_LIBCDR)
    if (importWithLibcdr(path, document, error))
        return true;
#endif
    if (importWithCdr2xhtml(path, document, error))
        return true;
    if (importWithInkscape(path, document, error))
        return true;
    if (importWithLibreOffice(path, document, error))
        return true;

    if (error)
        *error = QStringLiteral(
            "Не удалось прочитать CDR. Установите libcdr (пакет с cdr2xhtml), "
            "Inkscape или LibreOffice.");
    return false;
}

} // namespace CdrIO
