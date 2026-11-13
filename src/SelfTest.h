#pragma once

#include <QString>

namespace SelfTest {

// Headless checks for document I/O and drawing tools. Returns 0 on success.
int run(const QString &directory);

} // namespace SelfTest
