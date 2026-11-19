#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class Document;

// Reads CorelDRAW documents (`.cdr`). libcdr is used when the app was built
// with it; otherwise the importer falls back to cdr2xhtml, Inkscape or
// LibreOffice. CDR is a read-only format: there is no open-source writer.
namespace CdrIO {

bool isAvailable();
bool isCdrFile(const QString &path);
bool importFile(const QString &path, Document *document, QString *error);

// Shared with the CMX reader: turns SVG pages produced by libcdr into a
// document whose pages keep their real page sizes.
bool importSvgPages(const QVector<QByteArray> &pages, Document *document,
                    QString *error,
                    const QString &layerName = QStringLiteral("Импорт CDR"));

} // namespace CdrIO
