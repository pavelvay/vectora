#include "FormatIO.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QSharedPointer>
#include <QFontMetricsF>
#include <QImage>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <cstdio>

#if defined(VECTORA_HAVE_QT_PDF)
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#endif

#include "CdrIO.h"
#include "CmxIO.h"
#include "Document.h"
#include "SvgImporter.h"
#include "VectorCraftIO.h"

namespace VectorFormats {
namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

QString number(qreal value)
{
    if (qAbs(value) < 0.0005)
        return QStringLiteral("0");
    return QString::number(value, 'f', 3);
}

QColor overWhite(const QColor &color, qreal opacity = 1.0)
{
    const qreal alpha = qBound(0.0, color.alphaF() * opacity, 1.0);
    return QColor(qRound(color.red() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.green() * alpha + 255.0 * (1.0 - alpha)),
                  qRound(color.blue() * alpha + 255.0 * (1.0 - alpha)));
}

QColor solidFillColor(const VectorObject &object)
{
    if (object.fillStyle == 0 || object.gradientStops.size() < 2)
        return object.fill;
    qreal r = 0.0;
    qreal g = 0.0;
    qreal b = 0.0;
    qreal weight = 0.0;
    for (const GradientStop &stop : object.gradientStops) {
        r += stop.color.red();
        g += stop.color.green();
        b += stop.color.blue();
        weight += 1.0;
    }
    if (weight <= 0.0)
        return object.fill;
    return QColor(qRound(r / weight), qRound(g / weight), qRound(b / weight));
}

bool hasLiveEffects(const VectorObject &object)
{
    for (const Effect &effect : object.effects) {
        if (effect.enabled)
            return true;
    }
    return false;
}

bool objectVisibleOnPage(const Document &document, const VectorObject &object)
{
    if (object.layer >= 0 && object.layer < document.layerVisible.size()
        && !document.layerVisible.at(object.layer))
        return false;
    if (!document.pages.isEmpty()) {
        const int masterIndex = (document.currentPage >= 0
                                 && document.currentPage < document.pages.size())
                                    ? document.pages.at(document.currentPage).master
                                    : -1;
        const bool onPage = object.page == document.currentPage;
        const bool masterObject = object.page < 0 && masterIndex >= 0
                                  && object.page == -1 - masterIndex;
        if (!onPage && !masterObject)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// PDF export (shared by .pdf and PDF-compatible .ai)
// ---------------------------------------------------------------------------

bool writePdfInternal(const Document &document, const QString &path, bool illustrator,
                      QString *error)
{
    const Document source = document;
    const int count = qMax(1, source.pages.size());
    QPdfWriter writer(path);
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(QFileInfo(path).completeBaseName());
    writer.setCreator(illustrator ? QStringLiteral("Adobe Illustrator")
                                  : QStringLiteral("Vectora"));

    QPainter painter;
    bool started = false;
    for (int index = 0; index < count; ++index) {
        Document pageDocument = source;
        pageDocument.currentPage = index < source.pages.size() ? index : 0;
        if (index < source.pages.size())
            pageDocument.pageSize = source.pages.at(index).size;
        const QSizeF millimeters(pageDocument.pageSize.width() * 25.4 / 96.0,
                                 pageDocument.pageSize.height() * 25.4 / 96.0);
        writer.setPageSize(QPageSize(millimeters, QPageSize::Millimeter,
                                     QStringLiteral("Vectora")));
        if (!started) {
            if (!painter.begin(&writer)) {
                if (error)
                    *error = QStringLiteral("Не удалось создать PDF.");
                return false;
            }
            started = true;
        } else {
            writer.newPage();
        }
        painter.save();
        const qreal scaleX = writer.width() / pageDocument.pageSize.width();
        const qreal scaleY = writer.height() / pageDocument.pageSize.height();
        painter.scale(scaleX, scaleY);
        pageDocument.render(&painter);
        painter.restore();
    }
    if (started)
        painter.end();
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить PDF.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// PDF import
// ---------------------------------------------------------------------------

void appendPageImage(Document *document, const QImage &image, const QSizeF &pageSize,
                     int pageIndex, const QString &name)
{
    PageInfo info;
    info.name = name;
    info.size = pageSize;
    info.background = QColor(Qt::white);
    document->pages.append(info);

    QImage flattened(image.size(), QImage::Format_ARGB32_Premultiplied);
    flattened.fill(Qt::white);
    {
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
    }

    VectorObject object;
    object.type = ShapeType::Image;
    object.image = flattened;
    object.textBox = QRectF(0.0, 0.0, pageSize.width(), pageSize.height());
    object.pos = QPointF(0.0, 0.0);
    object.page = pageIndex;
    object.layer = 0;
    object.hasFill = false;
    object.hasStroke = false;
    object.name = name;
    document->objects.append(object);
}

#if defined(VECTORA_HAVE_QT_PDF)
bool importPdfData(const QByteArray &data, Document *document, QString *error)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QPdfDocument pdf;
    pdf.load(&buffer);
    if (pdf.status() == QPdfDocument::Status::Loading) {
        QElapsedTimer timer;
        timer.start();
        while (pdf.status() == QPdfDocument::Status::Loading
               && timer.elapsed() < 15000) {
            QCoreApplication::processEvents();
        }
    }
    if (pdf.status() != QPdfDocument::Status::Ready) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать PDF.");
        return false;
    }
    const int pageCount = pdf.pageCount();
    if (pageCount <= 0) {
        if (error)
            *error = QStringLiteral("PDF не содержит страниц.");
        return false;
    }

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers = {QStringLiteral("Импорт PDF")};
    document->layerVisible = {true};
    document->layerLocked = {false};

    // 200 dpi gives crisp canvas artwork. Very long documents are rendered at
    // a lower resolution so the imported pages fit in a sane memory budget.
    double baseScale = 200.0 / 72.0;
    double totalPixels = 0.0;
    for (int index = 0; index < pageCount; ++index) {
        const QSizeF points = pdf.pagePointSize(index);
        totalPixels += points.width() * baseScale * points.height() * baseScale;
    }
    constexpr double kPixelBudget = 120.0e6;
    if (totalPixels > kPixelBudget)
        baseScale *= std::sqrt(kPixelBudget / totalPixels);
    baseScale = qMax(baseScale, 72.0 / 72.0);

    for (int index = 0; index < pageCount; ++index) {
        const QSizeF points = pdf.pagePointSize(index);
        const QSizeF pageSize(points.width() * 96.0 / 72.0,
                              points.height() * 96.0 / 72.0);
        qreal scale = baseScale;
        const qreal maxDimension = qMax(points.width(), points.height()) * scale;
        if (maxDimension > 5000.0)
            scale *= 5000.0 / maxDimension;
        const QSize pixels(qMax(1, qRound(points.width() * scale)),
                           qMax(1, qRound(points.height() * scale)));
        const QImage rendered = pdf.render(index, pixels);
        if (rendered.isNull())
            continue;
        appendPageImage(document, rendered, pageSize, index,
                        QStringLiteral("Страница %1").arg(index + 1));
    }

    if (document->pages.isEmpty()) {
        if (error)
            *error = QStringLiteral("PDF не содержит отрисовываемых страниц.");
        return false;
    }
    document->pageSize = document->pages.first().size;
    return true;
}
#endif

bool importPdfVectorWithTool(const QString &path, Document *document, QString *error)
{
    const QString cairo = QStandardPaths::findExecutable(QStringLiteral("pdftocairo"));
    if (cairo.isEmpty() || path.isEmpty())
        return false;

    QTemporaryDir temp;
    if (!temp.isValid())
        return false;

    QVector<PageInfo> pages;
    QVector<VectorObject> objects;
    for (int page = 1; page <= 500; ++page) {
        const QString output = temp.filePath(QStringLiteral("page-%1").arg(page));
        QProcess process;
        process.setStandardInputFile(QProcess::nullDevice());
        process.start(cairo, {QStringLiteral("-svg"),
                              QStringLiteral("-f"), QString::number(page),
                              QStringLiteral("-l"), QString::number(page),
                              path, output});
        if (!process.waitForFinished(60000)
            || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0)
            break;
        const QFileInfo fileInfo(output);
        if (!fileInfo.exists() || fileInfo.size() < 20)
            break;

        // Masks, filters and clip paths are not modelled by the vector
        // importer yet. Such pages keep full fidelity through the raster
        // fallback instead of showing black boxes or clipped-away artwork.
        QFile svgFile(output);
        if (!svgFile.open(QIODevice::ReadOnly))
            return false;
        const QByteArray svgData = svgFile.readAll();
        if (svgData.contains("<mask") || svgData.contains("<filter")
            || svgData.contains("clip-path="))
            return false;

        Document pageDocument;
        QString pageError;
        if (!importSvgFile(output, &pageDocument, &pageError))
            return false;

        PageInfo info;
        info.name = QStringLiteral("Страница %1").arg(page);
        info.size = pageDocument.pageSize;
        info.background = QColor(Qt::white);
        pages.append(info);
        for (VectorObject object : pageDocument.objects) {
            object.page = page - 1;
            object.layer = 0;
            object.id = 0;
            object.group = 0;
            objects.append(object);
        }
    }

    if (pages.isEmpty())
        return false;

    document->objects = objects;
    document->pages = pages;
    document->currentPage = 0;
    document->pageSize = pages.first().size;
    document->layers = {QStringLiteral("Импорт PDF")};
    document->layerVisible = {true};
    document->layerLocked = {false};
    document->groups.clear();
    document->blends.clear();
    document->symbols.clear();
    document->instances.clear();
    document->patterns.clear();
    document->masters.clear();
    if (error)
        error->clear();
    return true;
}

bool importPdfWithTool(const QString &path, Document *document, QString *error)
{
    const QString gs = QStandardPaths::findExecutable(QStringLiteral("gs"));
    if (gs.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "Чтение PDF недоступно в этой сборке: нет модуля Qt PDF "
                "и не найден Ghostscript.");
        return false;
    }
    QTemporaryDir temp;
    if (!temp.isValid()) {
        if (error)
            *error = QStringLiteral("Не удалось создать временную папку.");
        return false;
    }
    const QString pattern = temp.filePath(QStringLiteral("page-%03d.png"));
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(gs, {QStringLiteral("-q"), QStringLiteral("-dNOPAUSE"),
                       QStringLiteral("-dBATCH"), QStringLiteral("-dSAFER"),
                       QStringLiteral("-sDEVICE=png16m"), QStringLiteral("-r150"),
                       QStringLiteral("-sOutputFile=%1").arg(pattern), path});
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("Ghostscript не смог прочитать PDF.");
        return false;
    }

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers = {QStringLiteral("Импорт PDF")};
    document->layerVisible = {true};
    document->layerLocked = {false};

