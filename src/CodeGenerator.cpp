#include "CodeGenerator.h"

#include <QByteArray>
#include <QStringList>
#include <QtMath>

#include <cmath>

namespace {

// ---------------------------------------------------------------------------
// Code 128 / Code 39 tables
// ---------------------------------------------------------------------------

const char *const kCode128[] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312",
    "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222",
    "123122", "123221", "223211", "221132", "221231", "213212", "223112", "312131",
    "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321",
    "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313",
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121",
    "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224",
    "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113",
    "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412",
    "211214", "211232", "2331112",
};

const QString kCode39Chars =
    QStringLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%");
const char *const kCode39[] = {
    "111331311", "311311113", "113311113", "313311111", "111331113", "311331111",
    "113331111", "111311313", "311311311", "113311311", "311113113", "113113113",
    "313113111", "111133113", "311133111", "113133111", "111113313", "311113311",
    "113113311", "111133311", "311111133", "113111133", "313111131", "111131133",
    "311131131", "113131131", "111111333", "311111331", "113111331", "111131331",
    "331111113", "133111113", "333111111", "131131113", "331131111", "133131111",
    "131111313", "331111311", "133111311", "131313111", "131311131", "131113131",
    "111313131",
};
const char *const kCode39Start = "131131311";

// ---------------------------------------------------------------------------
// Linear symbologies
// ---------------------------------------------------------------------------

CodeSymbol makeLinear(CodeKind kind, const QString &data, const QString &human)
{
    CodeSymbol symbol;
    symbol.kind = kind;
    symbol.data = data;
    symbol.humanText = human;
    symbol.modulesY = 1;
    return symbol;
}

void appendPattern(QVector<bool> *cells, const QString &modules)
{
    for (const QChar c : modules)
        cells->append(c == QLatin1Char('1'));
}

bool allDigits(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (const QChar c : text) {
        if (!c.isDigit())
            return false;
    }
    return true;
}

int eanCheckDigit(const QString &digitsWithoutCheck)
{
    int sum = 0;
    bool triple = true;
    for (int i = digitsWithoutCheck.size() - 1; i >= 0; --i) {
        const int value = digitsWithoutCheck.at(i).digitValue();
        sum += triple ? value * 3 : value;
        triple = !triple;
    }
    return (10 - sum % 10) % 10;
}

const char *const kEanL[10] = {
    "0001101", "0011001", "0010011", "0111101", "0100011",
    "0110001", "0101111", "0111011", "0110111", "0001011",
};
const char *const kEanG[10] = {
    "0100111", "0110011", "0011011", "0100001", "0011101",
    "0111001", "0000101", "0010001", "0001001", "0010111",
};
const char *const kEanR[10] = {
    "1110010", "1100110", "1101100", "1000010", "1011100",
    "1001110", "1010000", "1000100", "1001000", "1110100",
};
const char *const kEan13Parity[10] = {
    "000000", "001011", "001101", "001110", "010011",
    "011001", "011100", "010101", "010110", "011010",
};

