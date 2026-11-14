#pragma once

#include <QColor>
#include <QString>
#include <QVector>

// Supported machine-readable codes.
enum class CodeKind {
    Qr,
    Ean13,
    Ean8,
    UpcA,
    Code128,
    Code39,
    DataMatrix
};

struct CodeSymbol {
    bool valid = false;
    QString error;
    QString data;          // normalized data (check digits added for EAN/UPC)
    QString humanText;     // text printed under linear barcodes
    int modulesX = 0;
    int modulesY = 0;
    QVector<bool> cells;   // row-major modules; 1D barcodes use one row
    CodeKind kind = CodeKind::Qr;
};

QString codeKindName(CodeKind kind);
QStringList codeKindNames();
CodeKind codeKindFromIndex(int index);

// Generates a standards-compliant symbol. `ecc` is the QR error-correction
// level (0 L, 1 M, 2 Q, 3 H) and is ignored by other symbologies.
CodeSymbol generateCode(CodeKind kind, const QString &data, int ecc = 1);

// Module matrix rendered as an SVG-ready path is produced by the document;
// this helper reports whether the payload fits the supported symbol versions.
int qrVersionForData(const QByteArray &data, int ecc, bool *fits = nullptr);

// Barcode human-readable text rules (EAN/UPC and Code 39 keep their text).
bool codeShowsHumanText(CodeKind kind);