    int pageIndex = 0;
    for (;;) {
        const QString file = temp.filePath(
            QStringLiteral("page-%1.png").arg(pageIndex + 1, 3, 10, QLatin1Char('0')));
        QImageReader reader(file);
        const QImage image = reader.read();
        if (image.isNull())
            break;
        const QSizeF pageSize(image.width() * 96.0 / 150.0,
                              image.height() * 96.0 / 150.0);
        appendPageImage(document, image, pageSize, pageIndex,
                        QStringLiteral("Страница %1").arg(pageIndex + 1));
        ++pageIndex;
    }
    if (document->pages.isEmpty()) {
        if (error)
            *error = QStringLiteral("Ghostscript не вернул изображение страниц.");
        return false;
    }
    document->pageSize = document->pages.first().size;
    return true;
}

// ---------------------------------------------------------------------------
// EPS / PostScript import
//
// A self-contained interpreter for the drawing subset used by vector EPS
// files: paths, fills, strokes, colours, transformations and text outlines.
// Complex files fall back to Ghostscript (when installed) or to the embedded
// EPSI preview.
// ---------------------------------------------------------------------------

struct Affine {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;

    QPointF map(const QPointF &point) const
    {
        return QPointF(a * point.x() + c * point.y() + e,
                       b * point.x() + d * point.y() + f);
    }

    // Returns `this` applied after `inner`.
    Affine compose(const Affine &inner) const
    {
        Affine result;
        result.a = a * inner.a + c * inner.b;
        result.b = b * inner.a + d * inner.b;
        result.c = a * inner.c + c * inner.d;
        result.d = b * inner.c + d * inner.d;
        result.e = a * inner.e + c * inner.f + e;
        result.f = b * inner.e + d * inner.f + f;
        return result;
    }

    QTransform toTransform() const
    {
        return QTransform(a, b, c, d, e, f);
    }

    double scaleFactor() const
    {
        return std::sqrt(qAbs(a * d - b * c));
    }

    static Affine translation(double x, double y)
    {
        Affine m;
        m.e = x;
        m.f = y;
        return m;
    }

    static Affine scaling(double x, double y)
    {
        Affine m;
        m.a = x;
        m.d = y;
        return m;
    }

    static Affine rotation(double degrees)
    {
        const double radians = degrees * M_PI / 180.0;
        Affine m;
        m.a = std::cos(radians);
        m.b = std::sin(radians);
        m.c = -std::sin(radians);
        m.d = std::cos(radians);
        return m;
    }
};

struct PsValue;
using PsDict = QHash<QString, PsValue>;

struct PsValue {
    enum Kind { Number, Boolean, Name, String, Procedure, Array, Dictionary, Font, Mark };
    Kind kind = Number;
    double number = 0.0;
    bool boolean = false;
    bool literal = false;
    QString text;
    QVector<PsValue> items;
    QSharedPointer<PsDict> dictionary;
    QString fontName;
    double fontSize = 1.0;
};

bool isDelimiter(QChar c)
{
    static const QString delimiters = QStringLiteral("()<>[]{}/%");
    return c.isSpace() || delimiters.contains(c);
}

class PsTokenizer
{
public:
    explicit PsTokenizer(const QByteArray &data)
        : m_data(data)
    {
    }

    QVector<PsValue> parse()
    {
        QVector<PsValue> values;
        parseInto(values, '\0');
        return values;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'
                || c == '\0') {
                ++m_pos;
            } else if (c == '%') {
                while (m_pos < m_data.size() && m_data.at(m_pos) != '\n'
                       && m_data.at(m_pos) != '\r')
                    ++m_pos;
            } else {
                break;
            }
        }
    }

    bool parseInto(QVector<PsValue> &out, char terminator)
    {
        while (true) {
            skipSpace();
            if (m_pos >= m_data.size())
                return terminator == '\0';
            const char c = m_data.at(m_pos);
            if (terminator != '\0' && c == terminator) {
                ++m_pos;
                return true;
            }
            if (!parseOne(out))
                return false;
        }
    }

    bool parseOne(QVector<PsValue> &out)
    {
        skipSpace();
        if (m_pos >= m_data.size())
            return false;
        const char c = m_data.at(m_pos);

        if (c == '{') {
            ++m_pos;
            PsValue value;
            value.kind = PsValue::Procedure;
            parseInto(value.items, '}');
            out.append(value);
            return true;
        }
        if (c == '[') {
            ++m_pos;
            PsValue value;
            value.kind = PsValue::Array;
            parseUntil(value.items, ']');
            out.append(value);
            return true;
        }
        if (c == '(') {
            ++m_pos;
            QString text;
            int depth = 1;
            while (m_pos < m_data.size() && depth > 0) {
                QChar ch = QLatin1Char(m_data.at(m_pos++));
                if (ch == QLatin1Char('\\')) {
                    if (m_pos >= m_data.size())
                        break;
                    QChar next = QLatin1Char(m_data.at(m_pos++));
                    switch (next.unicode()) {
                    case 'n': text += QLatin1Char('\n'); break;
                    case 'r': text += QLatin1Char('\r'); break;
                    case 't': text += QLatin1Char('\t'); break;
                    case 'b': text += QLatin1Char('\b'); break;
                    case 'f': text += QLatin1Char('\f'); break;
                    default: text += next; break;
                    }
                } else if (ch == QLatin1Char('(')) {
                    ++depth;
                    text += ch;
                } else if (ch == QLatin1Char(')')) {
                    if (--depth > 0)
                        text += ch;
                } else {
                    text += ch;
                }
            }
            PsValue value;
            value.kind = PsValue::String;
            value.text = text;
            out.append(value);
            return true;
        }
        if (c == '<' && m_pos + 1 < m_data.size() && m_data.at(m_pos + 1) == '<') {
            m_pos += 2;
            PsValue value;
            value.kind = PsValue::Dictionary;
            parseUntil(value.items, '>');
            if (m_pos < m_data.size() && m_data.at(m_pos) == '>')
                ++m_pos;
            out.append(value);
            return true;
        }
        if (c == '<') {
            ++m_pos;
            QString hex;
            while (m_pos < m_data.size() && m_data.at(m_pos) != '>') {
                const char digit = m_data.at(m_pos++);
                if (isxdigit(static_cast<unsigned char>(digit)))
                    hex += QLatin1Char(digit);
            }
            if (m_pos < m_data.size())
                ++m_pos;
            if (hex.size() % 2 == 1)
                hex += QLatin1Char('0');
            QByteArray bytes;
            for (int i = 0; i + 1 < hex.size(); i += 2)
                bytes.append(static_cast<char>(hex.mid(i, 2).toInt(nullptr, 16)));
            PsValue value;
            value.kind = PsValue::String;
            value.text = QString::fromLatin1(bytes);
            out.append(value);
            return true;
        }
        if (c == '/') {
            ++m_pos;
            PsValue value;
            value.kind = PsValue::Name;
            value.literal = true;
            value.text = readWord();
            if (value.text.isEmpty() && m_pos < m_data.size())
                ++m_pos;
            out.append(value);
            return true;
        }

        const QString word = readWord();
        if (word.isEmpty()) {
            ++m_pos;
            return true;
        }
        PsValue value;
        bool numeric = false;
        if (word == QLatin1String("true") || word == QLatin1String("false")) {
            value.kind = PsValue::Boolean;
            value.boolean = word == QLatin1String("true");
            numeric = true;
        } else if (!word.contains(QLatin1Char('#'))) {
            const double parsed = word.toDouble(&numeric);
            if (numeric) {
                value.kind = PsValue::Number;
                value.number = parsed;
            }
        }
        if (!numeric) {
            value.kind = PsValue::Name;
            value.text = word;
        }
        out.append(value);
        return true;
    }

    void parseUntil(QVector<PsValue> &out, char terminator)
    {
        while (true) {
            skipSpace();
            if (m_pos >= m_data.size())
                return;
            const char c = m_data.at(m_pos);
            if (c == terminator) {
                ++m_pos;
                return;
            }
            if (!parseOne(out))
                return;
        }
    }

    QString readWord()
    {
        QString word;
        while (m_pos < m_data.size()) {
            const QChar c = QLatin1Char(m_data.at(m_pos));
            if (isDelimiter(c))
                break;
            word += c;
            ++m_pos;
        }
        return word;
    }

    QByteArray m_data;
    int m_pos = 0;
};