CodeSymbol generateEan(CodeKind kind, const QString &input)
{
    QString data = input;
    data.remove(QLatin1Char(' '));
    data.remove(QLatin1Char('-'));
    int payload = 0;
    int checkIndex = 0;
    if (kind == CodeKind::Ean13) {
        payload = 12;
        checkIndex = 12;
    } else if (kind == CodeKind::Ean8) {
        payload = 7;
        checkIndex = 7;
    } else {
        payload = 11;
        checkIndex = 11;
    }
    if (!allDigits(data) || (data.size() != payload && data.size() != payload + 1)) {
        CodeSymbol error;
        error.kind = kind;
        error.error = QStringLiteral("Нужно %1 или %2 цифр.").arg(payload).arg(payload + 1);
        return error;
    }
    const int check = eanCheckDigit(data.left(payload));
    if (data.size() == payload + 1
        && data.at(checkIndex).digitValue() != check) {
        CodeSymbol error;
        error.kind = kind;
        error.error = QStringLiteral("Неверная контрольная цифра (ожидается %1).")
                          .arg(check);
        return error;
    }
    if (data.size() == payload)
        data.append(QChar('0' + check));

    CodeSymbol symbol = makeLinear(kind, data, data);
    QVector<bool> cells;
    if (kind == CodeKind::Ean8) {
        appendPattern(&cells, QStringLiteral("101"));
        for (int i = 0; i < 4; ++i)
            appendPattern(&cells, QString::fromLatin1(kEanL[data.at(i).digitValue()]));
        appendPattern(&cells, QStringLiteral("01010"));
        for (int i = 4; i < 8; ++i)
            appendPattern(&cells, QString::fromLatin1(kEanR[data.at(i).digitValue()]));
        appendPattern(&cells, QStringLiteral("101"));
    } else {
        QString source = data;
        int firstDigit = 0;
        if (kind == CodeKind::UpcA)
            source.prepend(QLatin1Char('0'));
        firstDigit = source.at(0).digitValue();
        appendPattern(&cells, QStringLiteral("101"));
        const QString parity = QString::fromLatin1(kEan13Parity[firstDigit]);
        for (int i = 0; i < 6; ++i) {
            const int digit = source.at(i + 1).digitValue();
            appendPattern(&cells, QString::fromLatin1(
                                      parity.at(i) == QLatin1Char('0') ? kEanL[digit]
                                                                       : kEanG[digit]));
        }
        appendPattern(&cells, QStringLiteral("01010"));
        for (int i = 7; i < 13; ++i)
            appendPattern(&cells, QString::fromLatin1(kEanR[source.at(i).digitValue()]));
        appendPattern(&cells, QStringLiteral("101"));
    }
    symbol.cells = cells;
    symbol.modulesX = cells.size();
    symbol.valid = true;
    return symbol;
}

CodeSymbol generateCode128(const QString &input)
{
    if (input.isEmpty()) {
        CodeSymbol error;
        error.kind = CodeKind::Code128;
        error.error = QStringLiteral("Введите данные для Code 128.");
        return error;
    }
    for (const QChar c : input) {
        if (c.unicode() < 32 || c.unicode() > 126) {
            CodeSymbol error;
            error.kind = CodeKind::Code128;
            error.error = QStringLiteral("Code 128 поддерживает ASCII 32–126.");
            return error;
        }
    }

    QVector<int> values;
    bool useC = input.size() >= 2 && input.size() % 2 == 0 && allDigits(input);
    if (useC) {
        values.append(105); // START C
        for (int i = 0; i < input.size(); i += 2)
            values.append(input.mid(i, 2).toInt());
    } else {
        values.append(104); // START B
        for (const QChar c : input)
            values.append(c.unicode() - 32);
    }
    int checksum = values.first();
    for (int i = 1; i < values.size(); ++i)
        checksum += values.at(i) * i;
    checksum %= 103;
    values.append(checksum);
    values.append(106); // STOP

    CodeSymbol symbol = makeLinear(CodeKind::Code128, input, input);
    QVector<bool> cells;
    for (int value : values) {
        const QString pattern = QString::fromLatin1(kCode128[value]);
        bool bar = true;
        for (const QChar c : pattern) {
            const int width = c.digitValue();
            for (int i = 0; i < width; ++i)
                cells.append(bar);
            bar = !bar;
        }
    }
    symbol.cells = cells;
    symbol.modulesX = cells.size();
    symbol.valid = true;
    return symbol;
}

CodeSymbol generateCode39(const QString &input)
{
    QString data = input.toUpper();
    if (data.isEmpty()) {
        CodeSymbol error;
        error.kind = CodeKind::Code39;
        error.error = QStringLiteral("Введите данные для Code 39.");
        return error;
    }
    for (const QChar c : data) {
        if (!kCode39Chars.contains(c) && c != QLatin1Char('*')) {
            CodeSymbol error;
            error.kind = CodeKind::Code39;
            error.error = QStringLiteral(
                "Code 39: допустимы 0–9, A–Z, пробел и -.$/+%%.");
            return error;
        }
    }

    QVector<bool> cells;
    const auto appendChar = [&cells, &data](const QString &pattern) {
        Q_UNUSED(data)
        bool bar = true;
        for (const QChar c : pattern) {
            const int width = c.digitValue();
            for (int i = 0; i < width; ++i)
                cells.append(bar);
            bar = !bar;
        }
    };
    appendChar(QString::fromLatin1(kCode39Start));
    for (const QChar c : data) {
        cells.append(false); // inter-character gap
        const int index = kCode39Chars.indexOf(c);
        if (index >= 0)
            appendChar(QString::fromLatin1(kCode39[index]));
    }
    cells.append(false);
    appendChar(QString::fromLatin1(kCode39Start));

    CodeSymbol symbol = makeLinear(CodeKind::Code39, data, data);
    symbol.cells = cells;
    symbol.modulesX = cells.size();
    symbol.valid = true;
    return symbol;
}

