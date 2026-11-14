#include "TemplateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

#include "Document.h"

namespace {

using ObjectList = QVector<VectorObject>;

VectorObject makeText(const QString &text, const QRectF &box, int pixelSize,
                      const QColor &color, bool bold = false, int align = 0)
{
    VectorObject object;
    object.type = ShapeType::Text;
    object.text = text;
    object.pos = box.topLeft();
    object.textBox = QRectF(0, 0, box.width(), box.height());
    object.fill = color;
    object.hasFill = true;
    object.hasStroke = false;
    object.textAlign = align;
    QFont font(QStringLiteral("Adwaita Sans"));
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    object.font = font;
    return object;
}

VectorObject makeRect(const QRectF &box, const QColor &fill, qreal radius = 0.0,
                      bool stroke = false, const QColor &color = Qt::black)
{
    VectorObject object;
    object.type = ShapeType::Rectangle;
    object.path = buildRectanglePath(QRectF(0, 0, box.width(), box.height()), radius);
    object.pos = box.topLeft();
    object.fill = fill;
    object.hasFill = true;
    object.hasStroke = stroke;
    object.stroke = color;
    object.strokeWidth = 1.5;
    return object;
}

VectorObject makeEllipse(const QRectF &box, const QColor &fill)
{
    VectorObject object;
    object.type = ShapeType::Ellipse;
    object.path.addEllipse(QRectF(0, 0, box.width(), box.height()));
    object.pos = box.topLeft();
    object.fill = fill;
    object.hasFill = true;
    object.hasStroke = false;
    return object;
}

QJsonObject makeDocument(const QSizeF &pageSize, const QColor &background,
                         const ObjectList &objects)
{
    Document document;
    document.pageSize = pageSize;
    document.pages = {PageInfo{QStringLiteral("Страница 1"), pageSize, background,
                               37.8, 11.34, -1}};
    document.currentPage = 0;
    document.objects = objects;
    return document.toJson();
}

QJsonObject logoSheet()
{
    ObjectList objects;
    objects << makeRect(QRectF(80, 70, 1040, 660), QColor("#f5f7fa"), 18);
    objects << makeEllipse(QRectF(150, 170, 190, 190), QColor("#00c987"));
    objects << makeRect(QRectF(225, 225, 40, 80), QColor("#f5f7fa"), 6);
    objects << makeText(QStringLiteral("VECTORA"), QRectF(390, 185, 620, 90), 76,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("знак · логотип · фирменный стиль"),
                        QRectF(392, 285, 620, 40), 22, QColor("#5b6472"));
    objects << makeText(QStringLiteral("Основной знак"), QRectF(160, 420, 300, 30), 16,
                        QColor("#5b6472"));
    const QColor swatches[] = {QColor("#00c987"), QColor("#4f7cc0"), QColor("#ffb547"),
                               QColor("#e0466b")};
    for (int i = 0; i < 4; ++i)
        objects << makeRect(QRectF(160 + i * 200, 470, 160, 160), swatches[i], 12);
    return makeDocument(QSizeF(1200, 800), QColor("#ffffff"), objects);
}

QJsonObject businessCard()
{
    ObjectList objects;
    objects << makeRect(QRectF(30, 30, 280, 130), QColor("#1d2127"), 10);
    objects << makeEllipse(QRectF(52, 52, 54, 54), QColor("#00c987"));
    objects << makeText(QStringLiteral("Анна Орлова"), QRectF(52, 196, 260, 34), 24,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("дизайнер"), QRectF(52, 230, 220, 24), 15,
                        QColor("#00c987"));
    objects << makeText(QStringLiteral("+7 900 000-00-00\nanna@example.com\nvectora.app"),
                        QRectF(30, 270, 300, 90), 13, QColor("#5b6472"));
    return makeDocument(QSizeF(340, 400), QColor("#ffffff"), objects);
}

QJsonObject poster()
{
    ObjectList objects;
    objects << makeRect(QRectF(0, 0, 1123, 1587), QColor("#111318"));
    objects << makeEllipse(QRectF(680, 120, 380, 380), QColor("#ffb547"));
    objects << makeEllipse(QRectF(120, 980, 520, 520), QColor("#4f7cc0"));
    objects << makeText(QStringLiteral("НОВЫЙ\nВЗГЛЯД"), QRectF(90, 180, 860, 360), 128,
                        QColor("#ffffff"), true);
    objects << makeText(QStringLiteral("выставка векторной графики\n12 — 28 июня"),
                        QRectF(95, 610, 700, 100), 34, QColor("#b9c2d0"));
    objects << makeRect(QRectF(95, 760, 240, 8), QColor("#00c987"));
    return makeDocument(QSizeF(1123, 1587), QColor("#111318"), objects);
}

QJsonObject booklet()
{
    ObjectList objects;
    objects << makeRect(QRectF(40, 40, 720, 714), QColor("#f5f7fa"), 12);
    objects << makeRect(QRectF(790, 40, 290, 714), QColor("#00c987"), 12);
    objects << makeText(QStringLiteral("Буклет"), QRectF(90, 90, 600, 80), 54,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("Разворот A5 · две панели · поля и вылеты "
                                       "уже настроены."),
                        QRectF(92, 190, 600, 120), 22, QColor("#5b6472"));
    objects << makeText(QStringLiteral("01"), QRectF(830, 90, 200, 80), 54,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("Содержание\n\n· Обложка\n· Разворот\n· Контакты"),
                        QRectF(830, 200, 200, 260), 18, QColor("#1d2127"));
    return makeDocument(QSizeF(1123, 794), QColor("#ffffff"), objects);
}

QJsonObject illustration()
{
    ObjectList objects;
    objects << makeRect(QRectF(0, 0, 1000, 1000), QColor("#eef1f5"));
    objects << makeEllipse(QRectF(140, 150, 420, 420), QColor("#ffb547"));
    objects << makeEllipse(QRectF(420, 380, 460, 460), QColor("#4f7cc0"));
    objects << makeRect(QRectF(260, 620, 480, 90), QColor("#e0466b"), 40);
    objects << makeText(QStringLiteral("форма · цвет · ритм"), QRectF(140, 820, 720, 60),
                        32, QColor("#1d2127"), true);
    return makeDocument(QSizeF(1000, 1000), QColor("#eef1f5"), objects);
}

QJsonObject socialPost()
{
    ObjectList objects;
    objects << makeRect(QRectF(0, 0, 1080, 1080), QColor("#4f7cc0"));
    objects << makeEllipse(QRectF(620, 120, 360, 360), QColor("#00c987"));
    objects << makeText(QStringLiteral("НОВЫЙ\nПОСТ"), QRectF(90, 150, 640, 260), 110,
                        QColor("#ffffff"), true);
    objects << makeText(QStringLiteral("Vectora · 1080 × 1080"), QRectF(95, 820, 700, 40),
                        28, QColor("#dfe7f5"));
    objects << makeRect(QRectF(95, 460, 180, 10), QColor("#ffb547"));
    return makeDocument(QSizeF(1080, 1080), QColor("#4f7cc0"), objects);
}

QJsonObject packaging()
{
    ObjectList objects;
    objects << makeRect(QRectF(80, 60, 840, 580), QColor("#f5f7fa"), 10, true,
                        QColor("#9aa1ac"));
    objects << makeRect(QRectF(80, 380, 840, 260), QColor("#ffb547"), 0, true,
                        QColor("#9aa1ac"));
    objects << makeText(QStringLiteral("УПАКОВКА"), QRectF(120, 120, 600, 70), 48,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("развёртка · линии сгиба · вылеты"),
                        QRectF(122, 200, 600, 40), 20, QColor("#5b6472"));
    objects << makeText(QStringLiteral("Верхняя панель"), QRectF(120, 420, 400, 30), 18,
                        QColor("#1d2127"));
    return makeDocument(QSizeF(1000, 700), QColor("#ffffff"), objects);
}

QJsonObject label()
{
    ObjectList objects;
    objects << makeRect(QRectF(30, 30, 540, 340), QColor("#ffffff"), 12, true,
                        QColor("#1d2127"));
    objects << makeText(QStringLiteral("VECTORA"), QRectF(60, 60, 420, 50), 38,
                        QColor("#1d2127"), true);
    objects << makeText(QStringLiteral("этикетка продукта"), QRectF(62, 112, 380, 26),
                        16, QColor("#00c987"));
    objects << makeText(QStringLiteral("Состав: графика, цвет, кривые.\nМасса: 210 г\n"
                                       "Срок: бессрочно"),
                        QRectF(62, 170, 400, 100), 14, QColor("#5b6472"));
    objects << makeRect(QRectF(430, 270, 110, 70), QColor("#1d2127"), 4);
    return makeDocument(QSizeF(600, 400), QColor("#ffffff"), objects);
}

QJsonObject presentation()
{
    ObjectList objects;
    objects << makeRect(QRectF(0, 0, 1920, 1080), QColor("#111318"));
    objects << makeText(QStringLiteral("Презентация"), QRectF(120, 160, 1100, 110), 82,
                        QColor("#ffffff"), true);
    objects << makeText(QStringLiteral("Слайд 1 · введение"), QRectF(122, 290, 700, 40),
                        28, QColor("#00c987"));
    objects << makeEllipse(QRectF(1420, 160, 340, 340), QColor("#4f7cc0"));
    objects << makeText(QStringLiteral("Тезис\nВторой тезис\nТретий тезис"),
                        QRectF(122, 480, 900, 240), 34, QColor("#b9c2d0"));
    objects << makeRect(QRectF(122, 820, 260, 10), QColor("#ffb547"));
    return makeDocument(QSizeF(1920, 1080), QColor("#111318"), objects);
}

QJsonObject templateToJson(const DocumentTemplate &documentTemplate)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), documentTemplate.id);
    entry.insert(QStringLiteral("name"), documentTemplate.name);
    entry.insert(QStringLiteral("category"), documentTemplate.category);
    entry.insert(QStringLiteral("description"), documentTemplate.description);
    entry.insert(QStringLiteral("favorite"), documentTemplate.favorite);
    entry.insert(QStringLiteral("builtIn"), documentTemplate.builtIn);
    entry.insert(QStringLiteral("document"), documentTemplate.document);
    return entry;
}