struct GState {
    Affine ctm;
    QColor fill = QColor(Qt::black);
    QColor stroke = QColor(Qt::black);
    double lineWidth = 1.0;
    int cap = 0;
    int join = 0;
    double miter = 10.0;
    QString fontName = QStringLiteral("Helvetica");
    double fontSize = 12.0;
};

QString mapPostScriptFont(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QLatin1String("courier")) || lower.contains(QLatin1String("mono")))
        return QStringLiteral("monospace");
    if (lower.contains(QLatin1String("times")) || lower.contains(QLatin1String("serif")))
        return QStringLiteral("serif");
    return QStringLiteral("sans-serif");
}

class PsMachine
{
public:
    PsMachine(Document *document, const Affine &base, int pageIndex)
        : m_document(document), m_pageIndex(pageIndex)
    {
        m_dicts.append(QSharedPointer<PsDict>::create());
        GState state;
        state.ctm = base;
        m_states.append(state);
    }

    void run(const QVector<PsValue> &program)
    {
        execute(program);
    }

private:
    GState &state() { return m_states.last(); }

    void push(const PsValue &value) { m_stack.append(value); }

    PsValue pop()
    {
        if (m_stack.isEmpty())
            return PsValue();
        return m_stack.takeLast();
    }

    bool popNumber(double *value)
    {
        if (m_stack.isEmpty())
            return false;
        const PsValue valueOnStack = m_stack.takeLast();
        if (valueOnStack.kind == PsValue::Number) {
            *value = valueOnStack.number;
            return true;
        }
        if (valueOnStack.kind == PsValue::Boolean) {
            *value = valueOnStack.boolean ? 1.0 : 0.0;
            return true;
        }
        return false;
    }

    double numberOr(double fallback)
    {
        double value = fallback;
        popNumber(&value);
        return value;
    }

    bool popString(QString *text)
    {
        if (m_stack.isEmpty())
            return false;
        const PsValue value = m_stack.takeLast();
        if (value.kind == PsValue::String || value.kind == PsValue::Name) {
            *text = value.text;
            return true;
        }
        return false;
    }

    const PsValue *lookup(const QString &name) const
    {
        for (int i = m_dicts.size() - 1; i >= 0; --i) {
            const auto &dict = *m_dicts.at(i);
            const auto it = dict.constFind(name);
            if (it != dict.constEnd())
                return &it.value();
        }
        return nullptr;
    }

    void execute(const QVector<PsValue> &program)
    {
        if (m_aborted || m_depth >= 128)
            return;
        ++m_depth;
        for (const PsValue &value : program) {
            if (m_exit || m_aborted)
                break;
            if (++m_steps > 4000000) {
                m_aborted = true;
                break;
            }
            execValue(value);
        }
        --m_depth;
    }

    void execValue(const PsValue &value)
    {
        switch (value.kind) {
        case PsValue::Number:
        case PsValue::String:
        case PsValue::Procedure:
        case PsValue::Array:
        case PsValue::Dictionary:
        case PsValue::Font:
        case PsValue::Boolean:
        case PsValue::Mark:
            push(value);
            break;
        case PsValue::Name:
            if (value.literal) {
                push(value);
                break;
            }
            if (const PsValue *defined = lookup(value.text)) {
                if (defined->kind == PsValue::Procedure)
                    execute(defined->items);
                else
                    push(*defined);
                break;
            }
            dispatch(value.text);
            break;
        }
    }

    void appendObject(const QPainterPath &path, bool doFill, bool evenOdd,
                      bool doStroke)
    {
        if (path.isEmpty() || m_states.isEmpty())
            return;
        const QPainterPath mapped = state().ctm.toTransform().map(path);
        if (mapped.isEmpty())
            return;

        if (doFill) {
            VectorObject object;
            object.type = ShapeType::Freehand;
            object.path = mapped;
            object.path.setFillRule(evenOdd ? Qt::OddEvenFill : Qt::WindingFill);
            object.pos = QPointF(0.0, 0.0);
            object.page = m_pageIndex;
            object.layer = 0;
            object.hasFill = true;
            object.fill = state().fill;
            object.hasStroke = false;
            m_document->objects.append(object);
        }
        if (doStroke) {
            VectorObject object;
            object.type = ShapeType::Freehand;
            object.path = mapped;
            object.pos = QPointF(0.0, 0.0);
            object.page = m_pageIndex;
            object.layer = 0;
            object.hasFill = false;
            object.hasStroke = true;
            object.stroke = state().stroke;
            object.strokeWidth = qMax(0.05, state().lineWidth * state().ctm.scaleFactor());
            object.capStyle = state().cap;
            object.joinStyle = state().join;
            object.miterLimit = state().miter;
            m_document->objects.append(object);
        }
    }

    void moveTo(double x, double y)
    {
        if (m_path.elementCount() == 0)
            m_path.moveTo(x, y);
        else
            m_path.lineTo(x, y);
        m_current = QPointF(x, y);
        m_subpathStart = m_current;
        m_hasCurrent = true;
    }

    void lineTo(double x, double y)
    {
        if (!m_hasCurrent)
            moveTo(x, y);
        else
            m_path.lineTo(x, y);
        m_current = QPointF(x, y);
    }

    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
    {
        if (!m_hasCurrent)
            m_path.moveTo(x1, y1);
        m_path.cubicTo(x1, y1, x2, y2, x3, y3);
        m_current = QPointF(x3, y3);
        m_hasCurrent = true;
    }

    void arc(bool clockwise)
    {
        double ry = numberOr(0.0);
        double rx = numberOr(0.0);
        double angle2 = numberOr(0.0);
        double angle1 = numberOr(0.0);
        double cy = numberOr(0.0);
        double cx = numberOr(0.0);
        const double radius = (rx + ry) / 2.0;
        if (radius <= 0.0)
            return;
        double start = angle1;
        double end = angle2;
        if (clockwise) {
            while (end > start)
                end -= 360.0;
        } else {
            while (end < start)
                end += 360.0;
        }
        const QPointF first(cx + radius * std::cos(start * M_PI / 180.0),
                            cy + radius * std::sin(start * M_PI / 180.0));
        if (m_hasCurrent)
            m_path.lineTo(first);
        else
            m_path.moveTo(first);
        const double step = clockwise ? -2.0 : 2.0;
        QPointF last = first;
        for (double angle = start + step;
             clockwise ? angle >= end : angle <= end; angle += step) {
            last = QPointF(cx + radius * std::cos(angle * M_PI / 180.0),
                           cy + radius * std::sin(angle * M_PI / 180.0));
            m_path.lineTo(last);
        }
        last = QPointF(cx + radius * std::cos(end * M_PI / 180.0),
                       cy + radius * std::sin(end * M_PI / 180.0));
        m_path.lineTo(last);
        m_current = last;
        m_hasCurrent = true;
    }

    void showText(const QString &text)
    {
        if (text.isEmpty())
            return;
        QFont font(mapPostScriptFont(state().fontName));
        font.setPixelSize(qMax(1, qRound(state().fontSize)));
        QPainterPath glyphs;
        glyphs.addText(m_current, font, text);
        const QPainterPath mapped = state().ctm.toTransform().map(glyphs);
        if (!mapped.isEmpty()) {
            VectorObject object;
            object.type = ShapeType::Freehand;
            object.path = mapped;
            object.pos = QPointF(0.0, 0.0);
            object.page = m_pageIndex;
            object.layer = 0;
            object.hasFill = true;
            object.fill = state().fill;
            object.hasStroke = false;
            m_document->objects.append(object);
        }
        const QFontMetricsF metrics(font);
        m_current.rx() += metrics.horizontalAdvance(text);
    }

