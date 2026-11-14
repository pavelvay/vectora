#pragma once

#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

#include "Document.h"

// Names used by the editor and menus.
QStringList patternTypeNames();
QString patternTypeName(int type);

// Captures the selection into a reusable pattern definition. Objects are stored
// in their original coordinates; `bounds` is the captured tile box.
PatternDefinition patternFromObjects(const QVector<VectorObject> &objects,
                                     const QString &name);

// Scales the captured artwork into the repeat tile and bakes it into the flat
// shape list stored on a filled object.
QVector<PatternShape> bakePatternShapes(const PatternDefinition &definition);

// One transform per repeated tile, mapping tile-local coordinates into the
// target area. Shared by the painter, the preview and the expansion command.
QVector<QTransform> patternTileTransforms(const PatternSettings &settings,
                                          const QRectF &area);

// Paints the repeated tile artwork. The caller is responsible for clipping.
void paintPatternFill(QPainter *painter, const PatternSettings &settings,
                      const QVector<PatternShape> &shapes, const QRectF &area);

// Converts the repeated artwork into independent vector objects clipped to the
// given shape. Used by "convert pattern to objects".
QVector<VectorObject> expandPatternObject(const VectorObject &source);

// Renders a tile of the pattern for previews.
QImage renderPatternPreview(const PatternSettings &settings,
                            const QVector<PatternShape> &shapes,
                            const QSize &size);

// Estimates whether the tile joins itself without a visible seam. The returned
// value is the mean colour difference of opposite edges (0 = identical).
bool patternIsSeamless(const PatternSettings &settings,
                       const QVector<PatternShape> &shapes,
                       qreal *difference = nullptr);

// Serialisation shared with the document and the pattern library.
QJsonObject patternSettingsToJson(const PatternSettings &settings);
PatternSettings patternSettingsFromJson(const QJsonObject &entry);
QJsonObject patternDefinitionToJson(const PatternDefinition &definition);
PatternDefinition patternDefinitionFromJson(const QJsonObject &entry);