// ---------------------------------------------------------------------------
// QR code
// ---------------------------------------------------------------------------

const int kEccPerBlock[4][10] = {
    {7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
    {10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
    {13, 22, 18, 26, 18, 24, 18, 22, 20, 24},
    {17, 28, 22, 16, 22, 28, 26, 26, 24, 28},
};
const int kBlocks[4][10] = {
    {1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
    {1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
    {1, 1, 2, 2, 4, 4, 6, 6, 8, 8},
    {1, 1, 2, 4, 4, 4, 5, 6, 8, 8},
};
const int kTotalCodewords[10] = {26, 44, 70, 100, 134, 172, 196, 242, 292, 346};
const int kAlignment[10][8] = {
    {0},
    {6, 18},
    {6, 22},
    {6, 26},
    {6, 30},
    {6, 34},
    {6, 22, 38},
    {6, 24, 42},
    {6, 26, 46},
    {6, 28, 50},
};

int gfMul(int x, int y)
{
    int result = 0;
    for (int i = 7; i >= 0; --i) {
        result = (result << 1) ^ ((result >> 7) * 0x11D);
        result ^= ((y >> i) & 1) * x;
    }
    return result & 0xFF;
}

QVector<int> gfDivisor(int degree)
{
    QVector<int> divisor(degree);
    divisor[degree - 1] = 1;
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            divisor[j] = gfMul(divisor[j], root);
            if (j + 1 < degree)
                divisor[j] ^= divisor[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return divisor;
}

QVector<int> rsRemainder(const QVector<int> &data, int degree)
{
    const QVector<int> divisor = gfDivisor(degree);
    QVector<int> remainder(degree, 0);
    for (int value : data) {
        const int factor = value ^ remainder.first();
        for (int i = 0; i < degree - 1; ++i)
            remainder[i] = remainder.at(i + 1);
        remainder[degree - 1] = 0;
        for (int i = 0; i < degree; ++i)
            remainder[i] ^= gfMul(divisor.at(i), factor);
    }
    return remainder;
}

int qrDataCodewords(int version, int ecc)
{
    return kTotalCodewords[version - 1]
           - kEccPerBlock[ecc][version - 1] * kBlocks[ecc][version - 1];
}

int qrVersion(const QByteArray &data, int ecc, bool *fits)
{
    for (int version = 1; version <= 10; ++version) {
        const int headerBits = 4 + (version <= 9 ? 8 : 16);
        const int capacityBits = qrDataCodewords(version, ecc) * 8;
        if (headerBits + data.size() * 8 <= capacityBits) {
            if (fits)
                *fits = true;
            return version;
        }
    }
    if (fits)
        *fits = false;
    return 10;
}

CodeSymbol generateQr(const QByteArray &bytes, int ecc)
{
    ecc = qBound(0, ecc, 3);
    CodeSymbol symbol;
    symbol.kind = CodeKind::Qr;
    bool fits = false;
    const int version = qrVersion(bytes, ecc, &fits);
    if (!fits) {
        symbol.error = QStringLiteral(
            "Данные слишком длинные для QR до версии 10 (до 271 байта).");
        return symbol;
    }
    const int size = 17 + version * 4;
    const int dataCodewords = qrDataCodewords(version, ecc);
    const int eccPerBlock = kEccPerBlock[ecc][version - 1];
    const int blockCount = kBlocks[ecc][version - 1];

    QVector<int> bits;
    const auto appendBits = [&bits](int value, int count) {
        for (int i = count - 1; i >= 0; --i)
            bits.append((value >> i) & 1);
    };
    appendBits(0b0100, 4);
    appendBits(bytes.size(), version <= 9 ? 8 : 16);
    for (char byte : bytes)
        appendBits(static_cast<unsigned char>(byte), 8);
    const int capacityBits = dataCodewords * 8;
    appendBits(0, qMin(4, capacityBits - bits.size()));
    while (bits.size() % 8 != 0)
        bits.append(0);

    QVector<int> data;
    for (int i = 0; i < bits.size(); i += 8) {
        int value = 0;
        for (int j = 0; j < 8; ++j)
            value = (value << 1) | bits.at(i + j);
        data.append(value);
    }
    for (int pad = 0xEC; data.size() < dataCodewords; pad ^= 0xEC ^ 0x11)
        data.append(pad);

    // Split into RS blocks and interleave.
    const int shortLength = dataCodewords / blockCount;
    const int longCount = dataCodewords % blockCount;
    const int shortCount = blockCount - longCount;
    QVector<QVector<int>> blocks;
    QVector<QVector<int>> eccBlocks;
    int offset = 0;
    for (int b = 0; b < blockCount; ++b) {
        const int length = shortLength + (b >= shortCount ? 1 : 0);
        const QVector<int> block = data.mid(offset, length);
        offset += length;
        blocks.append(block);
        eccBlocks.append(rsRemainder(block, eccPerBlock));
    }
    QVector<int> codewords;
    const int maxBlock = shortLength + (longCount > 0 ? 1 : 0);
    for (int i = 0; i < maxBlock; ++i) {
        for (const QVector<int> &block : blocks) {
            if (i < block.size())
                codewords.append(block.at(i));
        }
    }
    for (int i = 0; i < eccPerBlock; ++i) {
        for (const QVector<int> &block : eccBlocks)
            codewords.append(block.at(i));
    }

    QVector<bool> modules(size * size, false);
    QVector<bool> function(size * size, false);
    const auto setFunction = [&modules, &function, size](int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= size || y >= size)
            return;
        modules[y * size + x] = dark;
        function[y * size + x] = true;
    };

    // Finder patterns with separators.
    const auto drawFinder = [&setFunction](int cx, int cy) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int distance = qMax(qAbs(dx), qAbs(dy));
                setFunction(cx + dx, cy + dy, distance != 2 && distance != 4);
            }
        }
    };
    drawFinder(3, 3);
    drawFinder(size - 4, 3);
    drawFinder(3, size - 4);

    // Timing patterns.
    for (int i = 8; i < size - 8; ++i) {
        setFunction(i, 6, i % 2 == 0);
        setFunction(6, i, i % 2 == 0);
    }

    // Alignment patterns.
    if (version >= 2) {
        int count = 2;
        while (count < 8 && kAlignment[version - 1][count] != 0)
            ++count;
        QVector<int> positions;
        for (int i = 0; i < count; ++i)
            positions.append(kAlignment[version - 1][i]);
        for (int y : positions) {
            for (int x : positions) {
                if ((x == 6 && y == 6) || (x == 6 && y == size - 7)
                    || (x == size - 7 && y == 6))
                    continue;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int distance = qMax(qAbs(dx), qAbs(dy));
                        setFunction(x + dx, y + dy, distance != 1);
                    }
                }
            }
        }
    }

    setFunction(8, size - 8, true);

    const auto drawFormat = [&setFunction, size](int eccLevel, int mask) {
        static const int formatBits[4] = {1, 0, 3, 2};
        int data = (formatBits[eccLevel] << 3) | mask;
        int remainder = data;
        for (int i = 0; i < 10; ++i)
            remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
        const int bits = ((data << 10) | remainder) ^ 0x5412;
        const auto bit = [bits](int index) {
            return ((bits >> index) & 1) != 0;
        };
        for (int i = 0; i <= 5; ++i)
            setFunction(8, i, bit(i));
        setFunction(8, 7, bit(6));
        setFunction(8, 8, bit(7));
        setFunction(7, 8, bit(8));
        for (int i = 9; i <= 14; ++i)
            setFunction(14 - i, 8, bit(i));
        for (int i = 0; i <= 7; ++i)
            setFunction(size - 1 - i, 8, bit(i));
        for (int i = 8; i <= 14; ++i)
            setFunction(8, size - 15 + i, bit(i));
    };
    drawFormat(ecc, 0);

    if (version >= 7) {
        int remainder = version;
        for (int i = 0; i < 12; ++i)
            remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
        const int bits = (version << 12) | remainder;
        for (int i = 0; i < 18; ++i) {
            const bool dark = ((bits >> i) & 1) != 0;
            const int a = size - 11 + i % 3;
            const int b = i / 3;
            setFunction(a, b, dark);
            setFunction(b, a, dark);
        }
    }

    // Place data modules in the zigzag order.
    int index = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size; ++vert) {
            const bool upward = ((right + 1) & 2) == 0;
            const int y = upward ? size - 1 - vert : vert;
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                if (function[y * size + x] || index >= codewords.size() * 8)
                    continue;
                const int value = codewords.at(index / 8);
                modules[y * size + x] = ((value >> (7 - index % 8)) & 1) != 0;
                ++index;
            }
        }
    }

    // Mask 0, then redraw the format bits for it.
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!function[y * size + x] && (x + y) % 2 == 0)
                modules[y * size + x] = !modules.at(y * size + x);
        }
    }
    drawFormat(ecc, 0);

    symbol.modulesX = size;
    symbol.modulesY = size;
    symbol.cells = modules;
    symbol.data = QString::fromUtf8(bytes);
    symbol.valid = true;
    return symbol;
}