    void dispatch(const QString &name)
    {
        auto takeNumbers = [this](int count, double *values) {
            for (int i = count - 1; i >= 0; --i)
                values[i] = numberOr(0.0);
        };

        if (name == QLatin1String("pop")) {
            pop();
        } else if (name == QLatin1String("dup")) {
            if (!m_stack.isEmpty())
                push(m_stack.last());
        } else if (name == QLatin1String("exch")) {
            if (m_stack.size() >= 2) {
                const PsValue a = m_stack.takeLast();
                const PsValue b = m_stack.takeLast();
                m_stack.append(a);
                m_stack.append(b);
            }
        } else if (name == QLatin1String("copy")) {
            const int count = qMax(0, qRound(numberOr(0.0)));
            if (count > 0 && m_stack.size() >= count) {
                const QVector<PsValue> tail = m_stack.mid(m_stack.size() - count);
                for (const PsValue &value : tail)
                    m_stack.append(value);
            }
        } else if (name == QLatin1String("index")) {
            const int index = qRound(numberOr(0.0));
            const int at = m_stack.size() - 1 - index;
            if (at >= 0 && at < m_stack.size())
                push(m_stack.at(at));
        } else if (name == QLatin1String("clear") || name == QLatin1String("cleartomark")) {
            m_stack.clear();
        } else if (name == QLatin1String("count")) {
            PsValue value;
            value.kind = PsValue::Number;
            value.number = m_stack.size();
            push(value);
        } else if (name == QLatin1String("add") || name == QLatin1String("sub")
                   || name == QLatin1String("mul") || name == QLatin1String("div")
                   || name == QLatin1String("idiv") || name == QLatin1String("mod")) {
            const double b = numberOr(0.0);
            const double a = numberOr(0.0);
            double result = 0.0;
            if (name == QLatin1String("add"))
                result = a + b;
            else if (name == QLatin1String("sub"))
                result = a - b;
            else if (name == QLatin1String("mul"))
                result = a * b;
            else if (name == QLatin1String("div"))
                result = b != 0.0 ? a / b : 0.0;
            else if (name == QLatin1String("idiv"))
                result = b != 0.0 ? std::trunc(a / b) : 0.0;
            else
                result = b != 0.0 ? std::fmod(a, b) : 0.0;
            PsValue value;
            value.kind = PsValue::Number;
            value.number = result;
            push(value);
        } else if (name == QLatin1String("neg") || name == QLatin1String("abs")
                   || name == QLatin1String("sqrt") || name == QLatin1String("sin")
                   || name == QLatin1String("cos") || name == QLatin1String("round")
                   || name == QLatin1String("floor")
                   || name == QLatin1String("ceiling")
                   || name == QLatin1String("truncate")) {
            const double a = numberOr(0.0);
            double result = 0.0;
            if (name == QLatin1String("neg"))
                result = -a;
            else if (name == QLatin1String("abs"))
                result = qAbs(a);
            else if (name == QLatin1String("sqrt"))
                result = a > 0.0 ? std::sqrt(a) : 0.0;
            else if (name == QLatin1String("sin"))
                result = std::sin(a * M_PI / 180.0);
            else if (name == QLatin1String("cos"))
                result = std::cos(a * M_PI / 180.0);
            else if (name == QLatin1String("round"))
                result = std::round(a);
            else if (name == QLatin1String("floor"))
                result = std::floor(a);
            else if (name == QLatin1String("ceiling"))
                result = std::ceil(a);
            else
                result = std::trunc(a);
            PsValue value;
            value.kind = PsValue::Number;
            value.number = result;
            push(value);
        } else if (name == QLatin1String("eq") || name == QLatin1String("ne")
                   || name == QLatin1String("gt") || name == QLatin1String("lt")
                   || name == QLatin1String("ge") || name == QLatin1String("le")) {
            const PsValue b = pop();
            const PsValue a = pop();
            bool result = false;
            const double av = a.number;
            const double bv = b.number;
            if (a.kind == PsValue::String && b.kind == PsValue::String) {
                const int cmp = QString::compare(a.text, b.text);
                if (name == QLatin1String("eq"))
                    result = cmp == 0;
                else if (name == QLatin1String("ne"))
                    result = cmp != 0;
                else if (name == QLatin1String("gt"))
                    result = cmp > 0;
                else if (name == QLatin1String("lt"))
                    result = cmp < 0;
                else if (name == QLatin1String("ge"))
                    result = cmp >= 0;
                else
                    result = cmp <= 0;
            } else {
                if (name == QLatin1String("eq"))
                    result = qFuzzyCompare(av + 1.0, bv + 1.0);
                else if (name == QLatin1String("ne"))
                    result = !qFuzzyCompare(av + 1.0, bv + 1.0);
                else if (name == QLatin1String("gt"))
                    result = av > bv;
                else if (name == QLatin1String("lt"))
                    result = av < bv;
                else if (name == QLatin1String("ge"))
                    result = av >= bv;
                else
                    result = av <= bv;
            }
            PsValue value;
            value.kind = PsValue::Boolean;
            value.boolean = result;
            push(value);
        } else if (name == QLatin1String("and") || name == QLatin1String("or")
                   || name == QLatin1String("not")) {
            PsValue value;
            value.kind = PsValue::Boolean;
            if (name == QLatin1String("not")) {
                const PsValue a = pop();
                value.boolean = !(a.kind == PsValue::Boolean ? a.boolean
                                                             : a.number != 0.0);
            } else {
                const PsValue b = pop();
                const PsValue a = pop();
                const bool av = a.kind == PsValue::Boolean ? a.boolean : a.number != 0.0;
                const bool bv = b.kind == PsValue::Boolean ? b.boolean : b.number != 0.0;
                value.boolean = name == QLatin1String("and") ? (av && bv) : (av || bv);
            }
            push(value);
        } else if (name == QLatin1String("def")) {
            const PsValue value = pop();
            const PsValue key = pop();
            if (key.kind == PsValue::Name && !m_dicts.isEmpty())
                (*m_dicts.last())[key.text] = value;
        } else if (name == QLatin1String("load")) {
            const PsValue key = pop();
            if (key.kind == PsValue::Name) {
                if (const PsValue *value = lookup(key.text))
                    push(*value);
            }
        } else if (name == QLatin1String("where")) {
            const PsValue key = pop();
            PsValue value;
            value.kind = PsValue::Boolean;
            value.boolean = key.kind == PsValue::Name && lookup(key.text) != nullptr;
            push(value);
        } else if (name == QLatin1String("dict")) {
            pop();
            PsValue value;
            value.kind = PsValue::Dictionary;
            value.dictionary = QSharedPointer<PsDict>::create();
            push(value);
        } else if (name == QLatin1String("begin")) {
            const PsValue value = pop();
            if (value.kind == PsValue::Dictionary && value.dictionary)
                m_dicts.append(value.dictionary);
            else
                m_dicts.append(QSharedPointer<PsDict>::create());
        } else if (name == QLatin1String("end")) {
            if (m_dicts.size() > 1)
                m_dicts.removeLast();
        } else if (name == QLatin1String("bind")) {
            // no-op: procedures are already executable
        } else if (name == QLatin1String("exec")) {
            const PsValue value = pop();
            if (value.kind == PsValue::Procedure)
                execute(value.items);
            else
                push(value);
        } else if (name == QLatin1String("if")) {
            const PsValue proc = pop();
            const PsValue cond = pop();
            const bool truth = cond.kind == PsValue::Boolean ? cond.boolean
                                                             : cond.number != 0.0;
            if (truth && proc.kind == PsValue::Procedure)
                execute(proc.items);
        } else if (name == QLatin1String("ifelse")) {
            const PsValue elseProc = pop();
            const PsValue thenProc = pop();
            const PsValue cond = pop();
            const bool truth = cond.kind == PsValue::Boolean ? cond.boolean
                                                             : cond.number != 0.0;
            const PsValue &proc = truth ? thenProc : elseProc;
            if (proc.kind == PsValue::Procedure)
                execute(proc.items);
        } else if (name == QLatin1String("for")) {
            const PsValue proc = pop();
            const double limit = numberOr(0.0);
            const double increment = numberOr(0.0);
            const double start = numberOr(0.0);
            if (proc.kind == PsValue::Procedure && increment != 0.0) {
                int guard = 0;
                for (double value = start;
                     (increment > 0.0 ? value <= limit + 1e-9 : value >= limit - 1e-9);
                     value += increment) {
                    PsValue numberValue;
                    numberValue.kind = PsValue::Number;
                    numberValue.number = value;
                    m_stack.append(numberValue);
                    execute(proc.items);
                    if (m_exit) {
                        m_exit = false;
                        break;
                    }
                    if (++guard > 100000)
                        break;
                }
            }
        } else if (name == QLatin1String("repeat")) {
            const PsValue proc = pop();
            const int count = qMax(0, qRound(numberOr(0.0)));
            if (proc.kind == PsValue::Procedure) {
                for (int i = 0; i < count; ++i) {
                    execute(proc.items);
                    if (m_exit) {
                        m_exit = false;
                        break;
                    }
                }
            }
        } else if (name == QLatin1String("exit")) {
            m_exit = true;
        } else if (name == QLatin1String("translate")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            state().ctm = state().ctm.compose(Affine::translation(x, y));
        } else if (name == QLatin1String("scale")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            state().ctm = state().ctm.compose(Affine::scaling(x, y));
        } else if (name == QLatin1String("rotate")) {
            const double angle = numberOr(0.0);
            state().ctm = state().ctm.compose(Affine::rotation(angle));
        } else if (name == QLatin1String("concat")) {
            const PsValue matrix = pop();
            if (matrix.kind == PsValue::Array && matrix.items.size() >= 6) {
                Affine m;
                m.a = matrix.items.at(0).number;
                m.b = matrix.items.at(1).number;
                m.c = matrix.items.at(2).number;
                m.d = matrix.items.at(3).number;
                m.e = matrix.items.at(4).number;
                m.f = matrix.items.at(5).number;
                state().ctm = state().ctm.compose(m);
            }
        } else if (name == QLatin1String("setmatrix")) {
            pop();
        } else if (name == QLatin1String("gsave")) {
            m_states.append(state());
        } else if (name == QLatin1String("grestore")) {
            if (m_states.size() > 1)
                m_states.removeLast();
        } else if (name == QLatin1String("grestoreall")) {
            while (m_states.size() > 1)
                m_states.removeLast();
        } else if (name == QLatin1String("newpath")) {
            m_path = QPainterPath();
            m_hasCurrent = false;
        } else if (name == QLatin1String("moveto")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            m_path.moveTo(x, y);
            m_current = QPointF(x, y);
            m_subpathStart = m_current;
            m_hasCurrent = true;
        } else if (name == QLatin1String("rmoveto")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            moveTo(m_current.x() + x, m_current.y() + y);
        } else if (name == QLatin1String("lineto")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            lineTo(x, y);
        } else if (name == QLatin1String("rlineto")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            lineTo(m_current.x() + x, m_current.y() + y);
        } else if (name == QLatin1String("curveto")) {
            double v[6];
            takeNumbers(6, v);
            curveTo(v[0], v[1], v[2], v[3], v[4], v[5]);
        } else if (name == QLatin1String("rcurveto")) {
            double v[6];
            takeNumbers(6, v);
            curveTo(m_current.x() + v[0], m_current.y() + v[1],
                    m_current.x() + v[2], m_current.y() + v[3],
                    m_current.x() + v[4], m_current.y() + v[5]);
        } else if (name == QLatin1String("closepath")) {
            m_path.closeSubpath();
            m_current = m_subpathStart;
            m_hasCurrent = true;
        } else if (name == QLatin1String("arc")) {
            arc(false);
        } else if (name == QLatin1String("arcn")) {
            arc(true);
        } else if (name == QLatin1String("rectfill")) {
            const double h = numberOr(0.0);
            const double w = numberOr(0.0);
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            m_path = QPainterPath();
            m_path.addRect(QRectF(x, y, w, h));
            appendObject(m_path, true, false, false);
            m_path = QPainterPath();
            m_hasCurrent = false;
        } else if (name == QLatin1String("rectstroke")) {
            const double h = numberOr(0.0);
            const double w = numberOr(0.0);
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            m_path = QPainterPath();
            m_path.addRect(QRectF(x, y, w, h));
            appendObject(m_path, false, false, true);
            m_path = QPainterPath();
            m_hasCurrent = false;
        } else if (name == QLatin1String("fill") || name == QLatin1String("eofill")) {
            appendObject(m_path, true, name == QLatin1String("eofill"), false);
        } else if (name == QLatin1String("stroke")) {
            appendObject(m_path, false, false, true);
        } else if (name == QLatin1String("clip") || name == QLatin1String("eoclip")) {
            // Clipping is approximated by drawing without a clip: EPS artwork
            // stays complete instead of disappearing.
        } else if (name == QLatin1String("setrgbcolor")) {
            const double b = numberOr(0.0);
            const double g = numberOr(0.0);
            const double r = numberOr(0.0);
            state().fill = QColor::fromRgbF(qBound(0.0, r, 1.0),
                                            qBound(0.0, g, 1.0),
                                            qBound(0.0, b, 1.0));
            state().stroke = state().fill;
        } else if (name == QLatin1String("setgray")) {
            const double gray = qBound(0.0, numberOr(0.0), 1.0);
            state().fill = QColor::fromRgbF(gray, gray, gray);
            state().stroke = state().fill;
        } else if (name == QLatin1String("setcmykcolor")) {
            const double k = numberOr(0.0);
            const double y = numberOr(0.0);
            const double m = numberOr(0.0);
            const double c = numberOr(0.0);
            state().fill = QColor::fromCmykF(qBound(0.0, c, 1.0),
                                             qBound(0.0, m, 1.0),
                                             qBound(0.0, y, 1.0),
                                             qBound(0.0, k, 1.0));
            state().stroke = state().fill;
        } else if (name == QLatin1String("setlinewidth")) {
            state().lineWidth = qMax(0.0, numberOr(1.0));
        } else if (name == QLatin1String("setlinecap")) {
            state().cap = qBound(0, qRound(numberOr(0.0)), 2);
        } else if (name == QLatin1String("setlinejoin")) {
            state().join = qBound(0, qRound(numberOr(0.0)), 2);
        } else if (name == QLatin1String("setmiterlimit")) {
            state().miter = qMax(1.0, numberOr(10.0));
        } else if (name == QLatin1String("setdash")) {
            pop();
            pop();
        } else if (name == QLatin1String("findfont")) {
            QString fontName;
            popString(&fontName);
            PsValue font;
            font.kind = PsValue::Font;
            font.fontName = fontName;
            font.fontSize = 1.0;
            push(font);
        } else if (name == QLatin1String("scalefont")) {
            const double size = numberOr(1.0);
            const PsValue font = pop();
            PsValue scaled = font;
            scaled.kind = PsValue::Font;
            scaled.fontSize *= size;
            push(scaled);
        } else if (name == QLatin1String("makefont")) {
            pop();
            const PsValue font = pop();
            push(font);
        } else if (name == QLatin1String("setfont")) {
            const PsValue font = pop();
            if (font.kind == PsValue::Font) {
                state().fontName = font.fontName;
                state().fontSize = font.fontSize;
            }
        } else if (name == QLatin1String("show")) {
            QString text;
            popString(&text);
            showText(text);
        } else if (name == QLatin1String("ashow")) {
            pop();
            QString text;
            popString(&text);
            showText(text);
        } else if (name == QLatin1String("stringwidth")) {
            QString text;
            popString(&text);
            QFont font(mapPostScriptFont(state().fontName));
            font.setPixelSize(qMax(1, qRound(state().fontSize)));
            const QFontMetricsF metrics(font);
            PsValue width;
            width.kind = PsValue::Number;
            width.number = metrics.horizontalAdvance(text);
            push(width);
            push(PsValue());
        } else if (name == QLatin1String("showpage")) {
            // One EPS page; ignore explicit page breaks.
        } else if (name == QLatin1String("image") || name == QLatin1String("colorimage")
                   || name == QLatin1String("imagemask")) {
            for (int i = 0; i < 6; ++i)
                pop();
        } else if (name == QLatin1String("setpagedevice")
                   || name == QLatin1String("sethsbcolor")
                   || name == QLatin1String("setcolor")
                   || name == QLatin1String("setoverprint")
                   || name == QLatin1String("setstrokeadjust")) {
            pop();
        } else if (name == QLatin1String("save")) {
            // no-op
        } else if (name == QLatin1String("restore") || name == QLatin1String("restoreall")) {
            pop();
        } else if (name == QLatin1String("transform")) {
            const double y = numberOr(0.0);
            const double x = numberOr(0.0);
            const QPointF mapped = state().ctm.map(QPointF(x, y));
            PsValue px;
            px.kind = PsValue::Number;
            px.number = mapped.x();
            push(px);
            PsValue py;
            py.kind = PsValue::Number;
            py.number = mapped.y();
            push(py);
        } else {
            // Unknown operator: AI prolog uses many private operators. Ignoring
            // them keeps the common drawing commands flowing.
        }
    }

