#pragma once

#include <QString>

class Document;

// Reads and writes the JSON document format used by the VectorCraft editor
// (`.vectorcraft`): artboards, layers, paths, compounds, groups, text, raster
// images and appearance (fill/stroke). Geometry inside the file is stored in
// PostScript points; Vectora converts it to 96 dpi pixels on import and back
// on export.
namespace VectorCraftIO {

bool isVectorCraftFile(const QString &path);
bool isVectorCraftData(const QByteArray &data);

bool importFile(const QString &path, Document *document, QString *error);
bool exportFile(const Document &document, const QString &path, QString *error);

} // namespace VectorCraftIO