// ---------------------------------------------------------------------------
// Data Matrix (ECC200, square symbols up to 26x26)
// ---------------------------------------------------------------------------

struct DataMatrixSize {
    int size;
    int dataCodewords;
    int eccCodewords;
};

const DataMatrixSize kDataMatrixSizes[] = {
    {10, 3, 5},   {12, 5, 7},   {14, 8, 10},  {16, 12, 12},
    {18, 18, 14}, {20, 22, 18}, {22, 30, 20}, {24, 36, 24},
    {26, 44, 28},
};

int dmGfMul(int x, int y)
{
    int result = 0;
    for (int i = 7; i >= 0; --i) {
        result = (result << 1) ^ ((result >> 7) * 0x12D);
        result ^= ((y >> i) & 1) * x;
    }
    return result & 0xFF;
}

QVector<int> dmRsRemainder(const QVector<int> &data, int degree)
{
    // Generator polynomial with roots alpha^1 .. alpha^degree, coefficients
    // stored from the leading term.
    QVector<int> generator(1, 1);
    for (int k = 0; k < degree; ++k) {
        int root = 1;
        for (int power = 0; power < k + 1; ++power)
            root = dmGfMul(root, 2);
        QVector<int> next(generator.size() + 1, 0);
        for (int j = 0; j < generator.size(); ++j) {
            next[j] ^= generator.at(j);
            next[j + 1] ^= dmGfMul(generator.at(j), root);
        }
        generator = next;
    }

    QVector<int> remainder(degree, 0);
    for (int value : data) {
        const int factor = value ^ remainder.first();
        for (int i = 0; i < degree - 1; ++i)
            remainder[i] = remainder.at(i + 1);
        remainder[degree - 1] = 0;
        for (int i = 0; i < degree; ++i)
            remainder[i] ^= dmGfMul(generator.at(i + 1), factor);
    }
    return remainder;
}