    Document *m_document = nullptr;
    QVector<PsValue> m_stack;
    QVector<QSharedPointer<PsDict>> m_dicts;
    QVector<GState> m_states;
    QPainterPath m_path;
    QPointF m_current;
    QPointF m_subpathStart;
    bool m_hasCurrent = false;
    int m_pageIndex = 0;
    bool m_exit = false;
    bool m_aborted = false;
    int m_depth = 0;
    int m_steps = 0;
};

struct EpsHeader {
    double llx = 0.0;
    double lly = 0.0;
    double urx = 612.0;
    double ury = 792.0;
    bool valid = false;
};

EpsHeader parseEpsHeader(const QByteArray &data)
{
    EpsHeader header;
    const QString text = QString::fromLatin1(data.left(200000));
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool haveHighRes = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("%%HiResBoundingBox:"))
            || trimmed.startsWith(QLatin1String("%%BoundingBox:"))) {
            const bool highRes = trimmed.startsWith(QLatin1String("%%HiResBoundingBox:"));
            if (highRes && haveHighRes)
                continue;
            const QStringList parts = trimmed.section(QLatin1Char(':'), 1).simplified()
                                          .split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 4
                && !parts.contains(QLatin1String("(atend)"))
                && !parts.contains(QLatin1String("atend"))) {
                const double llx = parts.at(0).toDouble();
                const double lly = parts.at(1).toDouble();
                const double urx = parts.at(2).toDouble();
                const double ury = parts.at(3).toDouble();
                if (urx > llx && ury > lly) {
                    header.llx = llx;
                    header.lly = lly;
                    header.urx = urx;
                    header.ury = ury;
                    header.valid = true;
                    haveHighRes = highRes;
                }
            }
        }
    }
    return header;
}

