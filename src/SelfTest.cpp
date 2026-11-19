#include "SelfTest.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDockWidget>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMenu>
#include <QKeyEvent>
#include <QListWidget>
#include <QAbstractItemView>
#include <QSlider>
#include <QToolButton>
#include <QTabWidget>
#include <QMouseEvent>
#include <QPageSize>
#include <QProcess>
#include <QStandardPaths>
#include <QPainter>
#include <QPdfWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

#include "AssetLibrary.h"
#include "AssetPanel.h"
#include "CodeDialog.h"
#include "CodeGenerator.h"
#include "DocumentDialogs.h"
#include "DocumentTools.h"
#include "TemplateStore.h"
#include "ToolPalette.h"
#include "UpdateChecker.h"
#include "CanvasView.h"
#include "ColorPalette.h"
#include "Document.h"
#include "PatternStore.h"
#include "Patterns.h"
#include "Effects.h"
#include "CdrIO.h"
#include "FormatIO.h"
#include "VectorCraftIO.h"
#include "Icons.h"
#include "MainWindow.h"
#include "PageDialog.h"
#include "PatternDialog.h"
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
    // Keep asset libraries, settings and recent files of the real profile out
    // of the test run.
    QStandardPaths::setTestModeEnabled(true);
    qputenv("VECTORA_NO_RECOVERY", "1");
    qputenv("VECTORA_NO_UPDATE_CHECK", "1");
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

    // --- Interchange formats -------------------------------------------------
    out << "\nInterchange formats\n";
    {
        check(VectorFormats::formatForPath(QStringLiteral("a.esp"))
                  == VectorFormats::Format::Eps,
              QStringLiteral(".esp maps to EPS"));
        check(VectorFormats::formatForPath(QStringLiteral("a.vectorcraft"))
                  == VectorFormats::Format::Vectorcraft,
              QStringLiteral(".vectorcraft maps to the native document"));

        check(VectorFormats::formatForPath(QStringLiteral("a.cdr"))
                  == VectorFormats::Format::Cdr,
              QStringLiteral(".cdr maps to CorelDRAW"));
        check(VectorFormats::formatForPath(QStringLiteral("a.cmx"))
                  == VectorFormats::Format::Cmx,
              QStringLiteral(".cmx maps to Corel CMX"));
        check(!VectorFormats::canExport(VectorFormats::Format::Cmx)
                  && VectorFormats::canImport(VectorFormats::Format::Cmx),
              QStringLiteral("CMX is import-only while export is in development"));
        check(!VectorFormats::canExport(VectorFormats::Format::Cdr),
              QStringLiteral("CDR is import-only"));
        const QString fakeCdr = dir.filePath(QStringLiteral("fake.cdr"));
        QFile fakeFile(fakeCdr);
        fakeFile.open(QIODevice::WriteOnly);
        QByteArray cdrMagic("RIFF");
        cdrMagic.append(char(0x10));
        cdrMagic.append(3, '\0');
        cdrMagic.append("CDR9");
        fakeFile.write(cdrMagic);
        fakeFile.close();
        check(CdrIO::isCdrFile(fakeCdr),
              QStringLiteral("CDR magic is recognised"));

        // Optional real-world sample: VECTORA_CDR_SAMPLE=/path/file.cdr
        const QString cdrSample = qEnvironmentVariable("VECTORA_CDR_SAMPLE");
        if (!cdrSample.isEmpty() && QFileInfo::exists(cdrSample)) {
            Document cdrDocument;
            check(VectorFormats::importDocument(cdrSample, &cdrDocument, &error),
                  QStringLiteral("open real CDR sample"));
            check(!cdrDocument.pages.isEmpty() && !cdrDocument.objects.isEmpty(),
                  QStringLiteral("CDR sample has editable objects"));
        }

        const QString vectorcraftPath =
            dir.filePath(QStringLiteral("sample.vectorcraft"));
        check(VectorFormats::exportDocument(document, vectorcraftPath, &error),
              QStringLiteral("export Vectorcraft"));
        check(VectorCraftIO::isVectorCraftFile(vectorcraftPath),
              QStringLiteral("Vectorcraft file uses the VectorCraft schema"));
        Document vectorcraftRead;
        check(VectorFormats::importDocument(vectorcraftPath, &vectorcraftRead, &error),
              QStringLiteral("open Vectorcraft"));
        check(vectorcraftRead.objects.size() == document.objects.size(),
              QStringLiteral("Vectorcraft keeps every object"));

        // Minimal VectorCraft document with two artboards, a red rectangle on
        // the first and point text on the second.
        QJsonObject firstArtboard;
        firstArtboard.insert(QStringLiteral("id"), 1);
        firstArtboard.insert(QStringLiteral("name"), QStringLiteral("Left"));
        QJsonObject firstRect;
        firstRect.insert(QStringLiteral("x0"), 0.0);
        firstRect.insert(QStringLiteral("y0"), 0.0);
        firstRect.insert(QStringLiteral("x1"), 200.0);
        firstRect.insert(QStringLiteral("y1"), 100.0);
        firstArtboard.insert(QStringLiteral("rect"), firstRect);
        QJsonObject secondArtboard;
        secondArtboard.insert(QStringLiteral("id"), 2);
        secondArtboard.insert(QStringLiteral("name"), QStringLiteral("Right"));
        QJsonObject secondRect;
        secondRect.insert(QStringLiteral("x0"), 220.0);
        secondRect.insert(QStringLiteral("y0"), 0.0);
        secondRect.insert(QStringLiteral("x1"), 320.0);
        secondRect.insert(QStringLiteral("y1"), 100.0);
        secondArtboard.insert(QStringLiteral("rect"), secondRect);
        QJsonArray artboards;
        artboards.append(firstArtboard);
        artboards.append(secondArtboard);

        QJsonArray anchors;
        const QVector<QPair<double, double>> corners = {
            {10.0, 10.0}, {60.0, 10.0}, {60.0, 50.0}, {10.0, 50.0}};
        for (const auto &corner : corners) {
            QJsonArray point;
            point.append(corner.first);
            point.append(corner.second);
            QJsonObject anchor;
            anchor.insert(QStringLiteral("p"), point);
            anchors.append(anchor);
        }
        QJsonObject subpath;
        subpath.insert(QStringLiteral("anchors"), anchors);
        subpath.insert(QStringLiteral("closed"), true);
        QJsonArray subpaths;
        subpaths.append(subpath);
        QJsonObject pathObject;
        pathObject.insert(QStringLiteral("subpaths"), subpaths);
        QJsonObject pathKind;
        pathKind.insert(QStringLiteral("type"), QStringLiteral("path"));
        pathKind.insert(QStringLiteral("path"), pathObject);
        QJsonObject redColor;
        redColor.insert(QStringLiteral("model"), QStringLiteral("rgb"));
        redColor.insert(QStringLiteral("r"), 1.0);
        redColor.insert(QStringLiteral("g"), 0.0);
        redColor.insert(QStringLiteral("b"), 0.0);
        QJsonObject redPaint;
        redPaint.insert(QStringLiteral("type"), QStringLiteral("solid"));
        redPaint.insert(QStringLiteral("color"), redColor);
        QJsonObject fillItem;
        fillItem.insert(QStringLiteral("kind"), QStringLiteral("fill"));
        fillItem.insert(QStringLiteral("paint"), redPaint);
        QJsonArray appearanceItems;
        appearanceItems.append(fillItem);
        QJsonObject appearance;
        appearance.insert(QStringLiteral("items"), appearanceItems);
        QJsonObject pathNode;
        pathNode.insert(QStringLiteral("id"), 2);
        pathNode.insert(QStringLiteral("appearance"), appearance);
        pathNode.insert(QStringLiteral("kind"), pathKind);

        QJsonObject textKindInner;
        textKindInner.insert(QStringLiteral("kind"), QStringLiteral("point"));
        QJsonArray textTransform;
        for (double value : {1.0, 0.0, 0.0, 1.0, 240.0, 40.0})
            textTransform.append(value);
        QJsonObject textStyle;
        textStyle.insert(QStringLiteral("font_family"), QStringLiteral("Sans"));
        textStyle.insert(QStringLiteral("font_style"), QStringLiteral("Regular"));
        textStyle.insert(QStringLiteral("size"), 12.0);
        QJsonObject blackPaint = redPaint;
        blackPaint.insert(QStringLiteral("color"), QJsonObject{
            {QStringLiteral("model"), QStringLiteral("rgb")},
            {QStringLiteral("r"), 0.0},
            {QStringLiteral("g"), 0.0},
            {QStringLiteral("b"), 0.0}});
        textStyle.insert(QStringLiteral("fill"), blackPaint);
        QJsonObject nonePaint;
        nonePaint.insert(QStringLiteral("type"), QStringLiteral("none"));
        textStyle.insert(QStringLiteral("stroke"), nonePaint);
        textStyle.insert(QStringLiteral("stroke_width"), 0.0);
        QJsonObject run;
        run.insert(QStringLiteral("text"), QStringLiteral("Hi"));
        run.insert(QStringLiteral("style"), textStyle);
        QJsonArray runs;
        runs.append(run);
        QJsonObject para;
        para.insert(QStringLiteral("justify"), QStringLiteral("Left"));
        QJsonObject textKind;
        textKind.insert(QStringLiteral("type"), QStringLiteral("text"));
        textKind.insert(QStringLiteral("kind"), textKindInner);
        textKind.insert(QStringLiteral("xf"), textTransform);
        textKind.insert(QStringLiteral("runs"), runs);
        textKind.insert(QStringLiteral("para"), para);
        QJsonObject textNode;
        textNode.insert(QStringLiteral("id"), 3);
        textNode.insert(QStringLiteral("kind"), textKind);

        QJsonArray children;
        children.append(pathNode);
        children.append(textNode);
        QJsonObject layerKind;
        layerKind.insert(QStringLiteral("type"), QStringLiteral("layer"));
        layerKind.insert(QStringLiteral("color"), QJsonObject{
            {QStringLiteral("Preset"), 0}});
        layerKind.insert(QStringLiteral("template"), false);
        layerKind.insert(QStringLiteral("printable"), true);
        layerKind.insert(QStringLiteral("children"), children);
        QJsonObject layer;
        layer.insert(QStringLiteral("id"), 1);
        layer.insert(QStringLiteral("name"), QStringLiteral("Layer 1"));
        layer.insert(QStringLiteral("kind"), layerKind);
        QJsonArray layers;
        layers.append(layer);

        QJsonObject documentBody;
        documentBody.insert(QStringLiteral("version"), 1);
        documentBody.insert(QStringLiteral("title"), QStringLiteral("Fixture"));
        documentBody.insert(QStringLiteral("units"), QStringLiteral("Points"));
        documentBody.insert(QStringLiteral("artboards"), artboards);
        documentBody.insert(QStringLiteral("layers"), layers);
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("vectorcraft"));
        root.insert(QStringLiteral("version"), 4);
        root.insert(QStringLiteral("generator"), QStringLiteral("self-test"));
        root.insert(QStringLiteral("document"), documentBody);
        root.insert(QStringLiteral("images"), QJsonObject());

        const QString fixturePath = dir.filePath(QStringLiteral("fixture.vectorcraft"));
        QSaveFile fixture(fixturePath);
        check(fixture.open(QIODevice::WriteOnly), QStringLiteral("write VectorCraft fixture"));
        fixture.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        check(fixture.commit(), QStringLiteral("commit VectorCraft fixture"));
        Document fixtureRead;
        check(VectorFormats::importDocument(fixturePath, &fixtureRead, &error),
              QStringLiteral("open VectorCraft fixture"));
        check(fixtureRead.pages.size() == 2
                  && qAbs(fixtureRead.pages.at(0).size.width() - 266.67) < 0.5
                  && qAbs(fixtureRead.pages.at(1).size.width() - 133.33) < 0.5,
              QStringLiteral("VectorCraft artboards become pages"));
        check(fixtureRead.objects.size() == 2,
              QStringLiteral("VectorCraft nodes become objects"));
        bool redRectangle = false;
        bool pointText = false;
        for (const VectorObject &object : fixtureRead.objects) {
            if (object.page == 0 && object.type == ShapeType::Freehand
                && object.fill == QColor(255, 0, 0)
                && qAbs(object.path.boundingRect().left() - 13.33) < 0.5) {
                redRectangle = true;
            }
            if (object.page == 1 && object.type == ShapeType::Text
                && object.text == QLatin1String("Hi")) {
                pointText = true;
            }
        }
        check(redRectangle, QStringLiteral("VectorCraft path keeps its shape and colour"));
        check(pointText, QStringLiteral("VectorCraft text lands on its artboard"));

        const QString cmxPath = dir.filePath(QStringLiteral("sample.cmx"));
        error.clear();
        check(!VectorFormats::exportDocument(document, cmxPath, &error),
              QStringLiteral("CMX export is disabled"));
        check(error.contains(QStringLiteral("разработке")),
              QStringLiteral("CMX export reports work in progress"));

        // Optional real-world sample: VECTORA_CMX_SAMPLE=/path/file.cmx
        const QString cmxSample = qEnvironmentVariable("VECTORA_CMX_SAMPLE");
        if (!cmxSample.isEmpty() && QFileInfo::exists(cmxSample)) {
            Document cmxRead;
            check(VectorFormats::importDocument(cmxSample, &cmxRead, &error),
                  QStringLiteral("open real CMX sample"));
            check(!cmxRead.pages.isEmpty() && !cmxRead.objects.isEmpty(),
                  QStringLiteral("CMX sample has editable objects"));
        }

        const QString epsPath = dir.filePath(QStringLiteral("sample.eps"));
        check(VectorFormats::exportDocument(document, epsPath, &error),
              QStringLiteral("export EPS"));
        QFile epsFile(epsPath);
        epsFile.open(QIODevice::ReadOnly);
        const QByteArray epsBytes = epsFile.readAll();
        check(epsBytes.startsWith("%!PS-Adobe-3.0 EPSF-3.0"),
              QStringLiteral("EPS has a PostScript header"));
        check(epsBytes.contains("%%BoundingBox:")
                  && epsBytes.contains(" setrgbcolor")
                  && epsBytes.contains("fill\n")
                  && epsBytes.contains("stroke\n"),
              QStringLiteral("EPS contains vector paint commands"));

        Document epsRead;
        check(VectorFormats::importDocument(epsPath, &epsRead, &error),
              QStringLiteral("open EPS"));
        check(!epsRead.objects.isEmpty(),
              QStringLiteral("EPS import returns editable paths"));
        check(epsRead.pages.size() == 1
                  && epsRead.pageSize.width() > 100.0
                  && epsRead.pageSize.height() > 100.0,
              QStringLiteral("EPS page size comes from the bounding box"));
        Document shapesDocument;
        shapesDocument.pageSize = QSizeF(600.0, 800.0);
        VectorObject boxShape;
        boxShape.type = ShapeType::Rectangle;
        boxShape.path.addRect(QRectF(0, 0, 200, 120));
        boxShape.pos = QPointF(40, 50);
        boxShape.page = -9999;
        boxShape.layer = -1;
        boxShape.hasStroke = false;
        VectorObject roundShape;
        roundShape.type = ShapeType::Ellipse;
        roundShape.path.addEllipse(QRectF(0, 0, 120, 120));
        roundShape.pos = QPointF(400, 300);
        roundShape.page = -9999;
        roundShape.layer = -1;
        roundShape.hasStroke = false;
        shapesDocument.objects = {boxShape, roundShape};
        const QString shapesEps = dir.filePath(QStringLiteral("shapes.eps"));
        check(VectorFormats::exportDocument(shapesDocument, shapesEps, &error),
              QStringLiteral("export EPS shapes"));
        Document shapesRead;
        check(VectorFormats::importDocument(shapesEps, &shapesRead, &error),
              QStringLiteral("open EPS shapes"));
        QRectF sourceBox = boxShape.localBounds().translated(boxShape.pos)
                               .united(roundShape.localBounds().translated(roundShape.pos));
        QRectF importedBox;
        for (const VectorObject &object : shapesRead.objects) {
            const QRectF bounds = object.localBounds().translated(object.pos);
            importedBox = importedBox.isNull() ? bounds : importedBox.united(bounds);
        }
        check(qAbs(sourceBox.width() - importedBox.width()) < 1.5
                  && qAbs(sourceBox.height() - importedBox.height()) < 1.5
                  && qAbs(sourceBox.left() - importedBox.left()) < 1.5
                  && qAbs(sourceBox.top() - importedBox.top()) < 1.5,
              QStringLiteral("EPS keeps the artwork geometry"));

        const QString aiPath = dir.filePath(QStringLiteral("sample.ai"));
        check(VectorFormats::exportDocument(document, aiPath, &error),
              QStringLiteral("export AI"));
        QFile aiFile(aiPath);
        aiFile.open(QIODevice::ReadOnly);
        const QByteArray aiBytes = aiFile.readAll();
        check(aiBytes.startsWith("%PDF-"),
              QStringLiteral("AI is a PDF-compatible Illustrator file"));

