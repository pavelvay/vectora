#pragma once

#include <QString>

class Document;

// Reads Corel Metafile Exchange (CMX) documents through libcdr or the
// external fallbacks. Writing is not exposed yet: CorelDRAW rejects the
// generated 32-bit CMX 1 files, so exportFile() reports "work in progress".
// CMX is an exchange format, not CorelDRAW's native .cdr container.
namespace CmxIO {

bool exportFile(const Document &document, const QString &path, QString *error);
bool exportFile32(const Document &document, const QString &path, QString *error);
bool importFile(const QString &path, Document *document, QString *error);

} // namespace CmxIO
