#include "SelfTest.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDockWidget>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QSlider>
#include <QToolButton>
#include <QTabWidget>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

#include "CanvasView.h"
#include "ColorPalette.h"
#include "Document.h"
#include "MainWindow.h"
#include "PageDialog.h"
#include "SvgImporter.h"
#include "VectorItem.h"

namespace {

int g_failures = 0;

void check(bool condition, const QString &message)
{
    QTextStream out(stdout);
    if (condition) {
        out << "  ok   " << message << "\n";
    } else {
        out << "  FAIL " << message << "\n";
        ++g_failures;
    }
}

void sendMouse(QWidget *target, QEvent::Type type, const QPoint &pos,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(pos), target->mapToGlobal(pos), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(target, &event);
    QApplication::processEvents();
}

void drag(CanvasView &view, const QPoint &from, const QPoint &to)
{
    sendMouse(view.viewport(), QEvent::MouseButtonPress, from, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, (from + to) / 2, Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, to, Qt::LeftButton,
              Qt::NoButton);
}

} // namespace

int SelfTest::run(const QString &directory)
{
    QDir dir(directory);
    dir.mkpath(QStringLiteral("."));

    QTextStream out(stdout);
    out << "Vectora self-test\n";

    // --- Document round trip -------------------------------------------------
    out << "\nDocument I/O\n";
    Document document = Document::sample();
    const int sampleCount = document.objects.size();
    const QString filePath = dir.filePath(QStringLiteral("sample.vct"));
    QString error;
    check(document.save(filePath, &error), QStringLiteral("save .vct"));
    check(QFileInfo::exists(filePath), QStringLiteral("file exists"));

    Document loaded;
    check(loaded.load(filePath, &error), QStringLiteral("load .vct: %1").arg(error));
    check(loaded.objects.size() == sampleCount,
          QStringLiteral("object count preserved (%1)").arg(sampleCount));
    check(loaded.layers == document.layers,
          QStringLiteral("layers preserved (%1)").arg(document.layers.size()));
    check(qFuzzyCompare(loaded.pageSize.width(), document.pageSize.width()),
          QStringLiteral("page width preserved"));

    bool pathPreserved = true;
    for (int i = 0; i < qMin(loaded.objects.size(), document.objects.size()); ++i) {
        if (loaded.objects.at(i).type != document.objects.at(i).type)
            pathPreserved = false;
        if (loaded.objects.at(i).path.elementCount()
            != document.objects.at(i).path.elementCount())
            pathPreserved = false;
    }
    check(pathPreserved, QStringLiteral("shape types and paths preserved"));

    {
        Document layered;
        layered.layers = {QStringLiteral("Нижний"), QStringLiteral("Верхний")};
        layered.layerVisible = {false, true};
        layered.layerLocked = {true, false};
        layered.groups = {{1, 0}};
        layered.guidesX = {120.0, 300.0};
        layered.guidesY = {80.0};
        VectorObject circle;
        circle.locked = true;
        circle.group = 1;
        circle.id = 7;
        circle.masterId = 3;
        circle.fillStyle = 1;
        circle.fill2 = QColor("#ff8800");
        circle.gradientAngle = 30;
        circle.pivot = QPointF(0.25, 0.75);
        circle.type = ShapeType::Ellipse;
        circle.layer = 1;
        circle.path.addEllipse(QRectF(0, 0, 40, 40));
        layered.objects.append(circle);
        const QString layeredPath = dir.filePath(QStringLiteral("layers.vct"));
        check(layered.save(layeredPath, &error), QStringLiteral("save layered document"));
        Document layeredLoaded;
        check(layeredLoaded.load(layeredPath, &error),
              QStringLiteral("load layered document"));
        check(layeredLoaded.layers == layered.layers,
              QStringLiteral("layer names preserved"));
        check(layeredLoaded.layerVisible == layered.layerVisible,
              QStringLiteral("layer visibility preserved"));
        check(layeredLoaded.layerLocked == layered.layerLocked,
              QStringLiteral("layer lock preserved"));
        check(!layeredLoaded.objects.isEmpty()
                  && layeredLoaded.objects.first().layer == 1,
              QStringLiteral("object layer preserved"));
        check(!layeredLoaded.objects.isEmpty()
                  && layeredLoaded.objects.first().locked,
              QStringLiteral("object lock preserved"));
        check(layeredLoaded.groups == layered.groups
                  && !layeredLoaded.objects.isEmpty()
                  && layeredLoaded.objects.first().group == 1,
              QStringLiteral("group preserved"));
        check(layeredLoaded.guidesX == layered.guidesX
                  && layeredLoaded.guidesY == layered.guidesY,
              QStringLiteral("guides preserved"));
        if (!layeredLoaded.objects.isEmpty()) {
            const VectorObject loadedCircle = layeredLoaded.objects.first();
            check(loadedCircle.id == 7 && loadedCircle.masterId == 3,
                  QStringLiteral("clone links preserved"));
            check(loadedCircle.fillStyle == 1
                      && loadedCircle.fill2 == QColor("#ff8800")
                      && qAbs(loadedCircle.pivot.x() - 0.25) < 0.01,
                  QStringLiteral("gradient and pivot preserved"));
        }
    }

    // --- Editable SVG import -------------------------------------------------
    out << "\nSVG import\n";
    {
        const QString svgPath = dir.filePath(QStringLiteral("import.svg"));
        QFile svgFile(svgPath);
        svgFile.open(QIODevice::WriteOnly | QIODevice::Text);
        svgFile.write(R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="300">
          <defs><linearGradient id="g"><stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
          <g id="group"><rect x="10" y="20" width="100" height="50" fill="url(#g)"/><circle cx="150" cy="30" r="20" fill="#00ff00"/></g>
          <text x="20" y="200" font-size="24" font-family="Arial">Hello</text>
        </svg>)SVG");
        svgFile.close();

        Document imported;
        QString importError;
        check(importSvgFile(svgPath, &imported, &importError),
              QStringLiteral("SVG parsed: %1").arg(importError));
        check(imported.objects.size() >= 3,
              QStringLiteral("SVG objects imported (%1)").arg(imported.objects.size()));
        check(imported.groups.size() >= 1, QStringLiteral("SVG group preserved"));
        bool hasText = false;
        bool hasGradient = false;
        for (const VectorObject &object : imported.objects) {
            if (object.type == ShapeType::Text && object.text.contains("Hello"))
                hasText = true;
            if (object.fillStyle == 1)
                hasGradient = true;
        }
        check(hasText, QStringLiteral("SVG text preserved"));
        check(hasGradient, QStringLiteral("SVG gradient preserved"));
        check(qAbs(imported.pageSize.width() - 400.0) < 1.0,
              QStringLiteral("SVG size preserved"));
    }

    // --- Exports -------------------------------------------------------------
    out << "\nExports\n";
    const QString pngPath = dir.filePath(QStringLiteral("sample.png"));
    QImage image(QSize(qRound(document.pageSize.width() * 2),
                       qRound(document.pageSize.height() * 2)),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter imagePainter(&image);
    imagePainter.scale(2.0, 2.0);
    document.render(&imagePainter);
    imagePainter.end();
    check(image.save(pngPath), QStringLiteral("export PNG (%1 px)").arg(image.width()));

    const QString svgPath = dir.filePath(QStringLiteral("sample.svg"));
    QSaveFile svgFile(svgPath);
    svgFile.open(QIODevice::WriteOnly | QIODevice::Text);
    svgFile.write(document.toSvg().toUtf8());
    check(svgFile.commit(), QStringLiteral("export SVG"));
    check(QFileInfo(svgPath).size() > 1000, QStringLiteral("SVG has content"));

    const QString pdfPath = dir.filePath(QStringLiteral("sample.pdf"));
    {
        QPdfWriter writer(pdfPath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(96);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter(&writer);
        painter.scale(writer.width() / document.pageSize.width(),
                      writer.height() / document.pageSize.height());
        document.render(&painter);
    }
    check(QFileInfo(pdfPath).size() > 1000, QStringLiteral("export PDF"));

    // --- Drawing tools -------------------------------------------------------
    out << "\nDrawing tools\n";
    CanvasView view;
    view.resize(1000, 1300);
    view.show();
    QApplication::processEvents();
    view.setPageSize(QSizeF(794, 1123));
    view.actualSize();
    QApplication::processEvents();

    const auto scenePoint = [&view](qreal x, qreal y) {
        return view.mapFromScene(QPointF(x, y));
    };

    view.setActiveTool(Tool::Rectangle);
    drag(view, scenePoint(100, 100), scenePoint(300, 220));
    check(view.vectorItems().size() == 1, QStringLiteral("rectangle created"));
    if (!view.vectorItems().isEmpty()) {
        const VectorObject object = view.vectorItems().first()->object();
        check(object.type == ShapeType::Rectangle, QStringLiteral("type is rectangle"));
        check(qAbs(object.localBounds().width() - 200.0) < 2.0
                  && qAbs(object.localBounds().height() - 120.0) < 2.0,
              QStringLiteral("rectangle has dragged size"));
    }

    view.setActiveTool(Tool::Ellipse);
    drag(view, scenePoint(350, 100), scenePoint(470, 220));
    check(view.vectorItems().size() == 2, QStringLiteral("ellipse created"));

    view.setActiveTool(Tool::Star);
    drag(view, scenePoint(560, 400), scenePoint(640, 400));
    check(view.vectorItems().size() == 3, QStringLiteral("star created"));

    view.setActiveTool(Tool::Line);
    drag(view, scenePoint(100, 300), scenePoint(300, 380));
    check(view.vectorItems().size() == 4, QStringLiteral("line created"));

    view.setActiveTool(Tool::Freehand);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, scenePoint(120, 500),
              Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i)
        sendMouse(view.viewport(), QEvent::MouseMove, scenePoint(120 + i * 12, 500 + i * 6),
                  Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, scenePoint(270, 570),
              Qt::LeftButton, Qt::NoButton);
    check(view.vectorItems().size() == 5, QStringLiteral("freehand created"));

    // Fill tool
    view.setFillColor(QColor("#ff0000"));
    VectorItem *rectangle = view.vectorItems().first();
    const QPoint rectCenter = scenePoint(200, 160);
    view.setActiveTool(Tool::Fill);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, rectCenter, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, rectCenter, Qt::LeftButton,
              Qt::NoButton);
    check(rectangle->object().fill == QColor("#ff0000"),
          QStringLiteral("fill tool applied colour"));

    // Selection + delete
    view.setActiveTool(Tool::Select);
    view.scene()->clearSelection();
    rectangle->setSelected(true);
    const int beforeDelete = view.vectorItems().size();
    view.deleteSelectedObjects();
    check(view.vectorItems().size() == beforeDelete - 1,
          QStringLiteral("delete selected object"));


    // Node editing: drag the end point of the line.
    VectorItem *lineItem = nullptr;
    for (VectorItem *item : view.vectorItems()) {
        if (item->object().type == ShapeType::Line)
            lineItem = item;
    }
    check(lineItem != nullptr, QStringLiteral("line item found"));
    if (lineItem) {
        view.scene()->clearSelection();
        lineItem->setSelected(true);
        view.setActiveTool(Tool::Node);
        const QVector<QPainterPath::Element> before = lineItem->pathElements();
        const QPointF endLocal(before.last().x, before.last().y);
        const QPoint endView = view.mapFromScene(lineItem->mapToScene(endLocal));
        drag(view, endView, endView + QPoint(24, 16));
        const QVector<QPainterPath::Element> after = lineItem->pathElements();
        check(!after.isEmpty()
                  && qAbs(after.last().x - before.last().x) > 10.0,
              QStringLiteral("node dragged"));
    }

    // Shadow effect
    VectorItem *ellipse = nullptr;
    for (VectorItem *item : view.vectorItems()) {
        if (item->object().type == ShapeType::Ellipse)
            ellipse = item;
    }
    if (ellipse) {
        view.setActiveTool(Tool::Shadow);
        const QPoint centre = view.mapFromScene(ellipse->sceneBoundingRect().center());
        sendMouse(view.viewport(), QEvent::MouseButtonPress, centre, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, centre, Qt::LeftButton,
                  Qt::NoButton);
        check(ellipse->object().shadow, QStringLiteral("shadow toggled on"));
    }

    // Blend between two selected objects
    view.setActiveTool(Tool::Select);
    view.scene()->clearSelection();
    const QList<VectorItem *> items = view.vectorItems();
    if (items.size() >= 2) {
        items.at(0)->setSelected(true);
        items.at(1)->setSelected(true);
        const int beforeBlend = items.size();
        view.setActiveTool(Tool::Blend);
        const QPoint empty = view.mapFromScene(QPointF(700, 1000));
        sendMouse(view.viewport(), QEvent::MouseButtonPress, empty, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, empty, Qt::LeftButton,
                  Qt::NoButton);
        check(view.vectorItems().size() == beforeBlend + 7,
              QStringLiteral("blend created 7 objects"));
    }

    // Bezier tool: smooth drag, cusp click, closing on the first node.
    out << "\nBezier\n";
    view.setActiveTool(Tool::Bezier);
    const int beforeBezier = view.vectorItems().size();

    const QPoint b1 = view.mapFromScene(QPointF(400, 900));
    const QPoint b2 = view.mapFromScene(QPointF(520, 850));
    const QPoint b3 = view.mapFromScene(QPointF(600, 940));

    sendMouse(view.viewport(), QEvent::MouseButtonPress, b1, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, b1 + QPoint(-36, -36), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, b1 + QPoint(-36, -36),
              Qt::LeftButton, Qt::NoButton);

    sendMouse(view.viewport(), QEvent::MouseButtonPress, b2, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, b2, Qt::LeftButton,
              Qt::NoButton);

    sendMouse(view.viewport(), QEvent::MouseButtonPress, b3, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, b3 + QPoint(30, 18), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, b3 + QPoint(30, 18),
              Qt::LeftButton, Qt::NoButton);

    // Clicking the first node closes the curve.
    sendMouse(view.viewport(), QEvent::MouseButtonPress, b1, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, b1, Qt::LeftButton,
              Qt::NoButton);

    check(view.vectorItems().size() == beforeBezier + 1,
          QStringLiteral("closed bezier created"));
    if (view.vectorItems().size() == beforeBezier + 1) {
        const VectorObject bezier = view.vectorItems().last()->object();
        check(bezier.type == ShapeType::Bezier, QStringLiteral("type is bezier"));
        check(bezier.hasFill, QStringLiteral("closed curve is filled"));
    }

    // Enter finishes an open curve.
    view.setActiveTool(Tool::Bezier);
    const QPoint d1 = view.mapFromScene(QPointF(400, 1000));
    const QPoint d2 = view.mapFromScene(QPointF(540, 1030));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, d1, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, d1, Qt::LeftButton,
              Qt::NoButton);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, d2, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, d2, Qt::LeftButton,
              Qt::NoButton);
    view.setFocus();
    QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &enterEvent);
    QApplication::processEvents();
    check(view.vectorItems().size() == beforeBezier + 2,
          QStringLiteral("Enter finished the open bezier"));

    // Round trip of the drawn document
    Document drawn;
    drawn.pageSize = view.pageSize();
    drawn.objects = view.objects();
    const QString drawnPath = dir.filePath(QStringLiteral("drawn.vct"));
    check(drawn.save(drawnPath, &error), QStringLiteral("save drawn document"));

    Document drawnLoaded;
    check(drawnLoaded.load(drawnPath, &error), QStringLiteral("reload drawn document"));
    check(drawnLoaded.objects.size() == drawn.objects.size(),
          QStringLiteral("drawn objects preserved (%1)").arg(drawn.objects.size()));

    // --- MainWindow history (undo / redo) -----------------------------------
    out << "\nHistory\n";
    {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        window.openPath(dir.filePath(QStringLiteral("sample.vct")));
        QApplication::processEvents();

        auto *canvas = window.findChild<CanvasView *>();
        check(canvas != nullptr, QStringLiteral("canvas found in main window"));
        if (canvas) {
            const int baseCount = canvas->vectorItems().size();
            canvas->setActiveTool(Tool::Rectangle);
            const QPoint a = canvas->mapFromScene(QPointF(150, 150));
            const QPoint b = canvas->mapFromScene(QPointF(330, 260));
            drag(*canvas, a, b);
            check(canvas->vectorItems().size() == baseCount + 1,
                  QStringLiteral("rectangle added via canvas"));

            QAction *undoAction = nullptr;
            QAction *redoAction = nullptr;
            for (QAction *candidate : window.findChildren<QAction *>()) {
                if (candidate->text() == QStringLiteral("Отменить"))
                    undoAction = candidate;
                else if (candidate->text() == QStringLiteral("Повторить"))
                    redoAction = candidate;
            }
            check(undoAction && undoAction->isEnabled(),
                  QStringLiteral("undo is enabled after drawing"));
            if (!canvas->vectorItems().isEmpty()) {
                canvas->vectorItems().last()->setSelected(true);
                QApplication::processEvents();
                window.grab().save(dir.filePath(QStringLiteral("window-selected.png")));
            }
            if (undoAction)
                undoAction->trigger();
            check(canvas->vectorItems().size() == baseCount,
                  QStringLiteral("undo removed the rectangle"));
            if (redoAction)
                redoAction->trigger();
            check(canvas->vectorItems().size() == baseCount + 1,
                  QStringLiteral("redo restored the rectangle"));

            // Properties docker applies geometry to the selected object.
            if (!canvas->vectorItems().isEmpty()) {
                VectorItem *item = canvas->vectorItems().last();
                canvas->scene()->clearSelection();
                item->setSelected(true);
                QApplication::processEvents();

                QDockWidget *propertiesDock = nullptr;
                for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                    if (dock->windowTitle() == QStringLiteral("Свойства"))
                        propertiesDock = dock;
                }
                check(propertiesDock != nullptr, QStringLiteral("properties docker found"));
                if (propertiesDock) {
                    const QList<QDoubleSpinBox *> fields =
                        propertiesDock->findChildren<QDoubleSpinBox *>();
                    check(!fields.isEmpty(), QStringLiteral("properties fields found"));
                    if (!fields.isEmpty()) {
                        fields.first()->setValue(210.0);
                        fields.first()->editingFinished();
                        QApplication::processEvents();
                        check(qAbs(item->pos().x() - 210.0) < 0.5,
                              QStringLiteral("properties X applied"));
                    }
                }

                QDockWidget *colorDock = nullptr;
                for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                    if (dock->windowTitle() == QStringLiteral("Цвет"))
                        colorDock = dock;
                }
                check(colorDock != nullptr, QStringLiteral("colour docker found"));
                if (colorDock) {
                    bool hasFillButton = false;
                    for (QToolButton *button : colorDock->findChildren<QToolButton *>()) {
                        if (button->text().contains(QStringLiteral("Залить"))
                            || button->text().contains(QStringLiteral("Обвести")))
                            hasFillButton = true;
                    }
                    check(!hasFillButton,
                          QStringLiteral("colour docker has no fill/outline buttons"));

                    const QList<QSlider *> sliders =
                        colorDock->findChildren<QSlider *>();
                    check(!sliders.isEmpty(), QStringLiteral("colour sliders found"));
                    if (!sliders.isEmpty()) {
                        const QColor before = item->object().fill;
                        const int oldHue = sliders.first()->value();
                        sliders.first()->setValue((oldHue + 40) % 360);
                        QApplication::processEvents();
                        check(item->object().fill != before,
                              QStringLiteral("colour docker applies fill automatically"));
                        sliders.first()->sliderReleased();
                    }
                }

                QDockWidget *effectsDock = nullptr;
                for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                    if (dock->windowTitle() == QStringLiteral("Эффекты"))
                        effectsDock = dock;
                }
                check(effectsDock != nullptr, QStringLiteral("effects docker found"));
                if (effectsDock) {
                    bool hasApplyButton = false;
                    for (QToolButton *button :
                         effectsDock->findChildren<QToolButton *>()) {
                        if (button->text().contains(QStringLiteral("Применить прозрачность")))
                            hasApplyButton = true;
                    }
                    check(!hasApplyButton,
                          QStringLiteral("transparency has no apply button"));

                    auto *tabs = effectsDock->findChild<QTabWidget *>();
                    if (tabs)
                        tabs->setCurrentIndex(1);
                    QApplication::processEvents();
                    const QList<QSlider *> sliders =
                        effectsDock->findChildren<QSlider *>();
                    check(!sliders.isEmpty(), QStringLiteral("transparency slider found"));
                    if (!sliders.isEmpty()) {
                        sliders.first()->setValue(42);
                        QApplication::processEvents();
                        check(qAbs(item->object().opacity - 0.42) < 0.01,
                              QStringLiteral("transparency applies in real time"));
                        sliders.first()->sliderReleased();
                    }
                }

                QDockWidget *objectsDock = nullptr;
                for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
                    if (dock->windowTitle() == QStringLiteral("Объекты"))
                        objectsDock = dock;
                }
                check(objectsDock != nullptr, QStringLiteral("objects docker found"));
                if (objectsDock) {
                    QToolButton *addLayer = nullptr;
                    for (QToolButton *button :
                         objectsDock->findChildren<QToolButton *>()) {
                        if (button->text().contains(QStringLiteral("+ Слой")))
                            addLayer = button;
                    }
                    check(addLayer != nullptr, QStringLiteral("add layer button found"));
                    if (addLayer) {
                        const int layersBefore = canvas->layers().size();
                        addLayer->click();
                        QApplication::processEvents();
                        check(canvas->layers().size() == layersBefore + 1,
                              QStringLiteral("add layer button creates a layer"));

                        canvas->scene()->clearSelection();
                        QApplication::processEvents();

                        QToolButton *removeLayerButton = nullptr;
                        for (QToolButton *button :
                             objectsDock->findChildren<QToolButton *>()) {
                            if (button->text().contains(QStringLiteral("Слой"))
                                && button->text().startsWith(QStringLiteral("−")))
                                removeLayerButton = button;
                        }
                        check(removeLayerButton != nullptr,
                              QStringLiteral("remove layer button found"));
                        if (removeLayerButton) {
                            removeLayerButton->click();
                            QApplication::processEvents();
                            check(canvas->layers().size() == layersBefore,
                                  QStringLiteral("remove layer button deletes layer"));
                        }
                    }
                }
            }
        }
    }

    // --- Layers --------------------------------------------------------------
    out << "\nLayers\n";
    {
        view.setDocument(Document());
        check(view.layers().size() == 1, QStringLiteral("default layer exists"));

        const int topLayer = view.addLayer(QStringLiteral("Верхний"));
        check(view.layers().size() == 2, QStringLiteral("layer created"));
        check(view.activeLayer() == topLayer, QStringLiteral("new layer is active"));

        VectorObject rectangle;
        rectangle.type = ShapeType::Rectangle;
        rectangle.path.addRect(QRectF(0, 0, 50, 50));
        VectorItem *item = view.addVectorObject(rectangle, false, false);
        check(item->object().layer == topLayer,
              QStringLiteral("new object lands in active layer"));

        view.moveItemsToLayer({item}, 0);
        check(view.layerOf(item) == 0, QStringLiteral("object moved to another layer"));

        view.moveLayer(0, 1);
        check(view.layerOf(item) == 1,
              QStringLiteral("object follows its layer when layers are reordered"));
        check(view.layers().first() == QStringLiteral("Верхний"),
              QStringLiteral("layer order changed"));

        view.removeLayer(1);
        check(view.layers().size() == 1, QStringLiteral("layer removed"));
        check(view.vectorItems().isEmpty(),
              QStringLiteral("layer contents removed with the layer"));

        // Ordering inside one layer.
        view.setDocument(Document());
        VectorObject first;
        first.type = ShapeType::Rectangle;
        first.path.addRect(QRectF(0, 0, 40, 40));
        VectorObject second;
        second.type = ShapeType::Ellipse;
        second.path.addEllipse(QRectF(0, 0, 40, 40));
        VectorItem *bottom = view.addVectorObject(first, false, false);
        VectorItem *top = view.addVectorObject(second, false, false);
        check(view.itemsInLayer(0).last() == top,
              QStringLiteral("new object is on top of the layer"));
        view.moveItemToLayer(bottom, 0, 0);
        check(view.itemsInLayer(0).last() == bottom,
              QStringLiteral("object moved to the top of its layer"));
        view.moveItemToLayer(top, 0, 0);
        check(view.itemsInLayer(0).last() == top,
              QStringLiteral("object reordered inside the layer"));

        view.setLayerVisible(0, false);
        check(!bottom->isVisible(), QStringLiteral("hidden layer hides objects"));
        view.setLayerVisible(0, true);
        check(bottom->isVisible(), QStringLiteral("shown layer shows objects"));

        view.setLayerLocked(0, true);
        check(!(bottom->flags() & QGraphicsItem::ItemIsSelectable),
              QStringLiteral("locked layer disables selection"));
        const int lockedCount = view.vectorItems().size();
        VectorObject blocked;
        blocked.type = ShapeType::Ellipse;
        blocked.path.addEllipse(QRectF(0, 0, 30, 30));
        check(view.addVectorObject(blocked, true, false) == nullptr,
              QStringLiteral("locked layer refuses new objects"));
        check(view.vectorItems().size() == lockedCount,
              QStringLiteral("refused object was not added"));
        view.setLayerLocked(0, false);
        check(view.addVectorObject(blocked, true, false) != nullptr,
              QStringLiteral("unlocked layer accepts objects again"));
    }

    // --- Shape parameters and object order ---------------------------------
    out << "\nShapes\n";
    {
        view.setDocument(Document());

        VectorObject rectangle;
        rectangle.type = ShapeType::Rectangle;
        rectangle.path = buildRectanglePath(QRectF(0, 0, 120, 80), 0);
        VectorItem *rectItem = view.addVectorObject(rectangle, false, false);
        const QSizeF rectSize = rectItem->object().localBounds().size();
        rectItem->setCornerRadius(24);
        check(qAbs(rectItem->object().cornerRadius - 24.0) < 0.01,
              QStringLiteral("rectangle corner radius set"));
        check(rectItem->object().path.elementCount() > 5,
              QStringLiteral("rounded rectangle rebuilt"));
        check(qAbs(rectItem->object().localBounds().width() - rectSize.width()) < 0.6,
              QStringLiteral("corner radius keeps rectangle size"));

        VectorObject polygon;
        polygon.type = ShapeType::Polygon;
        polygon.sides = 6;
        polygon.path = buildPolygonPath(QRectF(0, 0, 100, 100), 6);
        VectorItem *polygonItem = view.addVectorObject(polygon, false, false);
        const QSizeF polygonSize = polygonItem->object().localBounds().size();
        polygonItem->setSides(9);
        check(polygonItem->object().sides == 9,
              QStringLiteral("polygon side count changed"));
        check(qAbs(polygonItem->object().localBounds().width() - polygonSize.width()) < 0.6
                  && qAbs(polygonItem->object().localBounds().height()
                          - polygonSize.height()) < 0.6,
              QStringLiteral("polygon keeps its size"));

        VectorObject star;
        star.type = ShapeType::Star;
        star.starPoints = 5;
        star.starRatio = 0.45;
        star.path = buildStarPath(QRectF(0, 0, 120, 120), 5, 0.45);
        VectorItem *starItem = view.addVectorObject(star, false, false);
        starItem->setStarPoints(9);
        starItem->setStarRatio(0.3);
        check(starItem->object().starPoints == 9,
              QStringLiteral("star point count changed"));
        check(qAbs(starItem->object().starRatio - 0.3) < 0.01,
              QStringLiteral("star sharpness changed"));

        VectorObject spiral;
        spiral.type = ShapeType::Spiral;
        spiral.spiralTurns = 3;
        spiral.path = buildSpiralPath(QRectF(0, 0, 100, 100), 3);
        VectorItem *spiralItem = view.addVectorObject(spiral, false, false);
        spiralItem->setSpiralTurns(6);
        check(qAbs(spiralItem->object().spiralTurns - 6.0) < 0.01,
              QStringLiteral("spiral turns changed"));

        // Order operations.
        view.setDocument(Document());
        QList<VectorItem *> created;
        for (int i = 0; i < 3; ++i) {
            VectorObject object;
            object.type = ShapeType::Rectangle;
            object.path.addRect(QRectF(0, 0, 40, 40));
            object.pos = QPointF(i * 50, 0);
            created.append(view.addVectorObject(object, false, false));
        }
        view.scene()->clearSelection();
        created.first()->setSelected(true);
        view.bringToFront();
        check(view.itemsInLayer(0).last() == created.first(),
              QStringLiteral("bring to front"));
        view.sendToBack();
        check(view.itemsInLayer(0).first() == created.first(),
              QStringLiteral("send to back"));
        view.bringToFront();
        view.sendBackward();
        check(view.itemsInLayer(0).indexOf(created.first()) == 1,
              QStringLiteral("send backward one step"));
        view.bringForward();
        check(view.itemsInLayer(0).last() == created.first(),
              QStringLiteral("bring forward one step"));

        view.scene()->clearSelection();
        created.first()->setSelected(true);
        view.rotateSelection(90);
        check(qAbs(created.first()->object().rotation - 90.0) < 0.01,
              QStringLiteral("rotate 90 degrees"));
        const QPointF center = created.first()->sceneBoundingRect().center();
        view.flipSelection(true);
        check(QLineF(center, created.first()->sceneBoundingRect().center()).length() < 1.0,
              QStringLiteral("flip keeps the centre"));

        // Dragging one of several selected objects must move the whole selection.
        view.setDocument(Document());
        VectorObject left;
        left.type = ShapeType::Rectangle;
        left.path.addRect(QRectF(0, 0, 60, 60));
        left.pos = QPointF(120, 120);
        VectorObject right;
        right.type = ShapeType::Rectangle;
        right.path.addRect(QRectF(0, 0, 60, 60));
        right.pos = QPointF(260, 120);
        VectorItem *leftItem = view.addVectorObject(left, false, false);
        VectorItem *rightItem = view.addVectorObject(right, false, false);
        view.setActiveTool(Tool::Select);
        view.scene()->clearSelection();
        leftItem->setSelected(true);
        rightItem->setSelected(true);
        const QPointF leftBefore = leftItem->pos();
        const QPointF rightBefore = rightItem->pos();
        const QPoint startPoint = view.mapFromScene(leftItem->sceneBoundingRect().center());
        drag(view, startPoint, startPoint + QPoint(40, 30));
        const QPointF leftDelta = leftItem->pos() - leftBefore;
        const QPointF rightDelta = rightItem->pos() - rightBefore;
        check(qAbs(leftDelta.x() - 40.0) < 1.5 && qAbs(leftDelta.y() - 30.0) < 1.5,
              QStringLiteral("dragged object follows the mouse (delta %1,%2)")
                  .arg(leftDelta.x()).arg(leftDelta.y()));
        check(qAbs(rightDelta.x() - leftDelta.x()) < 0.5
                  && qAbs(rightDelta.y() - leftDelta.y()) < 0.5,
              QStringLiteral("multi-selection moves together"));

        // Groups: selection spreads to the whole group and ungroup works.
        view.setDocument(Document());
        QList<VectorItem *> groupItems;
        for (int i = 0; i < 4; ++i) {
            VectorObject object;
            object.type = ShapeType::Rectangle;
            object.path.addRect(QRectF(0, 0, 50, 50));
            object.pos = QPointF(80 + i * 80, 80);
            groupItems.append(view.addVectorObject(object, false, false));
        }
        view.scene()->clearSelection();
        groupItems.at(0)->setSelected(true);
        groupItems.at(1)->setSelected(true);
        view.groupSelection();
        check(view.groups().size() == 1, QStringLiteral("group created"));
        view.scene()->clearSelection();
        groupItems.at(0)->setSelected(true);
        QApplication::processEvents();
        check(view.selectedVectorItems().size() == 2,
              QStringLiteral("clicking a group selects all members"));

        // Ctrl+click enters the group and selects a single member.
        const QPoint insidePoint = view.mapFromScene(
            groupItems.at(0)->mapToScene(groupItems.at(0)->localBounds().center()));
        QMouseEvent ctrlClick(QEvent::MouseButtonPress, QPointF(insidePoint),
                              view.viewport()->mapToGlobal(insidePoint),
                              Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QApplication::sendEvent(view.viewport(), &ctrlClick);
        QApplication::processEvents();
        check(view.selectedVectorItems().size() == 1,
              QStringLiteral("Ctrl+click selects inside a group"));

        view.scene()->clearSelection();
        groupItems.at(0)->setSelected(true);
        QApplication::processEvents();
        check(view.selectedVectorItems().size() == 2,
              QStringLiteral("normal click reselects the whole group"));

        const QPointF beforeRotate = groupItems.at(1)->pos();
        view.rotateSelection(90);
        check(QLineF(beforeRotate, groupItems.at(1)->pos()).length() > 1.0,
              QStringLiteral("group rotates around its centre"));
        view.ungroupSelection();
        check(view.groups().isEmpty(), QStringLiteral("ungroup removes group"));

        // Nested groups and ungroup-all.
        view.scene()->clearSelection();
        groupItems.at(0)->setSelected(true);
        groupItems.at(1)->setSelected(true);
        view.groupSelection();
        view.scene()->clearSelection();
        groupItems.at(2)->setSelected(true);
        groupItems.at(3)->setSelected(true);
        view.groupSelection();
        view.selectAllObjects();
        view.groupSelection();
        check(view.groups().size() == 3, QStringLiteral("nested group created"));
        view.ungroupAll();
        check(view.groups().isEmpty(), QStringLiteral("ungroup all removes nesting"));

        // Step and repeat.
        view.setDocument(Document());
        VectorObject repeatBase;
        repeatBase.type = ShapeType::Rectangle;
        repeatBase.path.addRect(QRectF(0, 0, 40, 40));
        VectorItem *repeatItem = view.addVectorObject(repeatBase, false, false);
        view.scene()->clearSelection();
        repeatItem->setSelected(true);
        const int beforeRepeat = view.vectorItems().size();
        view.duplicateSelection(3, QPointF(50, 0));
        check(view.vectorItems().size() == beforeRepeat + 3,
              QStringLiteral("step and repeat creates copies"));

        // Skew changes the path.
        const int elementsBeforeSkew = repeatItem->object().path.elementCount();
        view.skewSelection(20, 0);
        check(repeatItem->object().path.elementCount() == elementsBeforeSkew,
              QStringLiteral("skew keeps path complexity"));

        // Clones follow their master.
        view.setDocument(Document());
        VectorObject masterObject;
        masterObject.type = ShapeType::Rectangle;
        masterObject.path.addRect(QRectF(0, 0, 60, 40));
        masterObject.fill = QColor("#112233");
        masterObject.pos = QPointF(100, 100);
        VectorItem *masterItem = view.addVectorObject(masterObject, false, false);
        view.scene()->clearSelection();
        masterItem->setSelected(true);
        view.cloneSelection();
        check(view.vectorItems().size() == 2, QStringLiteral("clone created"));
        VectorItem *cloneItem = view.vectorItems().last();
        check(cloneItem->object().masterId == masterItem->object().id,
              QStringLiteral("clone remembers its master"));
        masterItem->setFillColor(QColor("#ff0000"));
        view.syncClones();
        check(cloneItem->object().fill == QColor("#ff0000"),
              QStringLiteral("clone follows master fill"));

        // Group scaling changes the whole selection.
        view.setDocument(Document());
        QList<VectorItem *> scaledItems;
        for (int i = 0; i < 2; ++i) {
            VectorObject object;
            object.type = ShapeType::Rectangle;
            object.path.addRect(QRectF(0, 0, 50, 50));
            object.pos = QPointF(100 + i * 80, 100);
            scaledItems.append(view.addVectorObject(object, false, false));
        }
        view.scene()->clearSelection();
        scaledItems.at(0)->setSelected(true);
        scaledItems.at(1)->setSelected(true);
        const QRectF beforeScale = scaledItems.at(0)->mapRectToScene(
                                       scaledItems.at(0)->localBounds())
                                       .united(scaledItems.at(1)->mapRectToScene(
                                           scaledItems.at(1)->localBounds()));
        view.scaleSelectionTo(QRectF(beforeScale.topLeft(), QSizeF(300, 150)));
        const QRectF afterScale = scaledItems.at(0)->mapRectToScene(
                                      scaledItems.at(0)->localBounds())
                                      .united(scaledItems.at(1)->mapRectToScene(
                                          scaledItems.at(1)->localBounds()));
        check(qAbs(afterScale.width() - 300.0) < 2.0,
              QStringLiteral("group scaling resizes the selection"));

        // Text spacing and text-to-curves.
        view.setDocument(Document());
        VectorObject textObject;
        textObject.type = ShapeType::Text;
        textObject.text = QStringLiteral("Abc\nDef");
        textObject.font.setPixelSize(28);
        textObject.textBox = QRectF(0, 0, 260, 90);
        textObject.fill = QColor("#112233");
        textObject.pos = QPointF(120, 150);
        VectorItem *textItem = view.addVectorObject(textObject, false, false);
        textItem->setLetterSpacing(2.5);
        textItem->setLineSpacing(1.4);
        check(qAbs(textItem->object().letterSpacing - 2.5) < 0.01
                  && qAbs(textItem->object().lineSpacing - 1.4) < 0.01,
              QStringLiteral("text spacing applied"));
        check(!buildTextPath(textItem->object()).isEmpty(),
              QStringLiteral("text can be converted to a path"));
        view.scene()->clearSelection();
        textItem->setSelected(true);
        const int textCount = view.vectorItems().size();
        view.convertTextToCurves();
        check(view.vectorItems().size() == textCount
                  && view.selectedVectorItems().size() == 1
                  && view.selectedVectorItems().first()->object().type
                         == ShapeType::Bezier,
              QStringLiteral("text converted to curves"));

        // Boolean operations and geometry tools.
        const auto resetTwoRects = [&view]() {
            view.setDocument(Document());
            VectorObject first;
            first.type = ShapeType::Rectangle;
            first.path.addRect(QRectF(0, 0, 100, 100));
            first.pos = QPointF(100, 100);
            VectorObject second = first;
            second.pos = QPointF(150, 100);
            VectorItem *a = view.addVectorObject(first, false, false);
            VectorItem *b = view.addVectorObject(second, false, false);
            view.scene()->clearSelection();
            a->setSelected(true);
            b->setSelected(true);
            return QPair<VectorItem *, VectorItem *>(a, b);
        };

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Union);
        check(view.vectorItems().size() == 1
                  && qAbs(view.vectorItems().first()->object().localBounds().width() - 150.0) < 1.0,
              QStringLiteral("boolean union"));

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Difference);
        check(view.vectorItems().size() == 1,
              QStringLiteral("boolean difference"));

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Intersection);
        check(view.vectorItems().size() == 1
                  && qAbs(view.vectorItems().first()->object().localBounds().width() - 50.0) < 1.0,
              QStringLiteral("boolean intersection"));

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Exclude);
        check(view.vectorItems().size() == 1,
              QStringLiteral("boolean exclude"));

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Divide);
        check(view.vectorItems().size() == 3,
              QStringLiteral("boolean divide into 3 regions"));

        resetTwoRects();
        view.applyBooleanOperation(CanvasView::BooleanOperation::Trim);
        check(view.vectorItems().size() == 2,
              QStringLiteral("boolean trim"));

        // Knife and eraser.
        view.setDocument(Document());
        VectorObject knifeRect;
        knifeRect.type = ShapeType::Rectangle;
        knifeRect.path.addRect(QRectF(0, 0, 100, 100));
        knifeRect.pos = QPointF(100, 100);
        VectorItem *knifeItem = view.addVectorObject(knifeRect, false, false);
        view.scene()->clearSelection();
        knifeItem->setSelected(true);
        view.applyKnifeCut(QLineF(150, 50, 150, 250));
        check(view.vectorItems().size() == 2,
              QStringLiteral("knife splits a rectangle"));

        view.setDocument(Document());
        VectorObject eraserRect = knifeRect;
        VectorItem *eraserItem = view.addVectorObject(eraserRect, false, false);
        view.scene()->clearSelection();
        eraserItem->setSelected(true);
        view.applyEraserStroke({QPointF(150, 80), QPointF(150, 220)});
        check(view.vectorItems().size() == 1
                  && view.vectorItems().first()->object().path.toSubpathPolygons().size() >= 2,
              QStringLiteral("eraser splits the filled area"));

        // Stroke styling and stroke-to-path.
        view.setDocument(Document());
        VectorObject strokeRect;
        strokeRect.type = ShapeType::Rectangle;
        strokeRect.path.addRect(QRectF(0, 0, 80, 60));
        strokeRect.pos = QPointF(100, 100);
        strokeRect.stroke = QColor("#ff0000");
        strokeRect.strokeWidth = 4;
        VectorItem *strokeItem = view.addVectorObject(strokeRect, false, false);
        strokeItem->setStrokeStyle(1, 1, 6.0, {3.0, 1.5});
        check(strokeItem->object().capStyle == 1
                  && strokeItem->object().joinStyle == 1
                  && strokeItem->object().dashPattern.size() == 2,
              QStringLiteral("stroke cap/join/dash applied"));
        view.scene()->clearSelection();
        strokeItem->setSelected(true);
        view.convertStrokeToPath();
        check(view.vectorItems().size() == 1
                  && view.vectorItems().first()->object().type == ShapeType::Bezier
                  && view.vectorItems().first()->object().hasFill,
              QStringLiteral("stroke converted to a filled outline"));

        // Convert a primitive to curves.
        view.setDocument(Document());
        VectorObject primitive;
        primitive.type = ShapeType::Ellipse;
        primitive.path.addEllipse(QRectF(0, 0, 80, 60));
        VectorItem *primitiveItem = view.addVectorObject(primitive, false, false);
        view.scene()->clearSelection();
        primitiveItem->setSelected(true);
        view.convertSelectionToCurves();
        check(view.vectorItems().size() == 1
                  && view.vectorItems().first()->object().type == ShapeType::Bezier,
              QStringLiteral("primitive converted to curves"));

        // Shape Builder merges regions selected by a marquee.
        {
            view.setDocument(Document());
            VectorObject first;
            first.type = ShapeType::Rectangle;
            first.path.addRect(QRectF(0, 0, 100, 100));
            first.pos = QPointF(100, 100);
            VectorObject second = first;
            second.pos = QPointF(150, 100);
            VectorItem *a = view.addVectorObject(first, false, false);
            VectorItem *b = view.addVectorObject(second, false, false);
            view.scene()->clearSelection();
            a->setSelected(true);
            b->setSelected(true);
            view.show();
            view.setActiveTool(Tool::ShapeBuilder);
            const QPoint from = view.mapFromScene(QPointF(60, 60));
            const QPoint to = view.mapFromScene(QPointF(320, 260));
            drag(view, from, to);
            check(view.vectorItems().size() == 1,
                  QStringLiteral("shape builder merges all regions"));
            view.setActiveTool(Tool::Select);
        }

        // Contour creates outer and inner outlines.
        {
            view.setDocument(Document());
            VectorObject rect;
            rect.type = ShapeType::Rectangle;
            rect.path.addRect(QRectF(0, 0, 100, 80));
            rect.pos = QPointF(100, 100);
            rect.stroke = QColor("#ff0000");
            VectorItem *item = view.addVectorObject(rect, false, false);
            view.scene()->clearSelection();
            item->setSelected(true);
            view.applyContour(3, 4.0, 0, QColor("#ff0000"), true);
            check(view.vectorItems().size() == 4,
                  QStringLiteral("contour creates 3 outer outlines"));
            view.applyContour(2, 4.0, 1, QColor("#00ff00"), false);
            check(view.vectorItems().size() == 6,
                  QStringLiteral("contour creates inner outlines"));
        }

        // Envelope warps the object through its control grid.
        {
            view.setDocument(Document());
            VectorObject rect;
            rect.type = ShapeType::Rectangle;
            rect.path.addRect(QRectF(0, 0, 100, 100));
            rect.pos = QPointF(100, 100);
            VectorItem *item = view.addVectorObject(rect, false, false);
            view.scene()->clearSelection();
            item->setSelected(true);
            view.show();
            view.setActiveTool(Tool::Envelope);
            sendMouse(view.viewport(), QEvent::MouseButtonPress,
                      view.mapFromScene(QPointF(150, 150)), Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(view.viewport(), QEvent::MouseButtonRelease,
                      view.mapFromScene(QPointF(150, 150)), Qt::LeftButton,
                      Qt::NoButton);
            check(view.envelopeActive(), QStringLiteral("envelope grid activated"));
            const QPointF originalTopLeft = item->localBounds().topLeft();
            const QPointF handleScene = item->mapToScene(originalTopLeft);
            sendMouse(view.viewport(), QEvent::MouseButtonPress,
                      view.mapFromScene(handleScene), Qt::LeftButton, Qt::LeftButton);
            sendMouse(view.viewport(), QEvent::MouseMove,
                      view.mapFromScene(handleScene + QPointF(-30, -30)),
                      Qt::NoButton, Qt::LeftButton);
            const QRectF warped = item->localBounds();
            sendMouse(view.viewport(), QEvent::MouseButtonRelease,
                      view.mapFromScene(handleScene + QPointF(-30, -30)),
                      Qt::LeftButton, Qt::NoButton);
            check(QLineF(originalTopLeft, warped.topLeft()).length() > 5.0
                      && item->object().type == ShapeType::Rectangle,
                  QStringLiteral("envelope drag warps geometry"));
            view.setActiveTool(Tool::Select);
        }

        // Gradient types, stop transparency and colour spaces.
        {
            VectorObject gradient;
            gradient.type = ShapeType::Rectangle;
            gradient.path.addRect(QRectF(0, 0, 100, 100));
            gradient.fillStyle = 4;
            gradient.gradientStops = {GradientStop{0.0, QColor("#ff0000"), 1.0},
                                      GradientStop{0.5, QColor("#00ff00"), 0.6},
                                      GradientStop{1.0, QColor("#0000ff"), 1.0}};
            Document doc;
            doc.objects.append(gradient);
            QImage image(120, 120, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            doc.render(&painter);
            painter.end();
            check(image.pixelColor(10, 60) != image.pixelColor(58, 60)
                      && image.pixelColor(58, 60) != image.pixelColor(100, 60),
                  QStringLiteral("multi-stop gradient renders"));

            gradient.fillStyle = 3;
            Document conic;
            conic.objects.append(gradient);
            QImage conicImage(120, 120, QImage::Format_ARGB32);
            conicImage.fill(Qt::white);
            QPainter conicPainter(&conicImage);
            conic.render(&conicPainter);
            conicPainter.end();
            check(conicImage.pixelColor(50, 50) != conicImage.pixelColor(50, 20),
                  QStringLiteral("conic gradient renders"));

            gradient.fillStyle = 6;
            Document mesh;
            mesh.objects.append(gradient);
            QImage meshImage(120, 120, QImage::Format_ARGB32);
            meshImage.fill(Qt::white);
            QPainter meshPainter(&meshImage);
            mesh.render(&meshPainter);
            meshPainter.end();
            check(meshImage.pixelColor(10, 10) != meshImage.pixelColor(100, 100),
                  QStringLiteral("mesh gradient renders"));

            Document parsed = Document::fromJson(conic.toJson(), nullptr);
            check(parsed.objects.first().gradientStops.size() == 3
                      && parsed.objects.first().fillStyle == 3,
                  QStringLiteral("gradient stops serialize"));

            const QString svg = conic.toSvg();
            check(svg.contains(QStringLiteral("stop-color=\"#00ff00\""))
                      && svg.contains(QStringLiteral("radialGradient"))
                      && svg.contains(QStringLiteral("stop-opacity=\"0.60\"")),
                  QStringLiteral("gradient exported to SVG"));

            check(colorToHex(QColor("#123456")) == QStringLiteral("#123456")
                      && colorFromHex(QStringLiteral("#00ff00")) == QColor("#00ff00"),
                  QStringLiteral("hex colour conversion"));
            qreal c = 0, m = 0, y = 0, k = 0;
            colorToCmyk(QColor("#ff0000"), &c, &m, &y, &k);
            check(qAbs(c) < 0.01 && qAbs(m - 1.0) < 0.01
                      && qAbs(y - 1.0) < 0.01 && qAbs(k) < 0.01,
                  QStringLiteral("RGB to CMYK conversion"));
            const QColor fromCmyk = colorFromCmyk(1.0, 0.0, 0.0, 0.0);
            check(fromCmyk.red() < 3 && fromCmyk.green() > 250
                      && fromCmyk.blue() > 250,
                  QStringLiteral("CMYK to RGB conversion"));
            qreal labL = 0, labA = 0, labB = 0;
            colorToLab(QColor("#ffffff"), &labL, &labA, &labB);
            check(qAbs(labL - 100.0) < 1.0,
                  QStringLiteral("RGB to Lab conversion"));
            const QColor fromLab = colorFromLab(53.0, 80.0, 67.0);
            check(fromLab.red() > 200 && fromLab.green() < 80,
                  QStringLiteral("Lab to RGB conversion"));
            const QColor hsl = colorFromHsl(0.0, 1.0, 0.5);
            check(hsl.red() > 250 && hsl.green() < 5,
                  QStringLiteral("HSL conversion"));
        }

        // Blend modes and independent fill/stroke transparency.
        {
            VectorObject top;
            top.type = ShapeType::Rectangle;
            top.path.addRect(QRectF(0, 0, 100, 100));
            top.fill = QColor("#ff8800");
            VectorObject plain = top;

            top.blendMode = 1; // multiply
            top.fillOpacity = 0.5;
            Document doc;
            doc.objects.append(plain);
            doc.objects.append(top);
            QImage image(120, 120, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            doc.render(&painter);
            painter.end();
            const QColor blended = image.pixelColor(50, 50);

            Document single;
            single.objects.append(plain);
            QImage plainImage(120, 120, QImage::Format_ARGB32);
            plainImage.fill(Qt::white);
            QPainter plainPainter(&plainImage);
            single.render(&plainPainter);
            plainPainter.end();

            check(blended != plainImage.pixelColor(50, 50),
                  QStringLiteral("blend mode changes compositing"));

            Document parsed = Document::fromJson(doc.toJson(), nullptr);
            check(parsed.objects.size() == 2
                      && parsed.objects.at(1).blendMode == 1
                      && qAbs(parsed.objects.at(1).fillOpacity - 0.5) < 0.001,
                  QStringLiteral("blend mode and fill opacity serialize"));
        }

        // Text on path, text in shape and typography extras.
        {
            view.setDocument(Document());
            VectorObject textObject;
            textObject.type = ShapeType::Text;
            textObject.text = QStringLiteral("Vectora");
            textObject.font = QFont(QStringLiteral("DejaVu Sans"), 24);
            textObject.textBox = QRectF(0, 0, 300, 60);
            textObject.pos = QPointF(80, 80);
            textObject.hasFill = true;

            VectorObject curveObject;
            curveObject.type = ShapeType::Bezier;
            QPainterPath curve;
            curve.moveTo(0, 120);
            curve.cubicTo(80, 20, 220, 220, 300, 120);
            curveObject.path = curve;
            curveObject.pos = QPointF(60, 60);
            curveObject.hasFill = false;
            curveObject.hasStroke = true;

            VectorItem *textItem = view.addVectorObject(textObject, false, false);
            VectorItem *curveItem = view.addVectorObject(curveObject, false, false);
            view.scene()->clearSelection();
            textItem->setSelected(true);
            curveItem->setSelected(true);
            view.applyTextOnPath();
            check(textItem->object().textOnPath
                      && !textItem->object().textPathCurve.isEmpty()
                      && !buildTextPath(textItem->object()).isEmpty(),
                  QStringLiteral("text placed on a path"));

            const QString svg = view.objects().isEmpty()
                                    ? QString()
                                    : QString();
            Q_UNUSED(svg)

            view.detachTextSpecial();
            check(!textItem->object().textOnPath,
                  QStringLiteral("text detached from path"));

            textItem->setSelected(true);
            curveItem->setSelected(true);
            view.applyTextInShape();
            check(textItem->object().textInShape
                      && !textItem->object().textShape.isEmpty(),
                  QStringLiteral("text placed inside a shape"));

            view.detachTextSpecial();
            VectorObject typography = textItem->object();
            typography.underline = true;
            typography.strikeOut = true;
            typography.script = 1;
            typography.wordSpacing = 6.0;
            typography.horizontalScale = 1.25;
            typography.verticalScale = 0.8;
            typography.paragraphSpacing = 8.0;
            typography.firstLineIndent = 12.0;
            textItem->setObject(typography);

            Document textDoc;
            textDoc.objects.append(typography);
            const Document reloaded = Document::fromJson(textDoc.toJson(), nullptr);
            const VectorObject restored = reloaded.objects.first();
            check(restored.underline && restored.strikeOut && restored.script == 1
                      && qAbs(restored.wordSpacing - 6.0) < 0.001
                      && qAbs(restored.horizontalScale - 1.25) < 0.001,
                  QStringLiteral("typography attributes serialize"));

            QImage textImage(420, 220, QImage::Format_ARGB32);
            textImage.fill(Qt::white);
            QPainter textPainter(&textImage);
            textDoc.render(&textPainter);
            textPainter.end();
            check(textImage.pixelColor(400, 200) != QColor(Qt::black),
                  QStringLiteral("styled text renders"));
        }

        // Multipage documents, duplication and master pages.
        {
            view.setDocument(Document());
            check(view.pageList().size() == 1
                      && view.currentPage() == 0,
                  QStringLiteral("document starts with one page"));

            VectorObject pageRect;
            pageRect.type = ShapeType::Rectangle;
            pageRect.path.addRect(QRectF(0, 0, 80, 60));
            pageRect.pos = QPointF(100, 100);
            VectorItem *pageOneItem = view.addVectorObject(pageRect, false, false);

            const int pageTwo = view.addPage();
            check(view.pageList().size() == 2 && view.currentPage() == pageTwo,
                  QStringLiteral("page added"));
            check(!pageOneItem->isVisible(),
                  QStringLiteral("objects are hidden on other pages"));

            VectorItem *pageTwoItem = view.addVectorObject(pageRect, false, false);
            check(pageTwoItem->object().page == pageTwo,
                  QStringLiteral("new objects belong to the current page"));

            view.setCurrentPage(0);
            check(pageOneItem->isVisible() && !pageTwoItem->isVisible(),
                  QStringLiteral("switching pages updates visibility"));

            const int copy = view.duplicatePage(0);
            check(view.pageList().size() == 3 && view.currentPage() == copy,
                  QStringLiteral("page duplicated"));
            int copiedObjects = 0;
            for (const VectorObject &object : view.objects()) {
                if (object.page == copy)
                    ++copiedObjects;
            }
            check(copiedObjects == 1,
                  QStringLiteral("page duplication copies objects"));

            view.movePage(copy, 0);
            check(view.currentPage() == 0,
                  QStringLiteral("page order changes"));

            view.removePage(0);
            check(view.pageList().size() == 2,
                  QStringLiteral("page removed"));

            const int master = view.addMaster(QStringLiteral("Обложка"));
            check(master == 0 && view.masterList().size() == 1,
                  QStringLiteral("master page created"));
            view.setCurrentPage(-1 - master);
            check(view.currentPage() < 0,
                  QStringLiteral("master page can be edited"));
            VectorItem *masterItem = view.addVectorObject(pageRect, false, false);
            check(masterItem->object().page == -1 - master,
                  QStringLiteral("objects can live on a master page"));

            view.setCurrentPage(0);
            view.applyMasterToPage(0, master);
            check(view.pageList().at(0).master == master,
                  QStringLiteral("master applied to page"));
            const QImage preview = view.renderPageImage(0, QSize(160, 200));
            check(!preview.isNull() && preview.width() == 160,
                  QStringLiteral("page thumbnail renders"));

            Document pagesDoc;
            pagesDoc.pages = view.pageList();
            pagesDoc.masters = view.masterList();
            pagesDoc.currentPage = 0;
            pagesDoc.objects = view.objects();
            const Document reloaded = Document::fromJson(pagesDoc.toJson(), nullptr);
            check(reloaded.pages.size() == 2 && reloaded.masters.size() == 1
                      && reloaded.pages.at(0).master == master,
                  QStringLiteral("pages and masters serialize"));
        }

        // Node editor: add/delete, close/open, break and align.
        view.setDocument(Document());
        VectorObject nodeShape;
        nodeShape.type = ShapeType::Rectangle;
        nodeShape.path.addRect(QRectF(0, 0, 120, 80));
        nodeShape.pos = QPointF(80, 80);
        VectorItem *nodeItem = view.addVectorObject(nodeShape, false, false);
        view.setActiveTool(Tool::Node);
        view.scene()->clearSelection();
        nodeItem->setSelected(true);
        const QPointF firstNodeScene = nodeItem->mapToScene(QPointF(0, 0));
        const QPoint firstNode = view.mapFromScene(firstNodeScene);
        QMouseEvent nodeClick(QEvent::MouseButtonPress, QPointF(firstNode),
                              view.viewport()->mapToGlobal(firstNode),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &nodeClick);
        QMouseEvent nodeRelease(QEvent::MouseButtonRelease, QPointF(firstNode),
                                view.viewport()->mapToGlobal(firstNode),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &nodeRelease);
        QApplication::processEvents();
        const int nodesBeforeAdd = nodeItem->pathElements().size();
        view.addNodeAtSelectedSegment();
        check(nodeItem->pathElements().size() > nodesBeforeAdd,
              QStringLiteral("node added on selected segment"));
        view.deleteSelectedNodes();
        check(nodeItem->pathElements().size() == nodesBeforeAdd,
              QStringLiteral("node deleted"));

        VectorObject openCurve;
        openCurve.type = ShapeType::Bezier;
        openCurve.path.moveTo(0, 0);
        openCurve.path.cubicTo(30, -30, 70, 30, 100, 0);
        openCurve.path.cubicTo(130, 40, 170, -40, 200, 0);
        openCurve.pos = QPointF(80, 300);
        VectorItem *openItem = view.addVectorObject(openCurve, false, false);
        view.scene()->clearSelection();
        openItem->setSelected(true);
        view.closeSelectedPath();
        check(!openItem->pathElements().isEmpty()
                  && QLineF(QPointF(openItem->pathElements().first().x,
                                    openItem->pathElements().first().y),
                            QPointF(openItem->pathElements().last().x,
                                    openItem->pathElements().last().y))
                             .length()
                         < 0.5,
              QStringLiteral("path closed"));
        view.openSelectedPath();
        check(QLineF(QPointF(openItem->pathElements().first().x,
                             openItem->pathElements().first().y),
                     QPointF(openItem->pathElements().last().x,
                             openItem->pathElements().last().y))
                      .length()
                  > 1.0,
              QStringLiteral("path opened"));

        // Select the middle anchor for break.
        const QPointF middleScene = openItem->mapToScene(QPointF(100, 0));
        const QPoint middle = view.mapFromScene(middleScene);
        QMouseEvent middlePress(QEvent::MouseButtonPress, QPointF(middle),
                                view.viewport()->mapToGlobal(middle),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &middlePress);
        QMouseEvent middleRelease(QEvent::MouseButtonRelease, QPointF(middle),
                                  view.viewport()->mapToGlobal(middle),
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &middleRelease);
        QApplication::processEvents();
        check(view.hasSelectedNodes(),
              QStringLiteral("middle node selected (element %1 of %2)")
                  .arg(view.selectedNodeElement())
                  .arg(openItem->pathElements().size()));
        const int beforeBreak = view.vectorItems().size();
        view.breakAtSelectedNode();
        check(view.vectorItems().size() == beforeBreak + 1,
              QStringLiteral("curve broken into two objects (%1 -> %2)")
                  .arg(beforeBreak).arg(view.vectorItems().size()));

        // Guides and snapping.
        view.setDocument(Document());
        view.setSnapEnabled(true);
        view.endGuideDrag(Qt::Vertical, 333.0);
        check(view.guidesX().contains(333.0), QStringLiteral("vertical guide added"));
        view.beginGuideDrag(Qt::Horizontal);
        view.updateGuideDrag(Qt::Horizontal, 222.0);
        view.endGuideDrag(Qt::Horizontal, 222.0);
        check(view.guidesY().contains(222.0), QStringLiteral("horizontal guide added"));

        VectorObject snapObject;
        snapObject.type = ShapeType::Rectangle;
        snapObject.path.addRect(QRectF(0, 0, 50, 50));
        snapObject.pos = QPointF(0, 100);
        VectorItem *snapItem = view.addVectorObject(snapObject, false, false);
        view.setActiveTool(Tool::Select);
        view.scene()->clearSelection();
        snapItem->setSelected(true);
        const QPoint snapStart = view.mapFromScene(QPointF(25, 125));
        drag(view, snapStart, snapStart + QPoint(3, 4));
        check(qAbs(snapItem->pos().x()) < 0.6 && qAbs(snapItem->pos().y() - 100.0) < 0.6,
              QStringLiteral("grid snapping aligns the object"));
        const int guidesBeforeClear = view.guidesX().size() + view.guidesY().size();
        view.clearGuides();
        check(guidesBeforeClear > 0 && view.guidesX().isEmpty() && view.guidesY().isEmpty(),
              QStringLiteral("guides cleared"));

        // Gradient fill.
        view.setDocument(Document());
        VectorObject gradientObject;
        gradientObject.type = ShapeType::Rectangle;
        gradientObject.path.addRect(QRectF(0, 0, 100, 80));
        VectorItem *gradientItem = view.addVectorObject(gradientObject, false, false);
        gradientItem->setGradientFill(1, QColor("#ff0000"), 45);
        check(gradientItem->object().fillStyle == 1
                  && gradientItem->object().fill2 == QColor("#ff0000"),
              QStringLiteral("gradient fill applied"));
        {
            Document gradientDoc;
            gradientDoc.objects = view.objects();
            check(gradientDoc.toSvg().contains(QStringLiteral("linearGradient")),
                  QStringLiteral("gradient exported to SVG"));
        }

        // Pivot point.
        gradientItem->setPivot(QPointF(0.0, 0.0));
        const QPointF pivot = gradientItem->mapToScene(gradientItem->pivotPoint());
        const QPointF topLeft =
            gradientItem->mapToScene(gradientItem->localBounds().topLeft());
        check(QLineF(pivot, topLeft).length() < 0.5,
              QStringLiteral("rotation pivot can be moved"));

        // Object lock.
        view.setDocument(Document());
        leftItem = view.addVectorObject(left, false, false);
        rightItem = view.addVectorObject(right, false, false);
        view.setItemLocked(leftItem, true);
        check(leftItem->isLocked(), QStringLiteral("object is locked"));
        check(!(leftItem->flags() & QGraphicsItem::ItemIsSelectable),
              QStringLiteral("locked object cannot be selected"));
        check(!(leftItem->flags() & QGraphicsItem::ItemIsMovable),
              QStringLiteral("locked object cannot be moved"));
        view.setItemLocked(leftItem, false);
        check(leftItem->flags() & QGraphicsItem::ItemIsMovable,
              QStringLiteral("unlocked object can be moved"));

        // Alt+click selects the object below the topmost one.
        view.setDocument(Document());
        VectorObject lower;
        lower.type = ShapeType::Rectangle;
        lower.path.addRect(QRectF(0, 0, 120, 120));
        lower.pos = QPointF(100, 100);
        VectorObject upper = lower;
        upper.pos = QPointF(140, 140);
        VectorItem *lowerItem = view.addVectorObject(lower, false, false);
        VectorItem *upperItem = view.addVectorObject(upper, false, false);
        view.setActiveTool(Tool::Select);
        view.scene()->clearSelection();
        upperItem->setSelected(true);
        const QPointF overlapScene(180, 180);
        const QPoint overlap = view.mapFromScene(overlapScene);
        QMouseEvent altClick(QEvent::MouseButtonPress, QPointF(overlap),
                             view.viewport()->mapToGlobal(overlap),
                             Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(view.viewport(), &altClick);
        QApplication::processEvents();
        const QList<VectorItem *> altSelected = view.selectedVectorItems();
        check(altSelected.size() == 1 && altSelected.first() == lowerItem,
              QStringLiteral("Alt+click selects the object behind"));
    }

    // Colour palette: right button means outline.
    {
        ColorPalette palette;
        palette.resize(600, 34);
        bool outline = false;
        QObject::connect(&palette, &ColorPalette::outlineColorPicked,
                         [&outline](const QColor &, const QString &) { outline = true; });
        QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(8, 17),
                               palette.mapToGlobal(QPoint(8, 17)),
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&palette, &rightPress);
        check(outline, QStringLiteral("right click on palette picks outline colour"));
    }

    // Page dialog defaults and canvas context menu.
    {
        PageDialog pageDialog;
        const QSizeF page = pageDialog.pageSizePx();
        check(qAbs(page.width() - 793.7) < 2.0
                  && qAbs(page.height() - 1122.5) < 2.0,
              QStringLiteral("page dialog defaults to A4"));

        view.setDocument(Document());
        VectorObject contextObject;
        contextObject.type = ShapeType::Rectangle;
        contextObject.path.addRect(QRectF(0, 0, 120, 80));
        contextObject.pos = QPointF(120, 120);
        VectorItem *contextItem = view.addVectorObject(contextObject, false, false);
        view.scene()->clearSelection();
        VectorItem *menuItem = nullptr;
        QObject::connect(&view, &CanvasView::contextMenuRequested,
                         [&menuItem](const QPoint &, VectorItem *item) { menuItem = item; });
        const QPoint itemPoint = view.mapFromScene(QPointF(180, 160));
        QContextMenuEvent itemMenu(QContextMenuEvent::Mouse, itemPoint,
                                   view.viewport()->mapToGlobal(itemPoint));
        QApplication::sendEvent(view.viewport(), &itemMenu);
        check(menuItem == contextItem && contextItem->isSelected(),
              QStringLiteral("right click selects the object for menu"));

        const QPoint emptyPoint = view.mapFromScene(QPointF(700, 900));
        QContextMenuEvent emptyMenu(QContextMenuEvent::Mouse, emptyPoint,
                                    view.viewport()->mapToGlobal(emptyPoint));
        QApplication::sendEvent(view.viewport(), &emptyMenu);
        check(menuItem == nullptr, QStringLiteral("empty right click reports no object"));
        check(view.selectedVectorItems().isEmpty(),
              QStringLiteral("empty right click clears selection"));
    }

    // Visual snapshot of the drawn scene with a selection frame.
    if (!view.vectorItems().isEmpty()) {
        view.setActiveTool(Tool::Select);
        view.scene()->clearSelection();
        view.vectorItems().last()->setSelected(true);
        QApplication::processEvents();
        view.grab().save(dir.filePath(QStringLiteral("canvas.png")));
    }

    out << "\n" << (g_failures == 0 ? "PASSED" : "FAILED") << ": " << g_failures
        << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