#if defined(VECTORA_HAVE_QT_PDF)
        Document aiRead;
        check(VectorFormats::importDocument(aiPath, &aiRead, &error),
              QStringLiteral("open AI"));
        check(aiRead.pages.size() == 1 && !aiRead.objects.isEmpty(),
              QStringLiteral("AI import returns objects"));

        Document pdfRead;
        check(VectorFormats::importDocument(pdfPath, &pdfRead, &error),
              QStringLiteral("open PDF"));
        check(pdfRead.pages.size() == 1 && !pdfRead.objects.isEmpty(),
              QStringLiteral("PDF import returns objects"));
        check(qAbs(pdfRead.pageSize.width() - document.pageSize.width()) < 4.0
                  && qAbs(pdfRead.pageSize.height() - document.pageSize.height()) < 4.0,
              QStringLiteral("PDF import keeps the page size"));

        Document multipage;
        PageInfo first;
        first.name = QStringLiteral("Первая");
        first.size = QSizeF(300.0, 400.0);
        PageInfo second;
        second.name = QStringLiteral("Вторая");
        second.size = QSizeF(420.0, 260.0);
        multipage.pages = {first, second};
        multipage.pageSize = first.size;
        VectorObject rectangle;
        rectangle.type = ShapeType::Rectangle;
        rectangle.path.addRect(QRectF(0, 0, 100, 80));
        rectangle.pos = QPointF(30, 40);
        rectangle.page = 0;
        rectangle.layer = 0;
        rectangle.hasStroke = false;
        VectorObject ellipse;
        ellipse.type = ShapeType::Ellipse;
        ellipse.path.addEllipse(QRectF(0, 0, 120, 120));
        ellipse.pos = QPointF(50, 60);
        ellipse.page = 1;
        ellipse.layer = 0;
        ellipse.hasStroke = false;
        multipage.objects = {rectangle, ellipse};

        const QString multiPath = dir.filePath(QStringLiteral("multi.pdf"));
        check(VectorFormats::exportDocument(multipage, multiPath, &error),
              QStringLiteral("export multipage PDF"));
        Document multiRead;
        check(VectorFormats::importDocument(multiPath, &multiRead, &error),
              QStringLiteral("open multipage PDF"));
        check(multiRead.pages.size() == 2,
              QStringLiteral("PDF import keeps every page"));
        check(multiRead.pages.size() == 2
                  && multiRead.pages.at(1).size.width() > multiRead.pages.at(1).size.height(),
              QStringLiteral("PDF import keeps portrait and landscape pages"));
