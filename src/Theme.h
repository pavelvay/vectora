#pragma once

#include <QColor>

class QApplication;

namespace Theme {

QColor accent();
QColor panel();
QColor canvas();

// Wires the dark Fusion look used across the app.
void apply(QApplication &app);

} // namespace Theme