DocumentTemplate templateFromJson(const QJsonObject &entry)
{
    DocumentTemplate documentTemplate;
    documentTemplate.id = entry.value(QStringLiteral("id")).toString();
    documentTemplate.name = entry.value(QStringLiteral("name"))
                                .toString(QStringLiteral("Шаблон"));
    documentTemplate.category = entry.value(QStringLiteral("category"))
                                    .toString(QStringLiteral("Пользовательские"));
    documentTemplate.description =
        entry.value(QStringLiteral("description")).toString();
    documentTemplate.favorite =
        entry.value(QStringLiteral("favorite")).toBool(false);
    documentTemplate.builtIn = entry.value(QStringLiteral("builtIn")).toBool(false);
    documentTemplate.document = entry.value(QStringLiteral("document")).toObject();
    if (documentTemplate.id.isEmpty())
        documentTemplate.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return documentTemplate;
}

} // namespace

TemplateStore::TemplateStore() = default;

TemplateStore &TemplateStore::instance()
{
    static TemplateStore store;
    return store;
}

QString TemplateStore::templateExtension()
{
    return QStringLiteral("vtpl");
}

QString TemplateStore::storageFile() const
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/templates.json");
}

QStringList TemplateStore::builtinCategories()
{
    return {QStringLiteral("Логотипы"), QStringLiteral("Визитки"),
            QStringLiteral("Плакаты"), QStringLiteral("Буклеты"),
            QStringLiteral("Иллюстрации"), QStringLiteral("Социальные сети"),
            QStringLiteral("Упаковка"), QStringLiteral("Этикетки"),
            QStringLiteral("Презентации"), QStringLiteral("Пользовательские")};
}