CodeSymbol generateDataMatrix(const QString &input)
{
    const QByteArray bytes = input.toUtf8();
    QVector<int> codewords;
    for (int i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes.at(i));
        if (i + 1 < bytes.size() && c >= '0' && c <= '9'
            && bytes.at(i + 1) >= '0' && bytes.at(i + 1) <= '9') {
            codewords.append(130 + (c - '0') * 10 + (bytes.at(i + 1) - '0'));
            ++i;
        } else if (c <= 127) {
            codewords.append(c + 1);
        } else {
            codewords.append(235);
            codewords.append(c - 127);
        }
    }

    const DataMatrixSize *chosen = nullptr;
    for (const DataMatrixSize &size : kDataMatrixSizes) {
        if (codewords.size() <= size.dataCodewords) {
            chosen = &size;
            break;
        }
    }
    CodeSymbol symbol;
    symbol.kind = CodeKind::DataMatrix;
    if (!chosen) {
        symbol.error = QStringLiteral(
            "Данные слишком длинные для Data Matrix до 26×26 (до 44 байт).");
        return symbol;
    }

    const int dataCount = chosen->dataCodewords;
    if (codewords.isEmpty())
        codewords.append(129);
    if (codewords.size() < dataCount)
        codewords.append(129);
    int padPosition = codewords.size();
    while (codewords.size() < dataCount) {
        int pad = 129 + ((149 * (padPosition + 1)) % 253) + 1;
        if (pad > 254)
            pad -= 254;
        codewords.append(pad);
        ++padPosition;
    }

    QVector<int> stream = codewords;
    const QVector<int> ecc = dmRsRemainder(codewords, chosen->eccCodewords);
    stream += ecc;

    const int dataSize = chosen->size - 2;
    QVector<int> bits(dataSize * dataSize, -1);
    const auto hasBit = [&bits, dataSize](int col, int row) {
        return row >= 0 && row < dataSize && col >= 0 && col < dataSize
               && bits.at(row * dataSize + col) >= 0;
    };
    const auto placeModule = [&bits, dataSize, &stream](int row, int col,
                                                       int position, int bit) {
        if (row < 0) {
            row += dataSize;
            col += 4 - ((dataSize + 4) % 8);
        }
        if (col < 0) {
            col += dataSize;
            row += 4 - ((dataSize + 4) % 8);
        }
        if (row < 0 || row >= dataSize || col < 0 || col >= dataSize)
            return;
        if (position < 0 || position >= stream.size())
            return;
        const bool dark =
            ((stream.at(position) >> (8 - bit)) & 1) != 0;
        bits[row * dataSize + col] = dark ? 1 : 0;
    };
    const auto utah = [&placeModule](int row, int col, int position) {
        placeModule(row - 2, col - 2, position, 1);
        placeModule(row - 2, col - 1, position, 2);
        placeModule(row - 1, col - 2, position, 3);
        placeModule(row - 1, col - 1, position, 4);
        placeModule(row - 1, col, position, 5);
        placeModule(row, col - 2, position, 6);
        placeModule(row, col - 1, position, 7);
        placeModule(row, col, position, 8);
    };
    const auto corner1 = [&placeModule, dataSize](int position) {
        placeModule(dataSize - 1, 0, position, 1);
        placeModule(dataSize - 1, 1, position, 2);
        placeModule(dataSize - 1, 2, position, 3);
        placeModule(0, dataSize - 2, position, 4);
        placeModule(0, dataSize - 1, position, 5);
        placeModule(1, dataSize - 1, position, 6);
        placeModule(2, dataSize - 1, position, 7);
        placeModule(3, dataSize - 1, position, 8);
    };
    const auto corner2 = [&placeModule, dataSize](int position) {
        placeModule(dataSize - 3, 0, position, 1);
        placeModule(dataSize - 2, 0, position, 2);
        placeModule(dataSize - 1, 0, position, 3);
        placeModule(0, dataSize - 4, position, 4);
        placeModule(0, dataSize - 3, position, 5);
        placeModule(0, dataSize - 2, position, 6);
        placeModule(0, dataSize - 1, position, 7);
        placeModule(1, dataSize - 1, position, 8);
    };
    const auto corner3 = [&placeModule, dataSize](int position) {
        placeModule(dataSize - 3, 0, position, 1);
        placeModule(dataSize - 2, 0, position, 2);
        placeModule(dataSize - 1, 0, position, 3);
        placeModule(0, dataSize - 2, position, 4);
        placeModule(0, dataSize - 1, position, 5);
        placeModule(1, dataSize - 1, position, 6);
        placeModule(2, dataSize - 1, position, 7);
        placeModule(3, dataSize - 1, position, 8);
    };
    const auto corner4 = [&placeModule, dataSize](int position) {
        placeModule(dataSize - 1, 0, position, 1);
        placeModule(dataSize - 1, dataSize - 1, position, 2);
        placeModule(0, dataSize - 3, position, 3);
        placeModule(0, dataSize - 2, position, 4);
        placeModule(0, dataSize - 1, position, 5);
        placeModule(1, dataSize - 3, position, 6);
        placeModule(1, dataSize - 2, position, 7);
        placeModule(1, dataSize - 1, position, 8);
    };

    int position = 0;
    int row = 4;
    int col = 0;
    do {
        if (row == dataSize && col == 0)
            corner1(position++);
        if (row == dataSize - 2 && col == 0 && dataSize % 4 != 0)
            corner2(position++);
        if (row == dataSize - 2 && col == 0 && dataSize % 8 == 4)
            corner3(position++);
        if (row == dataSize + 4 && col == 2 && dataSize % 8 == 0)
            corner4(position++);
        do {
            if (row < dataSize && col >= 0 && !hasBit(col, row))
                utah(row, col, position++);
            row -= 2;
            col += 2;
        } while (row >= 0 && col < dataSize);
        row += 1;
        col += 3;
        do {
            if (row >= 0 && col < dataSize && !hasBit(col, row))
                utah(row, col, position++);
            row += 2;
            col -= 2;
        } while (row < dataSize && col >= 0);
        row += 3;
        col += 1;
    } while (row < dataSize || col < dataSize);

    const int size = chosen->size;
    QVector<bool> matrix(size * size, false);
    const auto module = [&matrix, size](int x, int y, bool dark) {
        if (x >= 0 && y >= 0 && x < size && y < size)
            matrix[y * size + x] = dark;
    };
    for (int y = 0; y < dataSize; ++y) {
        for (int x = 0; x < dataSize; ++x) {
            const int value = bits.at(y * dataSize + x);
            module(x + 1, y + 1, value == 1);
        }
    }
    for (int i = 0; i < size; ++i) {
        module(i, 0, i % 2 == 0);
        module(i, size - 1, true);
        module(0, i, true);
        module(size - 1, i, i % 2 == 1);
    }

    symbol.modulesX = size;
    symbol.modulesY = size;
    symbol.cells = matrix;
    symbol.data = input;
    symbol.valid = true;
    return symbol;
}

} // namespace

