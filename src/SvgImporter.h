#pragma once

#include <QByteArray>
#include <QString>

class Document;

// Imports an SVG as editable Vectora objects. Returns false and fills error
// when the document cannot be parsed.
bool importSvgFile(const QString &path, Document *document, QString *error);

// Same as importSvgFile, but reads the markup from memory. Used by the asset
// library, which stores resources as SVG fragments.
bool importSvgData(const QByteArray &data, Document *document, QString *error,
                   bool allowEmpty = false);