#endif
    }

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

    // --- Non-destructive effects and blends --------------------------------
    out << "\nEffects\n";
    {
        CanvasView fxView;
        fxView.resize(900, 1200);
        fxView.setPageSize(QSizeF(400, 400));

        VectorObject card;
        card.type = ShapeType::Rectangle;
        card.id = 101;
        card.path.addRect(QRectF(0, 0, 120, 90));
        card.pos = QPointF(100, 100);
        card.fill = QColor("#e0466b");
        card.strokeWidth = 0;
        card.hasStroke = false;

        VectorObject other = card;
        other.id = 102;
        other.pos = QPointF(220, 260);
        other.fill = QColor("#4f7cc0");

        VectorItem *cardItem = fxView.addVectorObject(card, false, false);
        VectorItem *otherItem = fxView.addVectorObject(other, false, false);
        check(cardItem && otherItem, QStringLiteral("effect test objects added"));

        // Stack: add, reorder, toggle.
        fxView.scene()->clearSelection();
        cardItem->setSelected(true);
        fxView.addEffectToSelection(makeEffect(QStringLiteral("shadow")));
        fxView.addEffectToSelection(makeEffect(QStringLiteral("glow")));
        QVector<Effect> stack = cardItem->effects();
        check(stack.size() == 2, QStringLiteral("two effects on one object"));
        check(stack.at(0).type == QStringLiteral("shadow")
                  && stack.at(1).type == QStringLiteral("glow"),
              QStringLiteral("effects keep insertion order"));
        fxView.moveEffectInSelection(1, -1);
        stack = cardItem->effects();
        check(stack.at(0).type == QStringLiteral("glow"),
              QStringLiteral("effect order can be changed"));
        fxView.moveEffectInSelection(0, 1);
        check(cardItem->effects().at(1).type == QStringLiteral("glow"),
              QStringLiteral("effect order restored"));

        stack = cardItem->effects();
        stack[0].enabled = false;
        fxView.setSelectionEffects(stack);
        check(!cardItem->effects().at(0).enabled,
              QStringLiteral("effect can be disabled"));
        stack[0].enabled = true;
        fxView.setSelectionEffects(stack);
        check(qAbs(effectBounds(cardItem->object()).width()
                   - cardItem->object().localBounds().width()) > 10.0,
              QStringLiteral("effect bounds expand past geometry"));

        // Rendering: the glow reaches outside the original rectangle.
        Document glowDoc;
        glowDoc.pageSize = QSizeF(400, 400);
        glowDoc.objects.append(cardItem->object());
        QImage glowImage(400, 400, QImage::Format_ARGB32_Premultiplied);
        glowImage.fill(Qt::white);
        {
            QPainter painter(&glowImage);
            glowDoc.render(&painter);
        }
        Document plainDoc = glowDoc;
        plainDoc.objects[0].effects.clear();
        QImage plainImage(400, 400, QImage::Format_ARGB32_Premultiplied);
        plainImage.fill(Qt::white);
        {
            QPainter painter(&plainImage);
            plainDoc.render(&painter);
        }
        const QColor outside = glowImage.pixelColor(228, 145);
        const QColor plainOutside = plainImage.pixelColor(228, 145);
        check(outside != plainOutside,
              QStringLiteral("shadow renders outside the geometry"));
        check(glowImage.pixelColor(150, 140) == QColor("#e0466b"),
              QStringLiteral("effect layer keeps the original fill"));

        // Perspective quad + serialization round-trip.
        Effect perspective = makeEffect(QStringLiteral("perspective"));
        perspective.quad = QPolygonF({QPointF(0.1, 0.0), QPointF(0.9, 0.05),
                                      QPointF(1.0, 1.0), QPointF(0.0, 0.95)});
        stack = cardItem->effects();
        stack.append(perspective);
        cardItem->setEffects(stack);

        Effect mask = makeEffect(QStringLiteral("clip-mask"));
        QPainterPath maskPath;
        maskPath.addRect(QRectF(0, 0, 60, 90));
        mask.mask = maskPath;
        stack = cardItem->effects();
        stack.append(mask);
        cardItem->setEffects(stack);

        Document stored;
        stored.pageSize = QSizeF(400, 400);
        stored.objects.append(cardItem->object());
        stored.blends.append(BlendRecord{7, 101, 102, 5, 0.0, true});
        const QString effectPath = dir.filePath(QStringLiteral("effects.vct"));
        check(stored.save(effectPath, &error),
              QStringLiteral("document with effects saved"));
        Document reloaded;
        check(reloaded.load(effectPath, &error),
              QStringLiteral("document with effects loaded"));
        check(reloaded.objects.size() == 1
                  && reloaded.objects.at(0).effects.size() == 4,
              QStringLiteral("effect stack persists"));
        if (reloaded.objects.size() == 1) {
            const QVector<Effect> restored = reloaded.objects.at(0).effects;
            check(restored.at(0).type == QStringLiteral("shadow")
                      && restored.at(1).type == QStringLiteral("glow")
                      && restored.at(2).type == QStringLiteral("perspective")
                      && restored.at(3).type == QStringLiteral("clip-mask"),
                  QStringLiteral("effect order persists"));
            check(restored.at(2).quad.size() == 4
                      && qAbs(restored.at(2).quad.at(1).x() - 0.9) < 0.001,
                  QStringLiteral("perspective corners persist"));
            check(!restored.at(3).mask.isEmpty(),
                  QStringLiteral("mask geometry persists"));
        }
        check(reloaded.blends.size() == 1
                  && reloaded.blends.at(0).steps == 5
                  && reloaded.blends.at(0).fromId == 101,
              QStringLiteral("blend record persists"));

        // On-canvas perspective corners.
        {
            fxView.setActiveTool(Tool::Perspective);
            const QRectF bounds = cardItem->object().localBounds();
            const QPoint centre = fxView.mapFromScene(
                cardItem->mapToScene(bounds.center()));
            sendMouse(fxView.viewport(), QEvent::MouseButtonPress, centre,
                      Qt::LeftButton, Qt::LeftButton);
            sendMouse(fxView.viewport(), QEvent::MouseButtonRelease, centre,
                      Qt::LeftButton, Qt::NoButton);
            int perspectiveIndex = -1;
            for (int i = 0; i < cardItem->effects().size(); ++i) {
                if (cardItem->effects().at(i).type == QStringLiteral("perspective"))
                    perspectiveIndex = i;
            }
            check(perspectiveIndex >= 0,
                  QStringLiteral("perspective tool attaches an effect"));

            const QPoint corner = fxView.mapFromScene(
                cardItem->mapToScene(QPointF(bounds.right(), bounds.bottom())));
            drag(fxView, corner, corner + QPoint(-42, -34));
            if (perspectiveIndex >= 0) {
                const QPolygonF quad = cardItem->effects().at(perspectiveIndex).quad;
                check(quad.size() == 4
                          && (qAbs(quad.at(2).x() - 1.0) > 0.05
                              || qAbs(quad.at(2).y() - 1.0) > 0.05),
                      QStringLiteral("perspective corner is dragged on canvas"));
            }
            fxView.setActiveTool(Tool::Select);
        }

        // Extrusion by dragging from the object.
        {
            fxView.setActiveTool(Tool::Extrude);
            const QPoint start = fxView.mapFromScene(
                cardItem->mapToScene(cardItem->object().localBounds().center()));
            drag(fxView, start, start + QPoint(0, 70));
            bool extruded = false;
            for (const Effect &effect : cardItem->effects()) {
                if (effect.type == QStringLiteral("extrude") && effect.distance > 20.0)
                    extruded = true;
            }
            check(extruded, QStringLiteral("extrude drag sets depth and angle"));
            fxView.setActiveTool(Tool::Select);
        }

        // Blend regeneration from live endpoints.
        fxView.scene()->clearSelection();
        cardItem->setSelected(true);
        otherItem->setSelected(true);
        fxView.blendSelected(3);
        check(fxView.blends().size() == 1, QStringLiteral("blend record created"));
        int generated = 0;
        for (VectorItem *item : fxView.vectorItems()) {
            if (item->object().blendId != 0)
                ++generated;
        }
        check(generated == 3, QStringLiteral("blend generated requested steps"));

        QPointF beforePos;
        for (VectorItem *item : fxView.vectorItems()) {
            if (item->object().blendId != 0) {
                beforePos = item->pos();
                break;
            }
        }
        otherItem->setPos(otherItem->pos() + QPointF(120, 0));
        fxView.updateAllBlends();
        QPointF afterPos;
        for (VectorItem *item : fxView.vectorItems()) {
            if (item->object().blendId != 0) {
                afterPos = item->pos();
                break;
            }
        }
        check(QLineF(beforePos, afterPos).length() > 10.0,
              QStringLiteral("moving an endpoint regenerates the blend"));

        fxView.setBlendSteps(fxView.blends().at(0).id, 5);
        generated = 0;
        for (VectorItem *item : fxView.vectorItems()) {
            if (item->object().blendId != 0)
                ++generated;
        }
        check(generated == 5, QStringLiteral("changing blend steps updates result"));

        fxView.setBlendSpacing(fxView.blends().at(0).id, 80.0);
        generated = 0;
        for (VectorItem *item : fxView.vectorItems()) {
            if (item->object().blendId != 0)
                ++generated;
        }
        check(generated >= 1, QStringLiteral("blend spacing produces objects"));

        // Clip mask limits painting to the mask shape.
        Document maskDoc;
        maskDoc.pageSize = QSizeF(400, 400);
        VectorObject masked;
        masked.type = ShapeType::Rectangle;
        masked.path.addRect(QRectF(0, 0, 120, 90));
        masked.pos = QPointF(100, 100);
        masked.fill = QColor("#00a86b");
        masked.hasStroke = false;
        Effect clip = makeEffect(QStringLiteral("clip-mask"));
        QPainterPath clipPath;
        clipPath.addRect(QRectF(0, 0, 60, 90));
        clip.mask = clipPath;
        masked.effects.append(clip);
        maskDoc.objects.append(masked);
        QImage maskImage(400, 400, QImage::Format_ARGB32_Premultiplied);
        maskImage.fill(Qt::white);
        {
            QPainter painter(&maskImage);
            maskDoc.render(&painter);
        }
        const QColor insideMask = maskImage.pixelColor(120, 140);
        const QColor outsideMask = maskImage.pixelColor(190, 140);
        check(insideMask != QColor(Qt::white)
                  && outsideMask == QColor(Qt::white),
              QStringLiteral("clip mask hides geometry outside the mask"));

        const QString effectSvg = maskDoc.toSvg();
        check(effectSvg.contains(QStringLiteral("data:image/png;base64,")),
              QStringLiteral("effects exported to SVG as a flattened layer"));

        // Both new tools are registered with shortcuts and icons.
        bool hasExtrude = false;
        bool hasPerspective = false;
        for (const ToolInfo &info : toolInfos()) {
            if (info.tool == Tool::Extrude)
                hasExtrude = !info.shortcut.isEmpty()
                             && !Icons::icon(info.id).isNull();
            if (info.tool == Tool::Perspective)
                hasPerspective = !info.shortcut.isEmpty()
                                 && !Icons::icon(info.id).isNull();
        }
        check(hasExtrude, QStringLiteral("extrude tool registered"));
        check(hasPerspective, QStringLiteral("perspective tool registered"));
        check(!Icons::appIcon().isNull(), QStringLiteral("application icon available"));
    }

    // QR code with an embedded centre logo.
    {
        const CodeSymbol symbol =
            generateCode(CodeKind::Qr, QStringLiteral("https://vectora.example"), 3);
        check(symbol.valid, QStringLiteral("QR payload generated"));
        VectorObject code;
        code.type = ShapeType::Code;
        code.codeKind = int(CodeKind::Qr);
        code.codeEcc = 3;
        code.codeMargin = 4;
        code.codeModulesX = symbol.modulesX;
        code.codeModulesY = symbol.modulesY;
        code.codeCells = symbol.cells;
        code.codeHumanText = symbol.humanText;
        code.textBox = QRectF(0, 0, 240, 240);
        code.codeBackground = Qt::white;
        code.codeColor = Qt::black;
        QImage logo(48, 48, QImage::Format_ARGB32_Premultiplied);
        logo.fill(Qt::red);
        code.codeLogo = logo;
        code.codeLogoScale = 0.2;

        QImage render(260, 260, QImage::Format_ARGB32_Premultiplied);
        render.fill(Qt::white);
        {
            QPainter painter(&render);
            painter.translate(10, 10);
            Document::paintObject(&painter, code);
        }
        const QColor center = render.pixelColor(130, 130);
        check(center.red() > 180 && center.green() < 100 && center.blue() < 100,
              QStringLiteral("QR centre logo is painted"));
        const QColor corner = render.pixelColor(12, 12);
        check(corner == QColor(Qt::black) || corner == QColor(Qt::white),
              QStringLiteral("QR modules remain around the logo"));

        const VectorObject restored =
            vectorObjectFromJson(vectorObjectToJson(code));
        check(!restored.codeLogo.isNull()
                  && restored.codeModulesX == code.codeModulesX
                  && qAbs(restored.codeLogoScale - 0.2) < 0.001,
              QStringLiteral("QR centre logo serializes"));
    }

    // Hit testing must not rebuild stroke/union for large paths on every
    // hover: the old code froze documents with hundreds of curves.
    {
        VectorObject complex;
        complex.type = ShapeType::Bezier;
        complex.fill = QColor("#4f7cc0");
        complex.hasFill = true;
        QPainterPath path;
        path.moveTo(0, 0);
        for (int i = 0; i < 1200; ++i) {
            const qreal x = i * 2.0;
            const qreal y = qreal(i % 30) * 3.0;
            path.cubicTo(x, y, x + 5.0, y + 10.0, x + 10.0, y);
        }
        complex.path = path;
        VectorItem item(complex);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i)
            (void)item.shape();
        check(timer.elapsed() < 500,
              QStringLiteral("complex hit shape is cached"));
    }

    // --- Symbols, instances and the asset library ---------------------------
    out << "\nSymbols and library\n";
    {
        CanvasView symbolView;
        symbolView.resize(900, 1200);
        symbolView.setPageSize(QSizeF(500, 500));

        VectorObject first;
        first.type = ShapeType::Rectangle;
        first.path.addRect(QRectF(0, 0, 80, 60));
        first.pos = QPointF(60, 60);
        first.fill = QColor("#e0466b");
        first.hasStroke = false;

        VectorObject second;
        second.type = ShapeType::Ellipse;
        second.path.addEllipse(QRectF(0, 0, 60, 60));
        second.pos = QPointF(160, 60);
        second.fill = QColor("#4f7cc0");
        second.hasStroke = false;

        VectorItem *firstItem = symbolView.addVectorObject(first, false, false);
        VectorItem *secondItem = symbolView.addVectorObject(second, false, false);
        check(firstItem && secondItem, QStringLiteral("symbol source objects added"));

        symbolView.scene()->clearSelection();
        firstItem->setSelected(true);
        secondItem->setSelected(true);
        const int symbolId = symbolView.createSymbolFromSelection(QStringLiteral("Тест"));
        check(symbolId > 0, QStringLiteral("symbol created from selection"));
        check(symbolView.symbols().size() == 1,
              QStringLiteral("symbol stored in the document"));
        check(symbolView.instances().size() == 1,
              QStringLiteral("first instance recorded"));
        const int firstInstance = symbolView.instances().at(0).id;
        check(firstItem->object().instanceId == firstInstance
                  && firstItem->object().symbolId == symbolId,
              QStringLiteral("source objects became a linked instance"));
        check(firstItem->object().group != 0,
              QStringLiteral("instance objects are grouped"));

        const int beforeInsert = symbolView.vectorItems().size();
        const int inserted = symbolView.insertSymbol(symbolId);
        check(inserted > 0, QStringLiteral("symbol instance inserted"));
        check(symbolView.vectorItems().size() == beforeInsert + 2,
              QStringLiteral("instance copies every symbol object"));
        check(symbolView.instances().size() == 2,
              QStringLiteral("second instance recorded"));
        check(!symbolView.renderSymbolImage(symbolId, QSize(64, 64)).isNull(),
              QStringLiteral("symbol preview renders"));

        // Edit the first instance, push it back into the symbol definition and
        // confirm the other instance is rebuilt from it.
        symbolView.scene()->clearSelection();
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == firstInstance)
                item->setSelected(true);
        }
        check(symbolView.selectedInstanceId() == firstInstance,
              QStringLiteral("instance selection detected"));
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == firstInstance
                && item->object().type == ShapeType::Rectangle) {
                item->setFillColor(QColor("#06d6a0"));
            }
        }
        check(symbolView.updateSymbolFromSelection(),
              QStringLiteral("symbol updated from its instance"));
        bool otherUpdated = false;
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == inserted
                && item->object().type == ShapeType::Rectangle
                && item->object().fill == QColor("#06d6a0"))
                otherUpdated = true;
        }
        check(otherUpdated,
              QStringLiteral("linked instances follow the symbol change"));

        // Independent instance scaling keeps the definition untouched.
        QRectF insertedBounds;
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == inserted) {
                const QRectF box = item->mapToScene(item->object().localBounds())
                                       .boundingRect();
                insertedBounds = insertedBounds.isNull() ? box : insertedBounds.united(box);
            }
        }
        const QRectF definitionBounds = symbolView.symbols().first().bounds;
        check(insertedBounds.isValid() && definitionBounds.isValid(),
              QStringLiteral("instance and definition bounds available"));
        VectorItem *scaledItem = nullptr;
        qreal originalLocalWidth = 0.0;
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == inserted
                && !item->object().path.isEmpty()) {
                scaledItem = item;
                originalLocalWidth = item->object().localBounds().width();
                break;
            }
        }
        if (scaledItem) {
            VectorObject object = scaledItem->object();
            const QRectF local = object.localBounds();
            QTransform scale;
            scale.translate(local.center().x(), local.center().y());
            scale.scale(1.5, 1.5);
            scale.translate(-local.center().x(), -local.center().y());
            object.path = scale.map(object.path);
            scaledItem->setObject(object);
        }
        check(scaledItem != nullptr
                  && scaledItem->object().localBounds().width()
                         > originalLocalWidth * 1.4,
              QStringLiteral("instance scales independently"));
        check(symbolView.symbols().first().bounds == definitionBounds,
              QStringLiteral("instance scaling leaves the definition unchanged"));

        // Detach the scaled instance; it stops following the symbol.
        symbolView.scene()->clearSelection();
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == inserted)
                item->setSelected(true);
        }
        symbolView.detachSelectedSymbolInstances();
        check(symbolView.instances().size() == 1,
              QStringLiteral("detached instance record removed"));
        bool stillLinked = false;
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().instanceId == inserted)
                stillLinked = true;
        }
        check(!stillLinked, QStringLiteral("detached objects lose the symbol link"));

        // Serialisation of symbols, instances and links.
        Document symbolDocument;
        symbolDocument.pageSize = symbolView.pageSize();
        symbolDocument.objects = symbolView.objects();
        symbolDocument.groups = symbolView.groups();
        symbolDocument.symbols = symbolView.symbols();
        symbolDocument.instances = symbolView.instances();
        const QString symbolsPath = dir.filePath(QStringLiteral("symbols.vct"));
        check(symbolDocument.save(symbolsPath, &error),
              QStringLiteral("document with symbols saved"));
        Document symbolLoaded;
        check(symbolLoaded.load(symbolsPath, &error),
              QStringLiteral("document with symbols loaded"));
        check(symbolLoaded.symbols.size() == 1
                  && symbolLoaded.instances.size() == 1,
              QStringLiteral("symbols and instances persist"));
        check(symbolLoaded.symbols.at(0).objects.size() == 2,
              QStringLiteral("symbol artwork persists"));
        bool linkPersisted = false;
        for (const VectorObject &object : symbolLoaded.objects) {
            if (object.symbolId == symbolId && object.instanceId > 0)
                linkPersisted = true;
        }
        check(linkPersisted, QStringLiteral("instance links persist"));

        symbolView.deleteSymbol(symbolId);
        check(symbolView.symbols().isEmpty(),
              QStringLiteral("symbol removed from the document"));
        bool unlinked = true;
        for (VectorItem *item : symbolView.vectorItems()) {
            if (item->object().symbolId != 0 || item->object().instanceId != 0)
                unlinked = false;
        }
        check(unlinked, QStringLiteral("deleting a symbol unlinks its instances"));

        // Asset insertion from SVG markup, the same path used by drag & drop.
        const int beforeAssets = symbolView.vectorItems().size();
        const QString svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\" "
            "viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"40\" "
            "fill=\"#ffb547\"/></svg>");
        check(symbolView.insertSvgAsset(svg, QPointF(250, 250)) == 1,
              QStringLiteral("library asset inserted on canvas"));
        check(symbolView.vectorItems().size() == beforeAssets + 1,
              QStringLiteral("inserted asset adds an object"));

        QMimeData dragMime;
        dragMime.setData(QStringLiteral("application/x-vectora-asset"), svg.toUtf8());
        const QPoint dropPos = symbolView.mapFromScene(QPointF(350, 350));
        QDragEnterEvent dragEnter(dropPos, Qt::CopyAction, &dragMime,
                                  Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(symbolView.viewport(), &dragEnter);
        QDropEvent dropEvent(QPointF(dropPos), Qt::CopyAction, &dragMime,
                             Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(symbolView.viewport(), &dropEvent);
        check(symbolView.vectorItems().size() == beforeAssets + 2,
              QStringLiteral("library asset dropped with the mouse"));
        check(dropEvent.isAccepted(),
              QStringLiteral("canvas accepts the library drop"));

        symbolView.scene()->clearSelection();
        symbolView.vectorItems().last()->setSelected(true);
        symbolView.vectorItems().last()->setSelected(true);
        const QString selectionSvg = symbolView.selectionToSvg();
        check(selectionSvg.contains(QStringLiteral("<svg"))
                  && !selectionSvg.contains(QStringLiteral("fill=\"#ffffff\"")),
              QStringLiteral("selection exported to SVG without background"));
    }

    // Asset store: libraries, categories, favourites, copy and persistence.
    {
        AssetStore &store = AssetStore::instance();
        store.load();
        check(store.count() >= 1, QStringLiteral("default asset library present"));
        check(store.library(0).assets.size() >= 8,
              QStringLiteral("library ships with real assets"));
        bool hasCategories = true;
        for (const char *category : {"Иконки", "Логотипы", "Фигуры",
                                     "Декоративные", "Геометрические",
                                     "Текстовые", "Символы"}) {
            if (!store.categories(0).contains(QString::fromUtf8(category)))
                hasCategories = false;
        }
        check(hasCategories, QStringLiteral("required asset categories available"));

        const int extra = store.addLibrary(QStringLiteral("Тестовая"));
        check(extra >= 0 && store.count() >= 2,
              QStringLiteral("second library created"));

        Asset asset;
        asset.name = QStringLiteral("Тестовый круг");
        asset.category = QStringLiteral("Иконки");
        asset.svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
            "<circle cx=\"20\" cy=\"20\" r=\"18\" fill=\"#06d6a0\"/></svg>");
        const int target = store.currentIndex();
        check(store.addAsset(target, asset), QStringLiteral("asset added to library"));
        const int index = store.findAsset(store.library(target).assets.last().id, target);
        check(index >= 0, QStringLiteral("added asset can be found by id"));
        check(store.library(target).assets.at(index).name == QStringLiteral("Тестовый круг"),
              QStringLiteral("asset name stored"));

        store.renameAsset(target, index, QStringLiteral("Переименованный"));
        check(store.library(target).assets.at(index).name
                  == QStringLiteral("Переименованный"),
              QStringLiteral("asset renamed"));

        store.setAssetCategory(target, index, QStringLiteral("Пользовательские"));
        check(store.library(target).assets.at(index).category
                  == QStringLiteral("Пользовательские"),
              QStringLiteral("asset category changed"));

        store.toggleFavorite(target, index);
        check(store.library(target).assets.at(index).favorite,
              QStringLiteral("asset marked as favourite"));
        store.toggleFavorite(target, index);

        check(store.copyAsset(target, index, 0),
              QStringLiteral("asset copied between libraries"));
        bool copied = false;
        for (const Asset &candidate : store.library(0).assets) {
            if (candidate.name.contains(QStringLiteral("(копия)")))
                copied = true;
        }
        check(copied, QStringLiteral("copied asset present in the first library"));

        const QString libraryPath = dir.filePath(QStringLiteral("test.vlib"));
        check(store.exportLibrary(0, libraryPath, &error),
              QStringLiteral("library exported to .vlib"));
        const int beforeImport = store.count();
        check(store.importLibrary(libraryPath, &error) >= 0,
              QStringLiteral("library imported from .vlib"));
        check(store.count() == beforeImport + 1,
              QStringLiteral("imported library added"));
        check(!AssetStore::renderPreview(asset.svg, QSize(32, 32)).isNull(),
              QStringLiteral("asset preview renders"));

        AssetPanel assetPanel(nullptr);
        QListWidget *draggableAssets = nullptr;
        for (QListWidget *list : assetPanel.findChildren<QListWidget *>()) {
            if (list->dragDropMode() == QAbstractItemView::DragOnly) {
                draggableAssets = list;
                break;
            }
        }
        check(draggableAssets != nullptr && draggableAssets->dragEnabled(),
              QStringLiteral("asset list is configured for dragging"));
        if (draggableAssets && draggableAssets->count() > 0) {
            check(draggableAssets->item(0)->flags() & Qt::ItemIsDragEnabled,
                  QStringLiteral("asset items can start a drag"));
        }

        QFile stored(store.storageFile());
        check(stored.open(QIODevice::ReadOnly)
                  && stored.readAll().contains("vectora-asset-libraries"),
              QStringLiteral("libraries persisted between launches"));
        stored.close();

        check(store.canUndo(), QStringLiteral("library edits are undoable"));
        check(store.undo(), QStringLiteral("library change undone"));
        check(store.canRedo(), QStringLiteral("library change can be redone"));
        const int afterUndo = store.count();
        check(store.redo(), QStringLiteral("library change redone"));
        check(store.count() == afterUndo + 1,
              QStringLiteral("redo restores the imported library"));
    }

    // --- Drafting: dimensions, grids, constructions, drawing scale ----------
    out << "\nDrafting\n";
    {
        CanvasView draftView;
        draftView.resize(900, 1200);
        draftView.setPageSize(QSizeF(500, 500));
        draftView.setSnapEnabled(false);

        VectorObject rect;
        rect.type = ShapeType::Rectangle;
        rect.path.addRect(QRectF(0, 0, 100, 60));
        rect.pos = QPointF(80, 80);
        rect.fill = QColor("#4f7cc0");
        VectorItem *rectItem = draftView.addVectorObject(rect, false, false);
        check(rectItem != nullptr, QStringLiteral("drafting source object added"));

        draftView.setActiveTool(Tool::Dimension);
        draftView.setDimensionMode(CanvasView::DimensionMode::Horizontal);
        const QPoint first = draftView.mapFromScene(QPointF(80, 80));
        const QPoint second = draftView.mapFromScene(QPointF(180, 80));
        sendMouse(draftView.viewport(), QEvent::MouseButtonPress, first,
                  Qt::LeftButton, Qt::LeftButton);
        sendMouse(draftView.viewport(), QEvent::MouseButtonRelease, first,
                  Qt::LeftButton, Qt::NoButton);
        sendMouse(draftView.viewport(), QEvent::MouseButtonPress, second,
                  Qt::LeftButton, Qt::LeftButton);
        sendMouse(draftView.viewport(), QEvent::MouseButtonRelease, second,
                  Qt::LeftButton, Qt::NoButton);

        VectorItem *dimensionItem = nullptr;
        for (VectorItem *item : draftView.vectorItems()) {
            if (item->object().type == ShapeType::Dimension)
                dimensionItem = item;
        }
        check(dimensionItem != nullptr,
              QStringLiteral("horizontal dimension created with the tool"));
        if (dimensionItem) {
            const VectorObject dimension = dimensionItem->object();
            check(dimension.dimensionType == 0,
                  QStringLiteral("dimension type stored"));
            check(dimension.dimRefA == rectItem->object().id
                      && dimension.dimRefB == rectItem->object().id,
                  QStringLiteral("dimension attached to the geometry"));
            const QPointF beforeB = dimension.dimB;

            VectorObject resized = rectItem->object();
            resized.path = QTransform::fromScale(1.6, 1.2).map(resized.path);
            rectItem->setObject(resized);
            draftView.updateAllDimensions();
            const VectorObject updated = dimensionItem->object();
            check(QLineF(beforeB, updated.dimB).length() > 10.0,
                  QStringLiteral("dimension follows geometry changes"));

            draftView.scene()->clearSelection();
            dimensionItem->setSelected(true);
            draftView.setDimensionUnits(1);
            draftView.setDimensionPrecision(2);
            draftView.setDimensionOffset(36.0);
            draftView.applyDimensionSettingsToSelection();
            const VectorObject styled = dimensionItem->object();
            check(styled.dimUnits == 1 && styled.dimPrecision == 2
                      && qAbs(styled.dimOffset - 36.0) < 0.01,
                  QStringLiteral("dimension style applied to selection"));

            draftView.setDrawingScale(2.0);
            check(qAbs(dimensionItem->object().dimScale - 2.0) < 0.001,
                  QStringLiteral("drawing scale propagated to dimensions"));
            const QString measured = draftView.measurementText(37.795275591 * 10.0);
            check(measured.contains(QStringLiteral("см")),
                  QStringLiteral("measurement uses selected units"));
            check(draftView.measurementText(37.795275591 * 10.0).contains(QStringLiteral("20")),
                  QStringLiteral("drawing scale affects real dimensions"));
        }

        // Radius and diameter tools attach to the ellipse.
        VectorObject circle;
        circle.type = ShapeType::Ellipse;
        circle.path.addEllipse(QRectF(0, 0, 80, 80));
        circle.pos = QPointF(300, 80);
        circle.fill = QColor("#ffb547");
        VectorItem *circleItem = draftView.addVectorObject(circle, false, false);
        draftView.setActiveTool(Tool::Dimension);
        draftView.setDimensionMode(CanvasView::DimensionMode::Radius);
        const QPoint onCircle = draftView.mapFromScene(QPointF(380, 120));
        sendMouse(draftView.viewport(), QEvent::MouseButtonPress, onCircle,
                  Qt::LeftButton, Qt::LeftButton);
        sendMouse(draftView.viewport(), QEvent::MouseButtonRelease, onCircle,
                  Qt::LeftButton, Qt::NoButton);
        int radialCount = 0;
        for (VectorItem *item : draftView.vectorItems()) {
            if (item->object().type == ShapeType::Dimension
                && item->object().dimensionType >= 4)
                ++radialCount;
        }
        check(radialCount == 1, QStringLiteral("radius dimension created"));
        if (circleItem) {
            const QRectF bounds = circleItem->object().localBounds();
            check(qAbs(bounds.width() - 80.0) < 0.01,
                  QStringLiteral("source circle stays unchanged"));
        }

        // Angular dimension needs three clicks.
        draftView.setDimensionMode(CanvasView::DimensionMode::Angular);
        const QVector<QPointF> angularPoints = {QPointF(320, 320), QPointF(420, 320),
                                                QPointF(320, 220)};
        for (const QPointF &point : angularPoints) {
            const QPoint viewPoint = draftView.mapFromScene(point);
            sendMouse(draftView.viewport(), QEvent::MouseButtonPress, viewPoint,
                      Qt::LeftButton, Qt::LeftButton);
            sendMouse(draftView.viewport(), QEvent::MouseButtonRelease, viewPoint,
                      Qt::LeftButton, Qt::NoButton);
        }
        bool angular = false;
        for (VectorItem *item : draftView.vectorItems()) {
            if (item->object().type == ShapeType::Dimension
                && item->object().dimensionType == 3)
                angular = true;
        }
        check(angular, QStringLiteral("angular dimension created from three points"));

        // Rendered dimension produces visible pixels.
        Document dimensionDocument;
        dimensionDocument.pageSize = QSizeF(500, 500);
        for (VectorItem *item : draftView.vectorItems()) {
            if (item->object().type == ShapeType::Dimension)
                dimensionDocument.objects.append(item->object());
        }
        QImage dimensionImage(500, 500, QImage::Format_ARGB32_Premultiplied);
        dimensionImage.fill(Qt::white);
        {
            QPainter painter(&dimensionImage);
            dimensionDocument.render(&painter);
        }
        int drawnPixels = 0;
        for (int y = 0; y < dimensionImage.height(); y += 2) {
            for (int x = 0; x < dimensionImage.width(); x += 2) {
                if (dimensionImage.pixelColor(x, y) != QColor(Qt::white))
                    ++drawnPixels;
            }
        }
        check(drawnPixels > 20, QStringLiteral("dimensions are rendered"));
    }

    // Cartesian, polar and isometric grids snap to their own lattice.
    {
        CanvasView gridView;
        gridView.resize(700, 800);
        gridView.setPageSize(QSizeF(400, 400));
        gridView.setGridStep(40);
        gridView.setSnapEnabled(true);
        const QPointF centre(200, 200);

        gridView.setGridType(1);
        gridView.setGridAngle(45.0);
        const QPointF polarRaw = centre + QPointF(80.0 * std::cos(0.785398),
                                                  80.0 * std::sin(0.785398))
                                + QPointF(2.5, -2.0);
        const QPointF polar = gridView.snapNodePoint(polarRaw, nullptr);
        const qreal radius = std::hypot(polar.x() - centre.x(), polar.y() - centre.y());
        check(qAbs(radius - qRound(radius / 40.0) * 40.0) < 0.2,
              QStringLiteral("polar grid snaps radius to the step"));
        qreal degrees = std::atan2(polar.y() - centre.y(), polar.x() - centre.x())
                        * 180.0 / 3.14159265358979323846;
        const qreal angleDelta = std::fmod(qAbs(degrees), 45.0);
        check(qMin(angleDelta, 45.0 - angleDelta) < 0.2,
              QStringLiteral("polar grid snaps the angle"));

        gridView.setGridType(2);
        const QPointF iso = gridView.snapNodePoint(centre + QPointF(47, 19), nullptr);
        check(QLineF(gridView.snapNodePoint(iso, nullptr), iso).length() < 0.3,
              QStringLiteral("isometric grid snap is stable"));

        gridView.setGridType(0);
        const QPointF cartesian = gridView.snapNodePoint(QPointF(38, 78), nullptr);
        check(qAbs(cartesian.x() - 40.0) < 0.1
                  && qAbs(cartesian.y() - 80.0) < 0.1,
              QStringLiteral("cartesian grid snaps to intersections"));

        gridView.setSnapOption(QStringLiteral("grid"), false);
        const QPointF unsnapped = gridView.snapNodePoint(QPointF(23, 77), nullptr);
        check(qAbs(unsnapped.x() - 23.0) < 0.001,
              QStringLiteral("grid snapping can be disabled"));
        gridView.setSnapOption(QStringLiteral("grid"), true);
    }

    // Exact construction commands.
    {
        CanvasView buildView;
        buildView.resize(700, 800);
        buildView.setPageSize(QSizeF(400, 400));
        buildView.setSnapEnabled(false);

        const int before = buildView.vectorItems().size();
        buildView.constructLine(QPointF(20, 20), QPointF(120, 40));
        check(buildView.vectorItems().size() == before + 1,
              QStringLiteral("line by coordinates constructed"));

        buildView.constructLineFrom(QPointF(200, 20), 100.0, 90.0);
        check(buildView.vectorItems().size() == before + 2,
              QStringLiteral("line by length and angle constructed"));
        const VectorItem *vertical = buildView.vectorItems().last();
        const QRectF verticalBounds = vertical->object().path.boundingRect();
        check(qAbs(verticalBounds.height() - 100.0) < 1.0,
              QStringLiteral("line length matches the request"));

        buildView.constructCircle(QPointF(300, 120), 50.0);
        const VectorItem *circle = buildView.vectorItems().last();
        check(qAbs(circle->object().path.boundingRect().width() - 100.0) < 1.0,
              QStringLiteral("circle by radius constructed"));

        buildView.constructArc(QPointF(120, 300), 60.0, 0.0, 90.0);
        check(!buildView.vectorItems().last()->object().path.isEmpty(),
              QStringLiteral("arc by radius constructed"));

        buildView.constructPolygon(QPointF(300, 300), 6, 70.0);
        const QVector<QPolygonF> polygonParts =
            buildView.vectorItems().last()->object().path.toSubpathPolygons();
        check(!polygonParts.isEmpty() && polygonParts.first().size() >= 6,
              QStringLiteral("regular polygon constructed"));

        // Parallel and perpendicular to a selected line.
        buildView.scene()->clearSelection();
        buildView.vectorItems().first()->setSelected(true);
        const int beforeParallel = buildView.vectorItems().size();
        check(buildView.createParallelFromSelection(30.0),
              QStringLiteral("parallel line command succeeds"));
        check(buildView.vectorItems().size() == beforeParallel + 1,
              QStringLiteral("parallel line added"));
        buildView.scene()->clearSelection();
        buildView.vectorItems().first()->setSelected(true);
        check(buildView.createPerpendicularFromSelection(80.0),
              QStringLiteral("perpendicular command succeeds"));

        // Tangents from an external point.
        VectorObject tangentCircle;
        tangentCircle.type = ShapeType::Ellipse;
        tangentCircle.path.addEllipse(QRectF(0, 0, 80, 80));
        tangentCircle.pos = QPointF(260, 80);
        tangentCircle.fill = QColor("#ffb547");
        VectorItem *tangentSource = buildView.addVectorObject(tangentCircle, false, false);
        buildView.scene()->clearSelection();
        tangentSource->setSelected(true);
        const int beforeTangents = buildView.vectorItems().size();
        check(buildView.createTangentFromSelection(QPointF(120, 120)),
              QStringLiteral("tangent command succeeds"));
        check(buildView.vectorItems().size() >= beforeTangents + 1,
              QStringLiteral("tangent lines added"));

        // Uniform scaling around the selection centre.
        VectorObject block;
        block.type = ShapeType::Rectangle;
        block.path.addRect(QRectF(0, 0, 40, 20));
        block.pos = QPointF(20, 20);
        VectorItem *blockItem = buildView.addVectorObject(block, false, false);
        buildView.scene()->clearSelection();
        blockItem->setSelected(true);
        const qreal widthBefore = blockItem->object().localBounds().width();
        buildView.scaleSelectionBy(2.0);
        check(qAbs(blockItem->object().localBounds().width() - widthBefore * 2.0) < 1.0,
              QStringLiteral("selection scales by factor"));

        // Drawing scale survives document serialisation.
        Document scaled;
        scaled.drawingScale = 2.5;
        const QString scaledPath = dir.filePath(QStringLiteral("scale.vct"));
        check(scaled.save(scaledPath, &error),
              QStringLiteral("document with drawing scale saved"));
        Document scaledLoaded;
        check(scaledLoaded.load(scaledPath, &error),
              QStringLiteral("document with drawing scale loaded"));
        check(qAbs(scaledLoaded.drawingScale - 2.5) < 0.001,
              QStringLiteral("drawing scale persists"));

        // Dimension objects round-trip through .vct.
        Document dimensionDoc;
        dimensionDoc.pageSize = QSizeF(500, 500);
        VectorObject savedDimension;
        savedDimension.type = ShapeType::Dimension;
        savedDimension.dimensionType = 3;
        savedDimension.dimA = QPointF(40, 40);
        savedDimension.dimB = QPointF(120, 40);
        savedDimension.dimC = QPointF(40, 110);
        savedDimension.dimUnits = 2;
        savedDimension.dimPrecision = 3;
        savedDimension.dimOffset = 28.0;
        savedDimension.dimRefA = 77;
        savedDimension.dimAnchorB = QPointF(0.5, 1.0);
        dimensionDoc.objects.append(savedDimension);
        const QString dimensionPath = dir.filePath(QStringLiteral("dimensions.vct"));
        check(dimensionDoc.save(dimensionPath, &error),
              QStringLiteral("document with dimensions saved"));
        Document loadedDimensions;
        check(loadedDimensions.load(dimensionPath, &error),
              QStringLiteral("document with dimensions loaded"));
        check(loadedDimensions.objects.size() == 1
                  && loadedDimensions.objects.at(0).type == ShapeType::Dimension
                  && loadedDimensions.objects.at(0).dimensionType == 3
                  && loadedDimensions.objects.at(0).dimUnits == 2
                  && loadedDimensions.objects.at(0).dimRefA == 77
                  && qAbs(loadedDimensions.objects.at(0).dimAnchorB.y() - 1.0) < 0.001,
              QStringLiteral("dimension geometry persists"));
    }

    // --- Patterns, repeats and the pattern library ---------------------------
    out << "\nPatterns\n";
    {
        CanvasView patternView;
        patternView.resize(900, 1200);
        patternView.setPageSize(QSizeF(500, 500));
        patternView.setSnapEnabled(false);

        VectorObject dot;
        dot.type = ShapeType::Ellipse;
        dot.path.addEllipse(QRectF(0, 0, 20, 20));
        dot.pos = QPointF(20, 20);
        dot.fill = QColor("#e0466b");
        dot.hasStroke = false;
        VectorItem *dotItem = patternView.addVectorObject(dot, false, false);

        VectorObject stripe;
        stripe.type = ShapeType::Rectangle;
        stripe.path.addRect(QRectF(0, 0, 24, 8));
        stripe.pos = QPointF(48, 44);
        stripe.fill = QColor("#4f7cc0");
        stripe.hasStroke = false;
        VectorItem *stripeItem = patternView.addVectorObject(stripe, false, false);

        patternView.scene()->clearSelection();
        dotItem->setSelected(true);
        stripeItem->setSelected(true);
        const int patternId = patternView.createPatternFromSelection(QStringLiteral("Тест"));
        check(patternId > 0, QStringLiteral("pattern created from selection"));
        check(patternView.patterns().size() == 1,
              QStringLiteral("pattern stored in the document"));
        PatternDefinition definition = patternView.patternDefinition(patternId);
        check(definition.objects.size() == 2,
              QStringLiteral("pattern keeps its source objects"));
        check(definition.settings.tile.width() > 1.0,
              QStringLiteral("pattern tile measured"));

        const QVector<PatternShape> shapes = bakePatternShapes(definition);
        check(!shapes.isEmpty(), QStringLiteral("pattern tile artwork baked"));
        check(!renderPatternPreview(definition.settings, shapes, QSize(64, 64)).isNull(),
              QStringLiteral("pattern preview renders"));

        VectorObject target;
        target.type = ShapeType::Rectangle;
        target.path.addRect(QRectF(0, 0, 220, 160));
        target.pos = QPointF(120, 120);
        target.fill = QColor("#ffffff");
        VectorItem *targetItem = patternView.addVectorObject(target, false, false);
        patternView.scene()->clearSelection();
        targetItem->setSelected(true);
        check(patternView.applyPatternToSelection(patternId),
              QStringLiteral("pattern applied as an object fill"));
        check(targetItem->object().patternId == patternId
                  && !targetItem->object().patternShapes.isEmpty(),
              QStringLiteral("applied pattern stored on the object"));

        Document patternDocument;
        patternDocument.pageSize = QSizeF(500, 500);
        patternDocument.patterns = patternView.patterns();
        patternDocument.objects.append(targetItem->object());
        QImage patternImage(500, 500, QImage::Format_ARGB32_Premultiplied);
        patternImage.fill(Qt::white);
        {
            QPainter painter(&patternImage);
            patternDocument.render(&painter);
        }
        bool patternVisible = false;
        for (int y = 120; y < 280 && !patternVisible; ++y) {
            for (int x = 120; x < 340; ++x) {
                const QColor pixel = patternImage.pixelColor(x, y);
                if (pixel != QColor(Qt::white)) {
                    patternVisible = true;
                    break;
                }
            }
        }
        check(patternVisible, QStringLiteral("pattern fill is rendered"));

        // Editing the definition updates every linked fill.
        PatternDefinition edited = patternView.patternDefinition(patternId);
        edited.settings.tile = QSizeF(36, 36);
        edited.settings.type = 1;
        patternView.updatePatternDefinition(edited);
        check(qAbs(targetItem->object().pattern.tile.width() - 36.0) < 0.01
                  && targetItem->object().pattern.type == 1,
              QStringLiteral("editing a pattern updates the applied fill"));

        // Replacing the source artwork refreshes linked instances.
        dotItem->setFillColor(QColor("#ffd166"));
        patternView.scene()->clearSelection();
        dotItem->setSelected(true);
        stripeItem->setSelected(true);
        check(patternView.updatePatternFromSelection(patternId),
              QStringLiteral("pattern source refreshed from selection"));
        check(patternView.patternDefinition(patternId).objects.first().fill
                  == QColor("#ffd166"),
              QStringLiteral("pattern definition keeps new source artwork"));

        // Repeat types produce the expected number of placements.
        PatternSettings settings = definition.settings;
        settings.tile = QSizeF(40, 40);
        settings.type = 0;
        const QRectF area(0, 0, 200, 160);
        check(patternTileTransforms(settings, area).size() > 4,
              QStringLiteral("grid repeat produces tiles"));
        settings.type = 1;
        check(patternTileTransforms(settings, area).size() > 4,
              QStringLiteral("brick repeat produces tiles"));
        settings.type = 2;
        check(patternTileTransforms(settings, area).size() > 4,
              QStringLiteral("hex repeat produces tiles"));
        settings.type = 3;
        check(patternTileTransforms(settings, area).size() >= 12,
              QStringLiteral("mirror repeat produces mirrored variants"));
        settings.type = 4;
        settings.count = 6;
        check(patternTileTransforms(settings, area).size() == 6,
              QStringLiteral("radial repeat uses the requested count"));
        settings.type = 5;
        check(patternTileTransforms(settings, area).size() == 6,
              QStringLiteral("rotational repeat uses the requested count"));
        settings.type = 6;
        settings.count = 5;
        settings.pathStep = 40;
        check(!patternTileTransforms(settings, area).isEmpty(),
              QStringLiteral("path repeat follows a guide curve"));

        // Seamless check: artwork with an empty right edge is not seamless.
        VectorObject edge;
        edge.type = ShapeType::Rectangle;
        edge.path.addRect(QRectF(0, 0, 16, 40));
        edge.pos = QPointF(0, 0);
        edge.fill = QColor("#06d6a0");
        edge.hasStroke = false;
        VectorObject marker;
        marker.type = ShapeType::Rectangle;
        marker.path.addRect(QRectF(0, 0, 2, 2));
        marker.pos = QPointF(46, 0);
        marker.fill = QColor("#06d6a0");
        marker.hasStroke = false;
        PatternDefinition uneven = patternFromObjects({edge, marker},
                                                      QStringLiteral("Шов"));
        const QVector<PatternShape> unevenShapes = bakePatternShapes(uneven);
        qreal seamDifference = 0.0;
        const bool seamless = patternIsSeamless(uneven.settings, unevenShapes,
                                                &seamDifference);
        check(!seamless && seamDifference > 0.0,
              QStringLiteral("seamless check reports edge differences"));

        VectorObject flush;
        flush.type = ShapeType::Rectangle;
        flush.path.addRect(QRectF(0, 0, 40, 40));
        flush.fill = QColor("#4f7cc0");
        flush.hasStroke = false;
        PatternDefinition closedTile = patternFromObjects({flush},
                                                         QStringLiteral("Ровный"));
        check(patternIsSeamless(closedTile.settings,
                                bakePatternShapes(closedTile)),
              QStringLiteral("full tile considered seamless"));

        // Serialisation of the pattern fill and the definition.
        Document storedPatterns;
        storedPatterns.pageSize = QSizeF(500, 500);
        storedPatterns.patterns = patternView.patterns();
        storedPatterns.objects.append(targetItem->object());
        const QString patternPath = dir.filePath(QStringLiteral("patterns.vct"));
        check(storedPatterns.save(patternPath, &error),
              QStringLiteral("document with patterns saved"));
        Document loadedPatterns;
        check(loadedPatterns.load(patternPath, &error),
              QStringLiteral("document with patterns loaded"));
        check(loadedPatterns.patterns.size() == 1,
              QStringLiteral("pattern definition persists"));
        check(loadedPatterns.objects.size() == 1
                  && loadedPatterns.objects.at(0).patternShapes.size()
                         == targetItem->object().patternShapes.size(),
              QStringLiteral("pattern fill persists on the object"));
        check(loadedPatterns.objects.at(0).patternId == patternId
                  && loadedPatterns.objects.at(0).pattern.type
                         == targetItem->object().pattern.type,
              QStringLiteral("pattern settings persist"));

        {
            PatternDialog editor(&patternView,
                                 patternView.patternDefinition(patternId));
            check(editor.windowTitle().contains(QStringLiteral("Узор")),
                  QStringLiteral("pattern editor opens for a definition"));
        }

        // Expand to independent vector objects.
        patternView.scene()->clearSelection();
        VectorItem *fillItem = nullptr;
        for (VectorItem *item : patternView.vectorItems()) {
            if (item->object().patternId == patternId)
                fillItem = item;
        }
        check(fillItem != nullptr, QStringLiteral("pattern filled object found"));
        if (fillItem) {
            patternView.scene()->clearSelection();
            fillItem->setSelected(true);
            const int expanded = patternView.expandSelectedPatterns();
            check(expanded > 0, QStringLiteral("pattern expanded to objects"));
            bool removed = true;
            for (VectorItem *item : patternView.vectorItems()) {
                if (item->object().patternId == patternId)
                    removed = false;
            }
            check(removed, QStringLiteral("pattern fill replaced by objects"));
        }
    }

    // Pattern library: persistence, export and import.
    {
        PatternStore &patterns = PatternStore::instance();
        patterns.load();
        const int before = patterns.count();

        PatternDefinition sample;
        sample.name = QStringLiteral("Библиотечный");
        VectorObject square;
        square.type = ShapeType::Rectangle;
        square.path.addRect(QRectF(0, 0, 32, 32));
        square.fill = QColor("#ffb547");
        sample.objects.append(square);
        sample.bounds = QRectF(0, 0, 32, 32);
        sample.settings.tile = QSizeF(32, 32);
        sample.settings.type = 1;

        check(patterns.add(sample) >= 0,
              QStringLiteral("pattern added to the library"));
        check(patterns.count() == before + 1,
              QStringLiteral("pattern library grows"));
        const QString exportPath = dir.filePath(QStringLiteral("pattern.vpat"));
        check(patterns.exportPattern(before, exportPath, &error),
              QStringLiteral("pattern exported to .vpat"));
        check(patterns.importPattern(exportPath, &error),
              QStringLiteral("pattern imported from .vpat"));
        check(patterns.count() == before + 2,
              QStringLiteral("imported pattern added"));
        QFile storedPattern(patterns.storageFile());
        check(storedPattern.open(QIODevice::ReadOnly)
                  && storedPattern.readAll().contains("vectora-patterns"),
              QStringLiteral("pattern library persisted"));
    }

    // --- QR codes and barcodes -----------------------------------------------
    out << "\nCodes\n";
    {
        const CodeSymbol qr = generateCode(CodeKind::Qr,
                                          QStringLiteral("hello world"), 1);
        check(qr.valid, QStringLiteral("QR code generated"));
        check(qr.modulesX == 21 && qr.cells.size() == 21 * 21,
              QStringLiteral("QR version 1 matrix size"));

        const CodeSymbol url = generateCode(
            CodeKind::Qr, QStringLiteral("https://example.com/path"), 2);
        check(url.valid && url.modulesX >= 25,
              QStringLiteral("QR URL uses a suitable version"));

        bool tooLong = false;
        const CodeSymbol huge = generateCode(CodeKind::Qr,
                                             QString(400, QLatin1Char('x')), 3);
        tooLong = !huge.valid && !huge.error.isEmpty();
        check(tooLong, QStringLiteral("QR rejects oversized payloads"));

        const CodeSymbol dm = generateCode(CodeKind::DataMatrix,
                                           QStringLiteral("HELLO"));
        check(dm.valid && dm.modulesX == 12,
              QStringLiteral("Data Matrix generated"));
        bool dmBorders = true;
        const int dmSize = dm.modulesX;
        for (int i = 0; i < dmSize; ++i) {
            if (!dm.cells.at((dmSize - 1) * dmSize + i)) // bottom solid
                dmBorders = false;
            if (!dm.cells.at(i * dmSize)) // left solid
                dmBorders = false;
            if (dm.cells.at(i) != (i % 2 == 0)) // top alternating
                dmBorders = false;
            if (dm.cells.at(i * dmSize + dmSize - 1)
                != (i % 2 == 1)) // right alternating
                dmBorders = false;
        }
        check(dmBorders, QStringLiteral("Data Matrix finder borders"));
        check(!generateCode(CodeKind::DataMatrix,
                            QString(60, QLatin1Char('x'))).valid,
              QStringLiteral("Data Matrix rejects oversized payloads"));

        const CodeSymbol ean = generateCode(CodeKind::Ean13,
                                            QStringLiteral("590123412345"));
        check(ean.valid && ean.data == QStringLiteral("5901234123457"),
              QStringLiteral("EAN-13 check digit is calculated"));
        check(ean.modulesX == 95, QStringLiteral("EAN-13 module count"));
        check(!generateCode(CodeKind::Ean13,
                            QStringLiteral("5901234123450")).valid,
              QStringLiteral("EAN-13 rejects a wrong check digit"));

        const CodeSymbol ean8 = generateCode(CodeKind::Ean8,
                                             QStringLiteral("96385074"));
        check(ean8.valid && ean8.modulesX == 67,
              QStringLiteral("EAN-8 generated"));

        const CodeSymbol upc = generateCode(CodeKind::UpcA,
                                            QStringLiteral("03600029145"));
        check(upc.valid && upc.data == QStringLiteral("036000291452"),
              QStringLiteral("UPC-A check digit is calculated"));

        const CodeSymbol code128 = generateCode(CodeKind::Code128,
                                                QStringLiteral("A1"));
        check(code128.valid && code128.modulesX == 57,
              QStringLiteral("Code 128 generated"));

        const CodeSymbol code39 = generateCode(CodeKind::Code39,
                                               QStringLiteral("ABC"));
        check(code39.valid && code39.modulesX > 30,
              QStringLiteral("Code 39 generated"));

        // External decoders validate the standards when they are installed.
        const auto zbarDecode = [&dir](const CodeSymbol &symbol,
                                       const QString &name) -> QString {
            const QString executable = QStandardPaths::findExecutable(
                QStringLiteral("zbarimg"));
            if (executable.isEmpty() || !symbol.valid)
                return QString();
            const bool linear = symbol.modulesY <= 1;
            const int margin = linear ? 8 : 4;
            const int scale = linear ? 4 : 6;
            const int width = (symbol.modulesX + margin * 2) * scale;
            const int height = linear
                                   ? 60 * scale
                                   : (symbol.modulesY + margin * 2) * scale;
            QImage image(width, height, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);
            if (linear) {
                const qreal unit = qreal(width) / (symbol.modulesX + margin * 2);
                for (int i = 0; i < symbol.cells.size(); ++i) {
                    if (symbol.cells.at(i))
                        painter.drawRect(QRectF((margin + i) * unit, 10,
                                                unit + 0.2, height - 20));
                }
            } else {
                for (int y = 0; y < symbol.modulesY; ++y) {
                    for (int x = 0; x < symbol.modulesX; ++x) {
                        if (symbol.cells.at(y * symbol.modulesX + x))
                            painter.drawRect(QRectF((margin + x) * scale,
                                                    (margin + y) * scale,
                                                    scale, scale));
                    }
                }
            }
            painter.end();
            const QString path = dir.filePath(name + QStringLiteral(".png"));
            image.save(path);
            QProcess process;
            process.start(executable, {QStringLiteral("-q"), QStringLiteral("--raw"),
                                       path});
            if (!process.waitForFinished(4000))
                return QString();
            return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        };

        if (!QStandardPaths::findExecutable(QStringLiteral("zbarimg")).isEmpty()) {
            check(zbarDecode(qr, QStringLiteral("qr")) == QStringLiteral("hello world"),
                  QStringLiteral("QR decodes with an external reader"));
            check(zbarDecode(ean, QStringLiteral("ean"))
                      == QStringLiteral("5901234123457"),
                  QStringLiteral("EAN-13 decodes with an external reader"));
            check(zbarDecode(code128, QStringLiteral("c128"))
                      == QStringLiteral("A1"),
                  QStringLiteral("Code 128 decodes with an external reader"));
            check(zbarDecode(code39, QStringLiteral("c39"))
                      == QStringLiteral("ABC"),
                  QStringLiteral("Code 39 decodes with an external reader"));
            check(zbarDecode(ean8, QStringLiteral("ean8"))
                      == QStringLiteral("96385074"),
                  QStringLiteral("EAN-8 decodes with an external reader"));
        }

        // Document integration: rendering, vector SVG and serialisation.
        CanvasView codeView;
        codeView.resize(900, 900);
        codeView.setPageSize(QSizeF(400, 400));
        VectorObject codeObject;
        codeObject.type = ShapeType::Code;
        codeObject.codeKind = static_cast<int>(CodeKind::Qr);
        codeObject.codeData = QStringLiteral("hello world");
        codeObject.codeEcc = 1;
        codeObject.codeMargin = 4;
        codeObject.codeColor = QColor("#102030");
        codeObject.codeBackground = QColor("#ffffff");
        codeObject.codeModulesX = qr.modulesX;
        codeObject.codeModulesY = qr.modulesY;
        codeObject.codeCells = qr.cells;
        codeObject.textBox = QRectF(0, 0, 180, 180);
        codeObject.pos = QPointF(100, 100);
        VectorItem *codeItem = codeView.addCodeObject(codeObject);
        check(codeItem != nullptr, QStringLiteral("code object added to canvas"));
        check(codeView.vectorItems().size() == 1,
              QStringLiteral("code object is an editable document item"));

        Document codeDocument;
        codeDocument.pageSize = QSizeF(400, 400);
        codeDocument.objects.append(codeItem->object());
        QImage codeImage(400, 400, QImage::Format_ARGB32_Premultiplied);
        codeImage.fill(Qt::white);
        {
            QPainter painter(&codeImage);
            codeDocument.render(&painter);
        }
        int darkPixels = 0;
        for (int y = 100; y < 280; ++y) {
            for (int x = 100; x < 280; ++x) {
                const QColor pixel = codeImage.pixelColor(x, y);
                if (pixel.lightness() < 128)
                    ++darkPixels;
            }
        }
        check(darkPixels > 100, QStringLiteral("QR modules are rendered"));

        const QString svg = codeDocument.toSvg(false);
        check(svg.contains(QStringLiteral("fill=\"#102030\""))
                  && !svg.contains(QStringLiteral("data:image/png")),
              QStringLiteral("QR exported to SVG as vector rectangles"));

        const QString codePath = dir.filePath(QStringLiteral("codes.vct"));
        check(codeDocument.save(codePath, &error),
              QStringLiteral("document with code saved"));
        Document loadedCode;
        check(loadedCode.load(codePath, &error),
              QStringLiteral("document with code loaded"));
        check(loadedCode.objects.size() == 1
                  && loadedCode.objects.at(0).type == ShapeType::Code
                  && loadedCode.objects.at(0).codeModulesX == qr.modulesX
                  && loadedCode.objects.at(0).codeCells.size() == qr.cells.size(),
              QStringLiteral("code geometry persists"));

        VectorObject updated = codeItem->object();
        const CodeSymbol eanSymbol = generateCode(CodeKind::Ean13,
                                                  QStringLiteral("590123412345"));
        updated.codeKind = static_cast<int>(CodeKind::Ean13);
        updated.codeData = eanSymbol.data;
        updated.codeModulesX = eanSymbol.modulesX;
        updated.codeModulesY = eanSymbol.modulesY;
        updated.codeCells = eanSymbol.cells;
        updated.codeHumanText = eanSymbol.humanText;
        codeView.updateCodeItem(codeItem, updated);
        check(codeItem->object().codeKind == static_cast<int>(CodeKind::Ean13)
                  && codeItem->object().codeModulesX == 95,
              QStringLiteral("code regenerated after editing"));

        CodeDialog dialog(&codeView, codeItem);
        check(dialog.windowTitle().contains(QStringLiteral("код")),
              QStringLiteral("code editor opens for an existing symbol"));
    }

    // --- Document management: autosave, versions, templates, packaging ------
    out << "\nDocument management\n";
    {
        check(UpdateChecker::compareVersions(QStringLiteral("0.2.0"),
                                             QStringLiteral("v0.10.0")) < 0,
              QStringLiteral("version comparison handles numeric parts"));
        check(UpdateChecker::compareVersions(QStringLiteral("1.0"),
                                             QStringLiteral("1.0.0")) == 0,
              QStringLiteral("version comparison treats missing parts as zero"));
        QVector<UpdateAsset> assets;
        assets.append({QStringLiteral("Vectora-1.0-x86_64.AppImage"),
                       QUrl(QStringLiteral("https://example.com/appimage")), 1});
        assets.append({QStringLiteral("vectora.deb"),
                       QUrl(QStringLiteral("https://example.com/deb")), 1});
        check(!UpdateChecker::chooseAsset(assets).isEmpty(),
              QStringLiteral("update asset selected for the platform"));

        // Autosave snapshots and recovery.
        const QString autosaveDir = dir.filePath(QStringLiteral("autosave"));
        AutoSaveManager::setDirectoryForTests(autosaveDir);
        AutoSaveManager::discardAll();
        Document savedDocument = Document::sample();
        const QJsonObject snapshot = savedDocument.toJson();
        QString autoPath;
        check(AutoSaveManager::saveSnapshot(QStringLiteral("session"),
                                            QStringLiteral("/tmp/example.vct"),
                                            QStringLiteral("example.vct"), snapshot,
                                            2, &autoPath),
              QStringLiteral("autosave snapshot written"));
        AutoSaveManager::saveSnapshot(QStringLiteral("session"), QString(), QString(),
                                      snapshot, 2);
        AutoSaveManager::saveSnapshot(QStringLiteral("session"), QString(), QString(),
                                      snapshot, 2);
        const QVector<AutoSaveEntry> pending = AutoSaveManager::pending();
        check(pending.size() == 2, QStringLiteral("autosave rotation keeps the limit"));
        QString original;
        const QJsonObject recovered =
            AutoSaveManager::loadSnapshot(pending.first().snapshotPath, &original);
        check(recovered.value(QStringLiteral("format")).toString()
                  == QStringLiteral("vectora-document"),
              QStringLiteral("autosave snapshot is a document"));
        AutoSaveManager::discardAll();
        check(AutoSaveManager::pending().isEmpty(),
              QStringLiteral("autosave snapshots discarded"));

        // Named versions and comparison.
        DocumentVersion version;
        version.name = QStringLiteral("Черновик");
        version.description = QStringLiteral("Первая версия");
        version.created = QDateTime::currentDateTime();
        version.snapshot = snapshot;
        const QString versionDocument = dir.filePath(QStringLiteral("versions.vct"));
        check(saveVersions(versionDocument, {version}),
              QStringLiteral("document versions saved"));
        const QVector<DocumentVersion> versions = loadVersions(versionDocument);
        check(versions.size() == 1
                  && versions.first().name == QStringLiteral("Черновик"),
              QStringLiteral("document versions loaded"));
        const QString comparison = compareDocuments(snapshot, snapshot,
                                                    QStringLiteral("A"),
                                                    QStringLiteral("B"));
        check(comparison.contains(QStringLiteral("совпадают")),
              QStringLiteral("identical versions are detected"));
        check(!versionsSidecar(versionDocument).isEmpty(),
              QStringLiteral("version sidecar path available"));

        // Templates: built-ins, user templates, export/import.
        TemplateStore &templates = TemplateStore::instance();
        templates.load();
        check(templates.count() >= 9, QStringLiteral("built-in templates present"));
        const QStringList requiredCategories = TemplateStore::builtinCategories();
        bool categoriesOk = true;
        for (const QString &category : requiredCategories) {
            if (!templates.categories().contains(category))
                categoriesOk = false;
        }
        check(categoriesOk, QStringLiteral("template categories available"));
        DocumentTemplate userTemplate;
        userTemplate.name = QStringLiteral("Тестовый шаблон");
        userTemplate.category = QStringLiteral("Пользовательские");
        userTemplate.description = QStringLiteral("Проверка");
        userTemplate.document = snapshot;
        const int beforeTemplates = templates.count();
        const int templateIndex = templates.add(userTemplate);
        check(templateIndex >= 0 && templates.count() == beforeTemplates + 1,
              QStringLiteral("user template saved"));
        const QString templatePath = dir.filePath(QStringLiteral("template.vtpl"));
        check(templates.exportTemplate(templateIndex, templatePath, &error),
              QStringLiteral("template exported"));
        const int beforeImport = templates.count();
        check(templates.importTemplate(templatePath, &error),
              QStringLiteral("template imported"));
        check(templates.count() == beforeImport + 1,
              QStringLiteral("imported template added"));
        templates.toggleFavorite(templateIndex);
        check(templates.templates().at(templateIndex).favorite,
              QStringLiteral("template marked as favourite"));
        check(templates.remove(templateIndex),
              QStringLiteral("user template removed"));

        DocumentTemplate constructed = TemplateStore::instance().templates().first();
        Document builtinDocument = Document::fromJson(constructed.document, &error);
        check(error.isEmpty() && !builtinDocument.objects.isEmpty(),
              QStringLiteral("built-in template is a real document"));

        // Missing fonts and document packaging.
        Document fontDocument;
        VectorObject text;
        text.type = ShapeType::Text;
        text.text = QStringLiteral("Missing font");
        text.font = QFont(QStringLiteral("Definitely Missing Font Family"));
        text.font.setPixelSize(20);
        text.textBox = QRectF(0, 0, 200, 40);
        fontDocument.objects.append(text);
        check(missingFonts(fontDocument).contains(
                  QStringLiteral("Definitely Missing Font Family")),
              QStringLiteral("missing fonts reported"));

        Document packageSource = Document::sample();
        VectorObject marked = packageSource.objects.first();
        marked.name = QStringLiteral("Названный объект");
        packageSource.objects[0] = marked;
        const PackageReport package = packageDocument(
            packageSource, dir.path(), QStringLiteral("vectora-test"));
        check(package.ok, QStringLiteral("document packaged"));
        check(QFileInfo::exists(package.folder + QStringLiteral("/report.txt")),
              QStringLiteral("package report written"));
        check(QFileInfo::exists(package.folder
                                + QStringLiteral("/vectora-test.vct")),
              QStringLiteral("package contains the document"));
        check(package.text.contains(QStringLiteral("Проверка целостности: успешно")),
              QStringLiteral("package integrity verified"));

        // Document dialogs construct over real data.
        HistoryDialog historyDialog({QStringLiteral("Действие")},
                                    {QDateTime::currentDateTime()}, 1);
        check(historyDialog.windowTitle().contains(QStringLiteral("История")),
              QStringLiteral("history dialog opens"));
        QVector<DocumentVersion> dialogVersions;
        VersionDialog versionDialog(&dialogVersions, snapshot,
                                    QStringLiteral("Документ"));
        check(versionDialog.windowTitle().contains(QStringLiteral("Версии")),
              QStringLiteral("versions dialog opens"));
        TemplateDialog templateDialog(snapshot);
        check(templateDialog.windowTitle().contains(QStringLiteral("Шаблоны")),
              QStringLiteral("template manager opens"));
        UpdateInfo fakeUpdate;
        fakeUpdate.version = QStringLiteral("9.9.9");
        fakeUpdate.notes = QStringLiteral("Тестовая заметка");
        fakeUpdate.pageUrl = QUrl(QStringLiteral("https://example.com"));
        fakeUpdate.assets.append({QStringLiteral("Vectora-9.9.9-x86_64.AppImage"),
                                  QUrl(QStringLiteral("https://example.com/app")), 1});
        UpdateDialog updateDialog(fakeUpdate);
        check(updateDialog.windowTitle().contains(QStringLiteral("обновление")),
              QStringLiteral("update dialog opens"));
        CanvasView searchView;
        searchView.resize(600, 600);
        VectorObject searchable;
        searchable.type = ShapeType::Rectangle;
        searchable.path.addRect(QRectF(0, 0, 40, 40));
        searchable.name = QStringLiteral("Кнопка");
        searchView.addVectorObject(searchable, false, false);
        ResourceSearchDialog searchDialog(&searchView);
        check(searchDialog.windowTitle().contains(QStringLiteral("Поиск")),
              QStringLiteral("resource search dialog opens"));
    }

    // --- Zoom, compact tools and page fields --------------------------------
    out << "\nUI interactions\n";
    {
        CanvasView zoomView;
        zoomView.resize(900, 700);
        zoomView.setPageSize(QSizeF(500, 500));
        zoomView.show();
        QApplication::processEvents();
        const QPoint anchor(450, 350);
        const QPointF before = zoomView.mapToScene(anchor);
        zoomView.zoomAt(2.0, anchor);
        const QPointF after = zoomView.mapToScene(anchor);
        check(QLineF(before, after).length() < 1.5,
              QStringLiteral("Ctrl+wheel zoom keeps the point under the cursor"));
        zoomView.zoomAt(1.0, anchor);
        const QPointF restored = zoomView.mapToScene(anchor);
        check(QLineF(before, restored).length() < 2.0,
              QStringLiteral("zoom out also stays under the cursor"));

        ToolPalette palette;
        QToolButton *shapeButton = nullptr;
        for (QToolButton *button : palette.findChildren<QToolButton *>()) {
            if (button->menu() && button->menu()->actions().size() >= 5)
                shapeButton = button;
        }
        check(shapeButton != nullptr,
              QStringLiteral("shape tools share one button with a menu"));
        if (shapeButton) {
            check(shapeButton->popupMode() == QToolButton::MenuButtonPopup,
                  QStringLiteral("shape menu opens from the button arrow"));
            palette.setCurrentTool(Tool::Star);
            check(shapeButton->property("toolId").toInt() == int(Tool::Star),
                  QStringLiteral("shape button reflects the current shape"));
        }

        PageDialog pageDialog;
        bool enabledSpins = true;
        for (QDoubleSpinBox *spin : pageDialog.findChildren<QDoubleSpinBox *>()) {
            if (!spin->isEnabled())
                enabledSpins = false;
        }
        check(enabledSpins,
              QStringLiteral("page size spin boxes are adjustable with arrows"));
    }

    // --- MainWindow history (undo / redo) -----------------------------------
    out << "\nHistory\n";
    {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        QDockWidget *libraryDock = nullptr;
        for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
            if (dock->windowTitle() == QStringLiteral("Библиотека"))
                libraryDock = dock;
        }
        check(libraryDock != nullptr && !libraryDock->isVisible(),
              QStringLiteral("library dock hidden on the welcome page"));
        window.openPath(dir.filePath(QStringLiteral("sample.vct")));
        QApplication::processEvents();
        check(libraryDock != nullptr && !libraryDock->isVisible(),
              QStringLiteral("library dock stays hidden until requested"));

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

            const auto effectCount = [canvas]() {
                int maximum = 0;
                for (VectorItem *item : canvas->vectorItems())
                    maximum = qMax(maximum, item->effects().size());
                return maximum;
            };
            canvas->scene()->clearSelection();
            canvas->vectorItems().last()->setSelected(true);
            QApplication::processEvents();
            const int effectsBefore = effectCount();
            canvas->addEffectToSelection(makeEffect(QStringLiteral("shadow")));
            check(effectCount() == effectsBefore + 1,
                  QStringLiteral("effect added through the canvas"));
            if (undoAction)
                undoAction->trigger();
            check(effectCount() == effectsBefore,
                  QStringLiteral("undo removes the effect"));
            if (redoAction)
                redoAction->trigger();
            check(effectCount() == effectsBefore + 1,
                  QStringLiteral("redo restores the effect"));

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

            const QRectF beforeOffset =
                buildTextPath(textItem->object()).boundingRect();
            view.setTextPathOffset(0.5);
            const QRectF afterOffset =
                buildTextPath(textItem->object()).boundingRect();
            check(afterOffset != beforeOffset
                      && qAbs(textItem->object().textPathOffset - 0.5) < 0.001,
                  QStringLiteral("text path offset moves the text"));
            view.setTextPathReverse(true);
            check(textItem->object().textPathReverse
                      && buildTextPath(textItem->object()).boundingRect()
                             != afterOffset,
                  QStringLiteral("text path direction can be reversed"));
            view.setTextPathReverse(false);
            view.setTextPathOffset(0.0);

            VectorObject straight;
            straight.type = ShapeType::Text;
            straight.text = QStringLiteral("ABC");
            straight.font = QFont(QStringLiteral("DejaVu Sans"), 24);
            straight.textBox = QRectF(0, 0, 300, 60);
            straight.textOnPath = true;
            straight.hasFill = true;
            straight.textPathCurve.moveTo(0, 100);
            straight.textPathCurve.lineTo(300, 100);
            const QRectF straightBox = buildTextPath(straight).boundingRect();
            check(!straightBox.isEmpty() && straightBox.bottom() > 95.0
                      && straightBox.bottom() < 105.0
                      && straightBox.top() < 90.0,
                  QStringLiteral("text sits on the curve, not upside down"));

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