bool importEmbeddedPreview(const QByteArray &data, const EpsHeader &header,
                           Document *document)
{
    const int begin = data.indexOf("%%BeginPreview:");
    if (begin < 0)
        return false;
    const int lineEnd = data.indexOf('\n', begin);
    if (lineEnd < 0)
        return false;
    const QString spec = QString::fromLatin1(data.mid(begin, lineEnd - begin));
    const QStringList parts = spec.section(QLatin1Char(':'), 1).simplified()
                                  .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return false;
    const int width = parts.at(0).toInt();
    const int height = parts.at(1).toInt();
    const int depth = parts.at(2).toInt();
    if (width <= 0 || height <= 0 || depth <= 0)
        return false;

    QByteArray hex;
    int pos = lineEnd + 1;
    while (pos < data.size()) {
        const int end = data.indexOf('\n', pos);
        const QByteArray line = data.mid(pos, end < 0 ? -1 : end - pos).trimmed();
        pos = end < 0 ? data.size() : end + 1;
        if (line.startsWith("%%EndPreview"))
            break;
        if (line.startsWith('%'))
            continue;
        hex += line;
    }
    hex = hex.simplified();
    hex.replace(" ", "");
    if (hex.size() % 2 == 1)
        hex += '0';
    QByteArray pixels;
    pixels.reserve(hex.size() / 2);
    for (int i = 0; i + 1 < hex.size(); i += 2)
        pixels.append(static_cast<char>(hex.mid(i, 2).toInt(nullptr, 16)));

    const int rowBytes = (width * depth + 7) / 8;
    if (pixels.size() < rowBytes * height)
        return false;

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < height; ++y) {
        const uchar *row = reinterpret_cast<const uchar *>(pixels.constData())
                           + y * rowBytes;
        for (int x = 0; x < width; ++x) {
            int gray = 0;
            if (depth == 1) {
                gray = (row[x / 8] >> (7 - (x % 8))) & 1 ? 255 : 0;
            } else if (depth == 8) {
                gray = row[x];
            } else if (depth == 24) {
                const int index = x * 3;
                image.setPixel(x, y, qRgb(row[index], row[index + 1],
                                          row[index + 2]));
                continue;
            } else {
                return false;
            }
            image.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers = {QStringLiteral("Импорт EPS")};
    document->layerVisible = {true};
    document->layerLocked = {false};
    const double widthPt = header.valid ? header.urx - header.llx : 612.0;
    const double heightPt = header.valid ? header.ury - header.lly : 792.0;
    const QSizeF pageSize(widthPt * 96.0 / 72.0, heightPt * 96.0 / 72.0);
    appendPageImage(document, image, pageSize, 0, QStringLiteral("Страница 1"));
    document->pageSize = pageSize;
    return true;
}

bool renderEpsWithGhostscript(const QString &path, Document *document, QString *error)
{
    const QString gs = QStandardPaths::findExecutable(QStringLiteral("gs"));
    if (gs.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "EPS содержит команды, которые эта сборка не поддерживает. "
                "Установите Ghostscript для сложных EPS-файлов.");
        return false;
    }
    QTemporaryDir temp;
    if (!temp.isValid())
        return false;
    const QString output = temp.filePath(QStringLiteral("eps.png"));
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(gs, {QStringLiteral("-q"), QStringLiteral("-dNOPAUSE"),
                       QStringLiteral("-dBATCH"), QStringLiteral("-dSAFER"),
                       QStringLiteral("-dEPSCrop"), QStringLiteral("-sDEVICE=png16m"),
                       QStringLiteral("-r150"),
                       QStringLiteral("-sOutputFile=%1").arg(output), path});
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("Ghostscript не смог прочитать EPS.");
        return false;
    }
    QImageReader reader(output);
    const QImage image = reader.read();
    if (image.isNull())
        return false;

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers = {QStringLiteral("Импорт EPS")};
    document->layerVisible = {true};
    document->layerLocked = {false};
    const QSizeF pageSize(image.width() * 96.0 / 150.0,
                          image.height() * 96.0 / 150.0);
    appendPageImage(document, image, pageSize, 0, QStringLiteral("Страница 1"));
    document->pageSize = pageSize;
    return true;
}

bool importEpsThroughPdf(const QString &path, Document *document, QString *error)
{
    if (path.isEmpty())
        return false;
    const QString gs = QStandardPaths::findExecutable(QStringLiteral("gs"));
    const QString cairo = QStandardPaths::findExecutable(QStringLiteral("pdftocairo"));
    if (gs.isEmpty() || cairo.isEmpty())
        return false;

    QTemporaryDir temp;
    if (!temp.isValid())
        return false;
    const QString pdf = temp.filePath(QStringLiteral("eps.pdf"));
    const QString svg = temp.filePath(QStringLiteral("eps.svg"));

    QProcess distiller;
    distiller.setStandardInputFile(QProcess::nullDevice());
    distiller.start(gs, {QStringLiteral("-q"), QStringLiteral("-dNOPAUSE"),
                         QStringLiteral("-dBATCH"), QStringLiteral("-dSAFER"),
                         QStringLiteral("-dEPSCrop"),
                         QStringLiteral("-sDEVICE=pdfwrite"),
                         QStringLiteral("-sOutputFile=%1").arg(pdf), path});
    if (!distiller.waitForFinished(60000)
        || distiller.exitStatus() != QProcess::NormalExit
        || distiller.exitCode() != 0)
        return false;

    QProcess converter;
    converter.setStandardInputFile(QProcess::nullDevice());
    converter.start(cairo, {QStringLiteral("-svg"), QStringLiteral("-f"),
                            QStringLiteral("1"), QStringLiteral("-l"),
                            QStringLiteral("1"), pdf, svg});
    if (!converter.waitForFinished(60000)
        || converter.exitStatus() != QProcess::NormalExit
        || converter.exitCode() != 0)
        return false;

    QFile svgFile(svg);
    if (!svgFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray svgData = svgFile.readAll();
    if (svgData.isEmpty())
        return false;

    Document imported;
    QString svgError;
    if (!importSvgData(svgData, &imported, &svgError, false)
        || imported.objects.isEmpty())
        return false;

    imported.pages.clear();
    PageInfo page;
    page.name = QStringLiteral("Страница 1");
    page.size = imported.pageSize;
    page.background = QColor(Qt::white);
    imported.pages.append(page);
    imported.layers = {QStringLiteral("Импорт EPS")};
    imported.layerVisible = {true};
    imported.layerLocked = {false};
    imported.currentPage = 0;
    for (VectorObject &object : imported.objects) {
        object.page = 0;
        object.layer = 0;
        object.id = 0;
        object.group = 0;
    }
    *document = imported;
    if (error)
        error->clear();
    return true;
}

bool importPostScriptData(const QByteArray &data, const QString &path, Document *document,
                          QString *error)
{
    if (error)
        error->clear();

    // Ghostscript + pdftocairo give a much richer editable result than the
    // built-in PostScript subset: text, clips and complex curves survive.
    if (importEpsThroughPdf(path, document, error))
        return true;

    const EpsHeader header = parseEpsHeader(data);
    const double widthPt = header.urx - header.llx;
    const double heightPt = header.ury - header.lly;
    const double scale = 96.0 / 72.0;
    const QSizeF pageSize(qBound(1.0, widthPt * scale, 40000.0),
                          qBound(1.0, heightPt * scale, 40000.0));

    document->objects.clear();
    document->pages.clear();
    document->currentPage = 0;
    document->layers = {QStringLiteral("Импорт EPS")};
    document->layerVisible = {true};
    document->layerLocked = {false};

    Affine base;
    base.a = scale;
    base.d = -scale;
    base.e = -header.llx * scale;
    base.f = header.ury * scale;

    PsTokenizer tokenizer(data);
    PsMachine machine(document, base, 0);
    machine.run(tokenizer.parse());

    // The vector interpreter does not decode PostScript raster images. When an
    // EPS embeds one, Ghostscript renders a complete page instead of returning
    // partial artwork without the pictures.
    if (document->objects.isEmpty()) {
        if (!path.isEmpty()
            && renderEpsWithGhostscript(path, document, error))
            return true;
        if (importEmbeddedPreview(data, header, document))
            return true;
        if (error && error->isEmpty())
            *error = QStringLiteral("В EPS не найдено векторных команд для импорта.");
        return false;
    }

    PageInfo info;
    info.name = QStringLiteral("Страница 1");
    info.size = pageSize;
    document->pages.append(info);
    document->pageSize = pageSize;
    return true;
}

bool importAiData(const QByteArray &data, const QString &path, Document *document,
                  QString *error)
{
    if (importPdfVectorWithTool(path, document, error))
        return true;

    const int pdfPos = data.indexOf("%PDF-");
    if (pdfPos >= 0) {
#if defined(VECTORA_HAVE_QT_PDF)
        QString pdfError;
        // Illustrator stores the PDF header at offset 0 in modern files and
        // after a short PostScript preamble in older ones.
        if (importPdfData(data, document, &pdfError))
            return true;
        if (pdfPos > 0 && importPdfData(data.mid(pdfPos), document, &pdfError))
            return true;
#endif
    }
    // Older Illustrator files are plain PostScript.
    return importPostScriptData(data, path, document, error);
}

// ---------------------------------------------------------------------------
// EPS export
// ---------------------------------------------------------------------------

QString postScriptPath(const QPainterPath &path)
{
    QString out;
    bool open = false;
    QPointF start;
    QPointF current;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        if (element.type == QPainterPath::MoveToElement) {
            if (open && QLineF(current, start).length() < 0.05)
                out += QStringLiteral("closepath\n");
            out += QStringLiteral("%1 %2 moveto\n")
                       .arg(number(element.x), number(element.y));
            start = QPointF(element.x, element.y);
            current = start;
            open = true;
        } else if (element.type == QPainterPath::LineToElement) {
            out += QStringLiteral("%1 %2 lineto\n")
                       .arg(number(element.x), number(element.y));
            current = QPointF(element.x, element.y);
        } else if (element.type == QPainterPath::CurveToElement) {
            if (i + 2 < path.elementCount()) {
                const QPainterPath::Element control2 = path.elementAt(i + 1);
                const QPainterPath::Element end = path.elementAt(i + 2);
                out += QStringLiteral("%1 %2 %3 %4 %5 %6 curveto\n")
                           .arg(number(element.x), number(element.y),
                                number(control2.x), number(control2.y),
                                number(end.x), number(end.y));
                current = QPointF(end.x, end.y);
                i += 2;
            }
        }
    }
    if (open && QLineF(current, start).length() < 0.05)
        out += QStringLiteral("closepath\n");
    return out;
}