void TemplateStore::ensureBuiltins()
{
    if (!m_templates.isEmpty())
        return;
    struct Builtin {
        const char *id;
        const char *name;
        const char *category;
        const char *description;
        QJsonObject (*document)();
    };
    const Builtin builtins[] = {
        {"builtin-logo", "Логотип и фирменный стиль", "Логотипы",
         "Знак, название и палитра бренда", logoSheet},
        {"builtin-card", "Визитка дизайнера", "Визитки",
         "Визитка 90×50 мм с контактами", businessCard},
        {"builtin-poster", "Плакат выставки", "Плакаты",
         "A3, тёмный фон и крупная типографика", poster},
        {"builtin-booklet", "Буклет A5", "Буклеты",
         "Разворот с двумя панелями", booklet},
        {"builtin-illustration", "Абстрактная иллюстрация", "Иллюстрации",
         "Геометрическая композиция 1000×1000", illustration},
        {"builtin-social", "Пост для соцсетей", "Социальные сети",
         "Квадрат 1080×1080", socialPost},
        {"builtin-packaging", "Развёртка упаковки", "Упаковка",
         "Передняя и нижняя панели", packaging},
        {"builtin-label", "Этикетка продукта", "Этикетки",
         "Этикетка 600×400 px", label},
        {"builtin-presentation", "Слайд презентации", "Презентации",
         "16:9, тёмная тема", presentation},
    };
    for (const Builtin &builtin : builtins) {
        DocumentTemplate documentTemplate;
        documentTemplate.id = QString::fromLatin1(builtin.id);
        documentTemplate.name = QString::fromUtf8(builtin.name);
        documentTemplate.category = QString::fromUtf8(builtin.category);
        documentTemplate.description = QString::fromUtf8(builtin.description);
        documentTemplate.builtIn = true;
        documentTemplate.document = builtin.document();
        m_templates.append(documentTemplate);
    }
}

