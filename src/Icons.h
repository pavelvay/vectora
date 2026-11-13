#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace Icons {

// Draws a crisp 24x24 vector icon from code. `color` overrides the ink colour,
// which is useful for colour swatches.
QIcon icon(const QString &id, const QColor &color = QColor());

// Application / window icon.
QIcon appIcon();

} // namespace Icons
