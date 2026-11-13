#include "SelfTest.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

#include "CanvasView.h"
#include "Document.h"
#include "MainWindow.h"
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
            }
        }
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