QString hexChunk(const QByteArray &data)
{
    QString out;
    const QString hex = QString::fromLatin1(data.toHex());
    for (int i = 0; i < hex.size(); i += 78)
        out += hex.mid(i, 78) + QLatin1Char('\n');
    return out;
}

QString postScriptImage(const QImage &source, const QRectF &rect)
{
    if (source.isNull() || rect.width() <= 0.5 || rect.height() <= 0.5)
        return QString();
    QImage image = source;
    const int limit = 1400;
    if (qMax(image.width(), image.height()) > limit)
        image = image.scaled(limit, limit, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    if (image.hasAlphaChannel()) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter composer(&flat);
        composer.drawImage(0, 0, image);
        composer.end();
        image = flat;
    } else if (image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QByteArray rgb;
    rgb.reserve(image.width() * image.height() * 3);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            rgb.append(static_cast<char>(qRed(line[x])));
            rgb.append(static_cast<char>(qGreen(line[x])));
            rgb.append(static_cast<char>(qBlue(line[x])));
        }
    }

    QString out;
    out += QStringLiteral("gsave\n");
    out += QStringLiteral("/DeviceRGB setcolorspace\n");
    out += QStringLiteral("%1 %2 translate\n")
               .arg(number(rect.left()), number(rect.top()));
    out += QStringLiteral("%1 %2 scale\n")
               .arg(number(rect.width()), number(rect.height()));
    out += QStringLiteral("/picstr %1 string def\n").arg(image.width() * 3);
    out += QStringLiteral("%1 %2 8 [%1 0 0 -%2 0 %2] "
                          "{ currentfile picstr readhexstring pop } false 3 colorimage\n")
               .arg(image.width())
               .arg(image.height());
    out += hexChunk(rgb);
    out += QStringLiteral("grestore\n");
    return out;
}

QString postScriptObject(const VectorObject &object)
{
    const bool raster = hasLiveEffects(object) || object.shadow
                        || object.type == ShapeType::Dimension
                        || object.type == ShapeType::Code;

    QString out;
    out += QStringLiteral("gsave\n");
    if (!qFuzzyIsNull(object.pos.x()) || !qFuzzyIsNull(object.pos.y()))
        out += QStringLiteral("%1 %2 translate\n")
                   .arg(number(object.pos.x()), number(object.pos.y()));
    if (!qFuzzyIsNull(object.rotation)) {
        const QRectF bounds = object.localBounds();
        out += QStringLiteral("%1 %2 translate\n")
                   .arg(number(bounds.center().x()), number(bounds.center().y()));
        out += QStringLiteral("%1 rotate\n").arg(number(object.rotation));
        out += QStringLiteral("%1 %2 translate\n")
                   .arg(number(-bounds.center().x()), number(-bounds.center().y()));
    }

    if (object.type == ShapeType::Image && !object.image.isNull() && !raster) {
        out += postScriptImage(object.image, object.textBox);
        out += QStringLiteral("grestore\n");
        return out;
    }

    if (raster) {
        const QRectF area = effectBounds(object).adjusted(-2.0, -2.0, 2.0, 2.0);
        if (area.width() < 1.0 || area.height() < 1.0) {
            out += QStringLiteral("grestore\n");
            return out;
        }
        qreal scale = 2.0;
        const qreal maxDimension = qMax(area.width(), area.height());
        if (maxDimension * scale > 2400.0)
            scale = 2400.0 / maxDimension;
        QImage layer(qMax(1, qRound(area.width() * scale)),
                     qMax(1, qRound(area.height() * scale)),
                     QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        {
            QPainter painter(&layer);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            painter.scale(scale, scale);
            painter.translate(-area.left(), -area.top());
            Document::paintObject(&painter, object);
        }
        out += postScriptImage(layer, area);
        out += QStringLiteral("grestore\n");
        return out;
    }

    QPainterPath geometry;
    if (object.type == ShapeType::Text && !object.text.isEmpty())
        geometry = buildTextPath(object);
    else
        geometry = object.path;
    if (geometry.isEmpty() && object.type == ShapeType::Image)
        geometry.addRect(object.textBox);

    if (!geometry.isEmpty()) {
        // The PostScript path is global state: fill/stroke keep it alive, so
        // every object must start a fresh path or shapes would stack up.
        QString pathMarkup = QStringLiteral("newpath\n") + postScriptPath(geometry);
        if (object.hasFill && object.fillOpacity > 0.001 && object.opacity > 0.001) {
            const QColor color = overWhite(solidFillColor(object),
                                           object.opacity * object.fillOpacity);
            out += QStringLiteral("%1 %2 %3 setrgbcolor\n")
                       .arg(number(color.redF()), number(color.greenF()),
                            number(color.blueF()));
            out += pathMarkup;
            out += QStringLiteral("fill\n");
        }
        if (object.hasStroke && object.strokeWidth > 0.0
            && object.strokeOpacity > 0.001 && object.opacity > 0.001) {
            const QColor color = object.stroke;
            out += QStringLiteral("%1 %2 %3 setrgbcolor\n")
                       .arg(number(color.redF()), number(color.greenF()),
                            number(color.blueF()));
            out += QStringLiteral("%1 setlinewidth\n").arg(number(object.strokeWidth));
            if (object.capStyle == 1)
                out += QStringLiteral("1 setlinecap\n");
            else if (object.capStyle == 2)
                out += QStringLiteral("2 setlinecap\n");
            if (object.joinStyle == 1)
                out += QStringLiteral("1 setlinejoin\n");
            else if (object.joinStyle == 2)
                out += QStringLiteral("2 setlinejoin\n");
            out += pathMarkup;
            out += QStringLiteral("stroke\n");
        }
    }
    out += QStringLiteral("grestore\n");
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Public dispatch
// ---------------------------------------------------------------------------

Format formatForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("vct"))
        return Format::Vectora;
    if (suffix == QLatin1String("vectorcraft"))
        return Format::Vectorcraft;
    if (suffix == QLatin1String("pdf"))
        return Format::Pdf;
    if (suffix == QLatin1String("eps") || suffix == QLatin1String("esp")
        || suffix == QLatin1String("ps"))
        return Format::Eps;
    if (suffix == QLatin1String("ai"))
        return Format::Ai;
    if (suffix == QLatin1String("cdr"))
        return Format::Cdr;
    if (suffix == QLatin1String("cmx"))
        return Format::Cmx;
    if (suffix == QLatin1String("svg"))
        return Format::Svg;
    return Format::Unknown;
}

