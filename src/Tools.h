#pragma once

#include <QString>
#include <QVector>

enum class Tool {
    Select,
    Node,
    Crop,
    Rectangle,
    Ellipse,
    Polygon,
    Star,
    Spiral,
    Line,
    Bezier,
    Freehand,
    Artistic,
    Text,
    Eyedropper,
    Fill,
    Outline,
    Transparency,
    Shadow,
    Blend,
    Zoom,
    Pan
};

enum class ToolGroup {
    Pick,
    Shape,
    Curve,
    Paint,
    View
};

struct ToolInfo {
    Tool tool;
    ToolGroup group;
    QString id;
    QString name;
    QString shortcut;
    QString hint;
};

inline const QVector<ToolInfo> &toolInfos()
{
    static const QVector<ToolInfo> infos = {
        {Tool::Select, ToolGroup::Pick, QStringLiteral("select"),
         QStringLiteral("Выделение"), QStringLiteral("V"),
         QStringLiteral("Выделение и перемещение объектов")},
        {Tool::Node, ToolGroup::Pick, QStringLiteral("node"),
         QStringLiteral("Форма"), QStringLiteral("N"),
         QStringLiteral("Редактирование узлов и кривых")},
        {Tool::Crop, ToolGroup::Pick, QStringLiteral("crop"),
         QStringLiteral("Обрезка"), QStringLiteral("C"),
         QStringLiteral("Обрезка и вырезание объектов")},

        {Tool::Rectangle, ToolGroup::Shape, QStringLiteral("rect"),
         QStringLiteral("Прямоугольник"), QStringLiteral("R"),
         QStringLiteral("Рисование прямоугольников и квадратов")},
        {Tool::Ellipse, ToolGroup::Shape, QStringLiteral("ellipse"),
         QStringLiteral("Эллипс"), QStringLiteral("E"),
         QStringLiteral("Рисование эллипсов и кругов")},
        {Tool::Polygon, ToolGroup::Shape, QStringLiteral("polygon"),
         QStringLiteral("Многоугольник"), QStringLiteral("P"),
         QStringLiteral("Рисование правильных многоугольников")},
        {Tool::Star, ToolGroup::Shape, QStringLiteral("star"),
         QStringLiteral("Звезда"), QStringLiteral("Shift+P"),
         QStringLiteral("Рисование звёзд и сложных звёзд")},
        {Tool::Spiral, ToolGroup::Shape, QStringLiteral("spiral"),
         QStringLiteral("Спираль"), QStringLiteral("Shift+S"),
         QStringLiteral("Рисование спиралей")},

        {Tool::Line, ToolGroup::Curve, QStringLiteral("line"),
         QStringLiteral("Линия"), QStringLiteral("L"),
         QStringLiteral("Рисование прямых линий")},
        {Tool::Bezier, ToolGroup::Curve, QStringLiteral("bezier"),
         QStringLiteral("Кривая Безье"), QStringLiteral("B"),
         QStringLiteral("Рисование кривых по опорным точкам")},
        {Tool::Freehand, ToolGroup::Curve, QStringLiteral("freehand"),
         QStringLiteral("Свободная форма"), QStringLiteral("F"),
         QStringLiteral("Рисование от руки")},
        {Tool::Artistic, ToolGroup::Curve, QStringLiteral("artistic"),
         QStringLiteral("Художественное перо"), QStringLiteral("A"),
         QStringLiteral("Живописные мазки и кисти")},

        {Tool::Text, ToolGroup::Pick, QStringLiteral("text"),
         QStringLiteral("Текст"), QStringLiteral("T"),
         QStringLiteral("Фигурный и простой текст")},

        {Tool::Eyedropper, ToolGroup::Paint, QStringLiteral("eyedropper"),
         QStringLiteral("Пипетка"), QStringLiteral("I"),
         QStringLiteral("Выбор цвета с холста")},
        {Tool::Fill, ToolGroup::Paint, QStringLiteral("fill"),
         QStringLiteral("Заливка"), QStringLiteral("G"),
         QStringLiteral("Интерактивная заливка объектов")},
        {Tool::Outline, ToolGroup::Paint, QStringLiteral("outline"),
         QStringLiteral("Обводка"), QStringLiteral("O"),
         QStringLiteral("Настройка контура объекта")},
        {Tool::Transparency, ToolGroup::Paint, QStringLiteral("transparency"),
         QStringLiteral("Прозрачность"), QStringLiteral("Shift+T"),
         QStringLiteral("Интерактивная прозрачность")},
        {Tool::Shadow, ToolGroup::Paint, QStringLiteral("shadow"),
         QStringLiteral("Тень"), QStringLiteral("Shift+D"),
         QStringLiteral("Отбрасываемая тень объекта")},
        {Tool::Blend, ToolGroup::Paint, QStringLiteral("blend"),
         QStringLiteral("Перетекание"), QStringLiteral("Shift+B"),
         QStringLiteral("Перетекание между объектами")},

        {Tool::Zoom, ToolGroup::View, QStringLiteral("zoom"),
         QStringLiteral("Масштаб"), QStringLiteral("Z"),
         QStringLiteral("Увеличение и уменьшение холста")},
        {Tool::Pan, ToolGroup::View, QStringLiteral("pan"),
         QStringLiteral("Панорама"), QStringLiteral("H"),
         QStringLiteral("Перемещение по холсту")},
    };
    return infos;
}

inline const ToolInfo *toolInfo(Tool tool)
{
    for (const ToolInfo &info : toolInfos()) {
        if (info.tool == tool)
            return &info;
    }
    return nullptr;
}
