#pragma once

#include <QString>

class Document;

// Imports an SVG as editable Vectora objects. Returns false and fills error
// when the document cannot be parsed.
bool importSvgFile(const QString &path, Document *document, QString *error);