QString formatName(Format format)
{
    switch (format) {
    case Format::Vectora: return QStringLiteral("Vectora");
    case Format::Vectorcraft: return QStringLiteral("Vectorcraft");
    case Format::Pdf: return QStringLiteral("PDF");
    case Format::Eps: return QStringLiteral("EPS");
    case Format::Ai: return QStringLiteral("Adobe Illustrator");
    case Format::Cdr: return QStringLiteral("CorelDRAW");
    case Format::Cmx: return QStringLiteral("Corel CMX");
    case Format::Svg: return QStringLiteral("SVG");
    default: return QStringLiteral("Неизвестный формат");
    }
}

QString extensionFor(Format format)
{
    switch (format) {
    case Format::Vectora: return QStringLiteral("vct");
    case Format::Vectorcraft: return QStringLiteral("vectorcraft");
    case Format::Pdf: return QStringLiteral("pdf");
    case Format::Eps: return QStringLiteral("eps");
    case Format::Ai: return QStringLiteral("ai");
    case Format::Cdr: return QStringLiteral("cdr");
    case Format::Cmx: return QStringLiteral("cmx");
    case Format::Svg: return QStringLiteral("svg");
    default: return QString();
    }
}

bool isDocumentFormat(Format format)
{
    return format == Format::Vectora;
}

bool canImport(Format format)
{
    return format != Format::Unknown;
}

bool canExport(Format format)
{
    // CDR is a closed, read-only format: libcdr can parse it but no
    // open-source library can write it. CMX writing is still in development:
    // the generated files are rejected by CorelDRAW, so export stays disabled.
    return format != Format::Unknown && format != Format::Cdr
           && format != Format::Cmx;
}

QString openFilter()
{
    return QStringLiteral(
               "Все поддерживаемые файлы (*.vct *.vectorcraft *.pdf *.ai *.eps *.esp *.cdr *.cmx *.svg);;"
               "Документы Vectora (*.vct *.vectorcraft);;"
               "PDF (*.pdf);;Adobe Illustrator (*.ai);;"
               "EPS/PostScript (*.eps *.esp);;CorelDRAW (*.cdr);;Corel CMX (*.cmx);;"
               "SVG (*.svg);;Все файлы (*)");
}

QString importFilter()
{
    return QStringLiteral(
               "Изображения и вектор (*.png *.jpg *.jpeg *.bmp *.svg *.pdf *.ai *.eps *.esp *.cdr *.cmx);;"
               "PDF (*.pdf);;Adobe Illustrator (*.ai);;EPS/PostScript (*.eps *.esp);;"
               "SVG и изображения (*.svg *.png *.jpg *.jpeg *.bmp);;"
               "CorelDRAW (*.cdr);;Corel CMX (*.cmx);;Все файлы (*)");
}

QString exportFilter()
{
    return QStringLiteral(
               "PDF (*.pdf);;Adobe Illustrator (*.ai);;EPS/ESP (*.eps *.esp);;"
               "Corel CMX (*.cmx) — в разработке;;SVG (*.svg);;PNG (*.png);;"
               "Vectorcraft (*.vectorcraft)");
}

QString saveFilter()
{
    return QStringLiteral(
               "Документы Vectora (*.vct);;Vectorcraft (*.vectorcraft)");
}

bool importDocument(const QString &path, Document *document, QString *error)
{
    if (error)
        error->clear();
    const Format format = formatForPath(path);
    if (isDocumentFormat(format))
        return document->load(path, error);
    if (format == Format::Cdr)
        return CdrIO::importFile(path, document, error);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл: %1")
                         .arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();

    switch (format) {
    case Format::Pdf:
        // Poppler's pdftocairo gives editable vector paths when installed;
        // Qt PDF (or Ghostscript) renders a bitmap otherwise.
        if (importPdfVectorWithTool(path, document, error))
            return true;
#if defined(VECTORA_HAVE_QT_PDF)
        if (importPdfData(data, document, error))
            return true;
#endif
        return importPdfWithTool(path, document, error);
    case Format::Ai:
        return importAiData(data, path, document, error);
    case Format::Eps:
        return importPostScriptData(data, path, document, error);
    case Format::Cdr:
        return CdrIO::importFile(path, document, error);
    case Format::Cmx:
        return CmxIO::importFile(path, document, error);
    case Format::Svg: {
        if (!importSvgFile(path, document, error))
            return false;
        if (document->pages.isEmpty()) {
            PageInfo page;
            page.name = QStringLiteral("Страница 1");
            page.size = document->pageSize;
            document->pages.append(page);
        }
        if (document->layers.isEmpty()) {
            document->layers.append(QStringLiteral("Слой 1"));
            document->layerVisible.append(true);
            document->layerLocked.append(false);
        }
        for (VectorObject &object : document->objects)
            object.page = 0;
        return true;
    }
    case Format::Vectorcraft:
        if (VectorCraftIO::isVectorCraftData(data))
            return VectorCraftIO::importFile(path, document, error);
        // Older Vectora builds used .vectorcraft as an alias for .vct.
        return document->load(path, error);
    default:
        if (QFileInfo(path).suffix().compare(QLatin1String("svg"),
                                             Qt::CaseInsensitive) == 0) {
            if (!importSvgFile(path, document, error))
                return false;
            if (document->pages.isEmpty()) {
                PageInfo page;
                page.name = QStringLiteral("Страница 1");
                page.size = document->pageSize;
                document->pages.append(page);
            }
            if (document->layers.isEmpty()) {
                document->layers.append(QStringLiteral("Слой 1"));
                document->layerVisible.append(true);
                document->layerLocked.append(false);
            }
            for (VectorObject &object : document->objects)
                object.page = 0;
            return true;
        }
        if (error)
            *error = QStringLiteral("Формат файла не поддерживается: %1")
                         .arg(QFileInfo(path).suffix());
        return false;
    }
}

bool exportDocument(const Document &document, const QString &path, QString *error)
{
    if (error)
        error->clear();
    switch (formatForPath(path)) {
    case Format::Vectora:
        return document.save(path, error);
    case Format::Vectorcraft:
        return VectorCraftIO::exportFile(document, path, error);
    case Format::Pdf:
        return writePdf(document, path, error);
    case Format::Ai:
        return writeAi(document, path, error);
    case Format::Eps:
        return writeEps(document, path, error);
    case Format::Cmx:
        return CmxIO::exportFile(document, path, error);
    case Format::Cdr:
        if (error)
            *error = QStringLiteral(
                "CDR — формат только для чтения: открыть и отредактировать "
                "его можно, а сохранить обратно в CDR нельзя. "
                "Экспортируйте в PDF, AI, EPS, SVG или VectorCraft.");
        return false;
    default:
        if (error)
            *error = QStringLiteral("Неизвестное расширение файла: %1")
                         .arg(QFileInfo(path).suffix());
        return false;
    }
}

bool writePdf(const Document &document, const QString &path, QString *error)
{
    return writePdfInternal(document, path, false, error);
}

bool writeAi(const Document &document, const QString &path, QString *error)
{
    // Illustrator 9 and newer read PDF-compatible files with an .ai suffix.
    return writePdfInternal(document, path, true, error);
}

QByteArray epsData(const Document &document)
{
    const Document source = document;
    const QSizeF pageSize = source.pageSize;
    const qreal widthPt = pageSize.width() * 72.0 / 96.0;
    const qreal heightPt = pageSize.height() * 72.0 / 96.0;

    QString out;
    out += QStringLiteral("%!PS-Adobe-3.0 EPSF-3.0\n");
    out += QStringLiteral("%%Creator: Vectora\n");
    out += QStringLiteral("%%CreationDate: %1\n")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out += QStringLiteral("%%BoundingBox: 0 0 %1 %2\n")
               .arg(qCeil(widthPt))
               .arg(qCeil(heightPt));
    out += QStringLiteral("%%HiResBoundingBox: 0 0 %1 %2\n")
               .arg(number(widthPt), number(heightPt));
    out += QStringLiteral("%%LanguageLevel: 2\n");
    out += QStringLiteral("%%Pages: 1\n");
    out += QStringLiteral("%%DocumentData: Clean7Bit\n");
    out += QStringLiteral("%%EndComments\n");
    out += QStringLiteral("%%Page: 1 1\n");
    out += QStringLiteral("save\n");
    out += QStringLiteral("0 %1 translate\n").arg(number(heightPt));
    out += QStringLiteral("0.75 -0.75 scale\n");

    for (const VectorObject &object : source.objects) {
        if (!objectVisibleOnPage(source, object))
            continue;
        out += postScriptObject(object);
    }

    out += QStringLiteral("restore\n");
    out += QStringLiteral("showpage\n");
    out += QStringLiteral("%%EOF\n");
    return out.toLatin1();
}

bool writeEps(const Document &document, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл для записи: %1")
                         .arg(file.errorString());
        return false;
    }
    const QByteArray data = epsData(document);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить EPS: %1")
                         .arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace VectorFormats