void TemplateStore::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    ensureBuiltins();

    QFile file(storageFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value :
         root.value(QStringLiteral("templates")).toArray()) {
        DocumentTemplate documentTemplate = templateFromJson(value.toObject());
        documentTemplate.builtIn = false;
        if (!documentTemplate.document.isEmpty())
            m_templates.append(documentTemplate);
    }
}

bool TemplateStore::save(QString *error)
{
    QJsonArray array;
    for (const DocumentTemplate &documentTemplate : m_templates) {
        if (!documentTemplate.builtIn)
            array.append(templateToJson(documentTemplate));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-templates"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("templates"), array);
    QFile file(storageFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

const DocumentTemplate &TemplateStore::at(int index) const
{
    static const DocumentTemplate empty;
    if (index < 0 || index >= m_templates.size())
        return empty;
    return m_templates.at(index);
}

DocumentTemplate *TemplateStore::at(int index)
{
    if (index < 0 || index >= m_templates.size())
        return nullptr;
    return &m_templates[index];
}

int TemplateStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_templates.size(); ++i) {
        if (m_templates.at(i).id == id)
            return i;
    }
    return -1;
}

QStringList TemplateStore::categories() const
{
    QStringList result = builtinCategories();
    for (const DocumentTemplate &documentTemplate : m_templates) {
        if (!result.contains(documentTemplate.category))
            result.append(documentTemplate.category);
    }
    return result;
}

int TemplateStore::add(const DocumentTemplate &documentTemplate)
{
    if (documentTemplate.document.isEmpty())
        return -1;
    DocumentTemplate copy = documentTemplate;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.builtIn = false;
    m_templates.append(copy);
    save();
    emit changed();
    return m_templates.size() - 1;
}

bool TemplateStore::remove(int index)
{
    if (index < 0 || index >= m_templates.size()
        || m_templates.at(index).builtIn)
        return false;
    m_templates.removeAt(index);
    save();
    emit changed();
    return true;
}

void TemplateStore::toggleFavorite(int index)
{
    if (index < 0 || index >= m_templates.size())
        return;
    m_templates[index].favorite = !m_templates.at(index).favorite;
    save();
    emit changed();
}

bool TemplateStore::importTemplate(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    DocumentTemplate documentTemplate = templateFromJson(root);
    if (documentTemplate.document.isEmpty()) {
        if (error)
            *error = QStringLiteral("Файл не содержит шаблон Vectora.");
        return false;
    }
    if (documentTemplate.name.isEmpty())
        documentTemplate.name = QFileInfo(path).completeBaseName();
    add(documentTemplate);
    return true;
}

bool TemplateStore::exportTemplate(int index, const QString &path,
                                   QString *error) const
{
    if (index < 0 || index >= m_templates.size()) {
        if (error)
            *error = QStringLiteral("Шаблон не выбран.");
        return false;
    }
    QJsonObject root = templateToJson(m_templates.at(index));
    root.insert(QStringLiteral("format"), QStringLiteral("vectora-template"));
    root.insert(QStringLiteral("version"), 1);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
