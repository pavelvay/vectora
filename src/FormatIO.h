#pragma once

#include <QByteArray>
#include <QString>

class Document;

// Import and export for the interchange formats Vectora supports besides its
// own native JSON documents: PDF, Adobe Illustrator (AI), EPS/ESP and the
// `.vectorcraft` document alias.
namespace VectorFormats {

enum class Format {
    Unknown,
    Vectora,      // .vct — native JSON document
    Vectorcraft,  // .vectorcraft — same JSON schema as .vct
    Pdf,          // .pdf
    Eps,          // .eps / .esp — PostScript
    Ai,           // .ai — Adobe Illustrator (PDF-compatible or EPS-based)
    Cdr,          // .cdr — CorelDRAW (read-only)
    Cmx,          // .cmx — Corel Metafile Exchange
    Svg           // .svg — editable vector import
};

Format formatForPath(const QString &path);
QString formatName(Format format);
QString extensionFor(Format format);

bool isDocumentFormat(Format format);   // native JSON (vct / vectorcraft)
bool canImport(Format format);          // anything we can open
bool canExport(Format format);          // anything we can write

QString openFilter();
QString importFilter();
QString exportFilter();
QString saveFilter();

// Reads `path` into `document`, choosing the reader by extension.
bool importDocument(const QString &path, Document *document, QString *error);

// Writes `document` to `path`, choosing the writer by extension.
bool exportDocument(const Document &document, const QString &path, QString *error);

// Format-specific writers, also used by the file menu and the self-tests.
bool writePdf(const Document &document, const QString &path, QString *error);
bool writeEps(const Document &document, const QString &path, QString *error);
bool writeAi(const Document &document, const QString &path, QString *error);

// Serialised EPS as text, without touching the file system.
QByteArray epsData(const Document &document);

} // namespace VectorFormats