QString codeKindName(CodeKind kind)
{
    switch (kind) {
    case CodeKind::Qr: return QStringLiteral("QR-код");
    case CodeKind::Ean13: return QStringLiteral("EAN-13");
    case CodeKind::Ean8: return QStringLiteral("EAN-8");
    case CodeKind::UpcA: return QStringLiteral("UPC-A");
    case CodeKind::Code128: return QStringLiteral("Code 128");
    case CodeKind::Code39: return QStringLiteral("Code 39");
    case CodeKind::DataMatrix: return QStringLiteral("Data Matrix");
    }
    return QStringLiteral("Код");
}

QStringList codeKindNames()
{
    return {codeKindName(CodeKind::Qr), codeKindName(CodeKind::Ean13),
            codeKindName(CodeKind::Ean8), codeKindName(CodeKind::UpcA),
            codeKindName(CodeKind::Code128), codeKindName(CodeKind::Code39),
            codeKindName(CodeKind::DataMatrix)};
}

CodeKind codeKindFromIndex(int index)
{
    switch (index) {
    case 1: return CodeKind::Ean13;
    case 2: return CodeKind::Ean8;
    case 3: return CodeKind::UpcA;
    case 4: return CodeKind::Code128;
    case 5: return CodeKind::Code39;
    case 6: return CodeKind::DataMatrix;
    default: return CodeKind::Qr;
    }
}

bool codeShowsHumanText(CodeKind kind)
{
    return kind != CodeKind::Qr && kind != CodeKind::DataMatrix;
}

CodeSymbol generateCode(CodeKind kind, const QString &data, int ecc)
{
    switch (kind) {
    case CodeKind::Qr:
        return generateQr(data.toUtf8(), ecc);
    case CodeKind::Ean13:
    case CodeKind::Ean8:
    case CodeKind::UpcA:
        return generateEan(kind, data);
    case CodeKind::Code128:
        return generateCode128(data);
    case CodeKind::Code39:
        return generateCode39(data);
    case CodeKind::DataMatrix:
        return generateDataMatrix(data);
    }
    return CodeSymbol();
}

int qrVersionForData(const QByteArray &data, int ecc, bool *fits)
{
    return qrVersion(data, ecc, fits);
}
