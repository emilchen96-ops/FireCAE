#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsExamples.h"
#include "fds/FdsWriter.h"
#include "geometry/FcIfcObject.h"
#include "import/IfcImportService.h"
#include "visualization/GeometryDisplayManager.h"

#include <AIS_ColoredShape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <V3d_Viewer.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message)
{
    std::cout << (value ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!value) ++failures;
}
bool almostEqual(double a, double b) { return std::abs(a - b) < 1.0e-6; }
bool equal(const FcIfcAppearance& a, const FcIfcAppearance& b)
{
    return almostEqual(a.red, b.red) && almostEqual(a.green, b.green) && almostEqual(a.blue, b.blue) &&
           almostEqual(a.alpha, b.alpha) && a.origin == b.origin && a.materialName == b.materialName;
}
std::shared_ptr<FcIfcObject> firstShape(const std::shared_ptr<FcIfcObject>& object)
{
    if (!object) return {};
    if (object->hasShape()) return object;
    for (const auto& child : object->children())
        if (const auto found = firstShape(std::dynamic_pointer_cast<FcIfcObject>(child))) return found;
    return {};
}
void inspectMaterials(const std::shared_ptr<FcIfcObject>& object)
{
    bool red = false, blue = false;
    if (object) for (const auto& value : object->faceAppearances()) {
        red |= almostEqual(value.red, 0.8) && almostEqual(value.green, 0.2) && almostEqual(value.blue, 0.1) && almostEqual(value.alpha, 0.65);
        blue |= almostEqual(value.red, 0.1) && almostEqual(value.green, 0.3) && almostEqual(value.blue, 0.9) && almostEqual(value.alpha, 0.25);
    }
    check(red && blue, "Both source face colors and distinct alpha values survive the conversion pipeline");
}
void checkPresentation(const std::shared_ptr<FcIfcObject>& object)
{
    const Handle(AIS_ColoredShape) presentation = Handle(AIS_ColoredShape)::DownCast(
        GeometryDisplayManager::createIfcPresentation(*object));
    check(!presentation.IsNull(), "IFC uses a per-face colored presentation");
    if (presentation.IsNull()) return;
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(object->shape(), TopAbs_FACE, faces);
    bool match = true;
    for (auto it = object->faceAppearances().cbegin(); it != object->faceAppearances().cend(); ++it) {
        const auto drawer = presentation->CustomAspects(faces(it.key()));
        const auto& color = drawer->ShadingAspect()->Color();
        match &= almostEqual(color.Red(), it->red) && almostEqual(color.Green(), it->green) && almostEqual(color.Blue(), it->blue) &&
                 almostEqual(drawer->ShadingAspect()->Transparency(), 1.0 - it->alpha);
    }
    check(match, "AIS face drawers receive exact linear RGB and transparency");
    check(!presentation->Attributes()->IsoOnTriangulation(), "IFC does not enable triangulation isolines");
}
QJsonValue modifyIfcAppearanceJson(const QJsonValue& value, bool removeAppearance)
{
    if (value.isArray()) {
        QJsonArray result;
        for (const auto& item : value.toArray()) result.append(modifyIfcAppearanceJson(item, removeAppearance));
        return result;
    }
    if (!value.isObject()) return value;
    QJsonObject result = value.toObject();
    for (auto it = result.begin(); it != result.end(); ++it)
        it.value() = modifyIfcAppearanceJson(it.value(), removeAppearance);
    if (result.value(QStringLiteral("storageClass")).toString() == QStringLiteral("Ifc")) {
        if (removeAppearance) {
            result.remove(QStringLiteral("ifcAppearance"));
            result.remove(QStringLiteral("ifcFaceAppearances"));
        } else if (!result.value(QStringLiteral("ifcFaceAppearances")).toArray().isEmpty()) {
            QJsonArray faces = result.value(QStringLiteral("ifcFaceAppearances")).toArray();
            QJsonObject face = faces[0].toObject();
            face.insert(QStringLiteral("faceIndex"), 1000000);
            faces[0] = face;
            result.insert(QStringLiteral("ifcFaceAppearances"), faces);
        }
    }
    return result;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QDir data(QStringLiteral(FIRECAE_TEST_DATA_DIR));
    IfcImportService importer(data.absoluteFilePath(QStringLiteral("../../third_party/ifcopenshell/IfcConvert.exe")));
    const auto fallbackImport = importer.importFile(data.filePath(QStringLiteral("tessellated-item.ifc")));
    check(fallbackImport.success(), "Unstyled IFC fixture imports");
    const auto fallback = firstShape(fallbackImport.rootObject);
    check(fallback && !fallback->hasSourceAppearance() && fallback->hasFallbackAppearance(),
          "Converter DefaultMaterial is identified as fallback, not IFC source color");
    if (fallback) {
        check(equal(fallback->appearance(), FcIfcObject::typeFallbackAppearance(fallback->ifcClass())),
              "Unstyled IFC uses stable type fallback");
        const auto edges = GeometryDisplayManager::ifcFeatureEdges(fallback->shape());
        check(edges.size() == 12, "Triangulated cube has twelve feature edges with no six face diagonals");
    }
    check(!equal(FcIfcObject::typeFallbackAppearance(QStringLiteral("IfcDoor")),
                 FcIfcObject::typeFallbackAppearance(QStringLiteral("IfcWindow"))),
          "Doors and windows use distinct deterministic fallback colors");

    QTemporaryDir temporary;
    QFile input(data.filePath(QStringLiteral("tessellated-item.ifc")));
    check(temporary.isValid() && input.open(QIODevice::ReadOnly), "Fixture workspace is available");
    QByteArray fixture = input.readAll();
    fixture.replace("'Tessellation',(#1021)", "'Tessellation',(#1021,#1023)");
    const QByteArray styles =
        "#1023= IFCTRIANGULATEDFACESET(#1022,$,.F.,((1,2,3)),$);\n"
        "#20001= IFCCOLOURRGB($,0.8,0.2,0.1);\n"
        "#20002= IFCSURFACESTYLERENDERING(#20001,0.35,$,$,$,$,$,$,.NOTDEFINED.);\n"
        "#20003= IFCSURFACESTYLE('Source red',.BOTH.,(#20002));\n"
        "#20004= IFCSTYLEDITEM(#1021,(#20003),$);\n"
        "#20011= IFCCOLOURRGB($,0.1,0.3,0.9);\n"
        "#20012= IFCSURFACESTYLERENDERING(#20011,0.75,$,$,$,$,$,$,.NOTDEFINED.);\n"
        "#20013= IFCSURFACESTYLE('Source blue',.BOTH.,(#20012));\n"
        "#20014= IFCSTYLEDITEM(#1023,(#20013),$);\n";
    fixture.replace("ENDSEC;\r\nEND-ISO", styles + "ENDSEC;\r\nEND-ISO");
    fixture.replace("ENDSEC;\nEND-ISO", styles + "ENDSEC;\nEND-ISO");
    const QString styledPath = temporary.filePath(QStringLiteral("styled.ifc"));
    QFile styledFile(styledPath);
    check(styledFile.open(QIODevice::WriteOnly) && styledFile.write(fixture) == fixture.size(),
          "Two-material IFC fixture is written");
    styledFile.close();
    const auto imported = importer.importFile(styledPath);
    check(imported.success(), "IFC with two source materials imports");
    if (!imported.success()) std::cerr << imported.errorMessage.toStdString() << std::endl;
    const auto object = firstShape(imported.rootObject);
    check(object && object->hasSourceAppearance() && !object->hasFallbackAppearance(),
          "Complete source styling is distinguished from fallback");
    if (!object) return failures + 1;
    inspectMaterials(object);
    checkPresentation(object);

    auto project = FdsExamples::createSimpleTestProject();
    project->document()->geometryGroup()->addChild(imported.rootObject);
    const auto before = FdsWriter::render(*project);
    object->setVisible(false);
    const QString projectPath = temporary.filePath(QStringLiteral("appearance.fcproj"));
    QString error;
    check(FcProjectSerializer::save(*project, projectPath, &error), "Appearance project saves");
    const auto loaded = FcProjectSerializer::load(projectPath);
    check(loaded.success(), "Appearance project reopens");
    const auto restored = loaded.success() ? std::dynamic_pointer_cast<FcIfcObject>(
        loaded.project->document()->findObject(object->id())) : nullptr;
    check(restored && equal(restored->appearance(), object->appearance()) &&
          restored->faceAppearances().size() == object->faceAppearances().size(),
          "UUID, component appearance and face mapping survive save/reopen");
    if (restored) {
        bool faceIndicesMatch = true;
        for (auto it = object->faceAppearances().cbegin(); it != object->faceAppearances().cend(); ++it)
            faceIndicesMatch &= equal(restored->appearanceForFace(it.key()), it.value());
        check(faceIndicesMatch, "Each persisted BREP face retains its own material assignment");
        inspectMaterials(restored);
        checkPresentation(restored);
        check(!restored->isVisible() && restored->fdsConversionRoute() == QStringLiteral("REFERENCE"),
              "Visibility and IFC reference semantics survive save/reopen independently");
    }
    const auto after = FdsWriter::render(*project);
    check(before.success() && after.success() && before.text == after.text,
          "Appearance and visibility leave FDS export unchanged");
    check(loaded.success() && before.text == FdsWriter::render(*loaded.project).text,
          "Saved and reopened appearance leaves FDS export unchanged");
    QFile savedFile(projectPath);
    check(savedFile.open(QIODevice::ReadOnly), "Saved appearance JSON is readable");
    const auto savedJson = QJsonDocument::fromJson(savedFile.readAll()).object();
    savedFile.close();
    for (bool legacy : {true, false}) {
        const QString modifiedPath = temporary.filePath(legacy ? QStringLiteral("legacy.fcproj")
                                                               : QStringLiteral("invalid-face.fcproj"));
        QFile modified(modifiedPath);
        const QByteArray bytes = QJsonDocument(modifyIfcAppearanceJson(savedJson, legacy).toObject()).toJson();
        check(modified.open(QIODevice::WriteOnly) && modified.write(bytes) == bytes.size(), "Modified regression project written");
        modified.close();
        const auto result = FcProjectSerializer::load(modifiedPath);
        if (legacy) {
            const auto item = result.success() ? std::dynamic_pointer_cast<FcIfcObject>(
                result.project->document()->findObject(object->id())) : nullptr;
            check(item && item->hasFallbackAppearance() && !item->hasSourceAppearance(),
                  "Legacy projects without appearance still load with labelled type fallback");
        } else check(!result.success(), "Invalid persisted face mapping is rejected instead of reassigned");
    }

    // No native window, view, OpenGL context, or GUI event loop is created.
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    {
        GeometryDisplayManager manager(context);
        check(manager.displayIfcModel(imported.rootObject), "Headless OCCT context accepts styled IFC");
        check(manager.selectObject(object->id()), "IFC remains selectable by FireCAE UUID");
        manager.clearSelection();
        check(manager.selectedObjectIds().isEmpty() && object->hasSourceAppearance(),
              "Selection cancellation restores the unchanged source appearance");
        const auto displayed = Handle(AIS_ColoredShape)::DownCast(manager.presentationForObject(object->id()));
        check(!displayed.IsNull() && displayed->CustomAspectsMap().Extent() == object->faceAppearances().size(),
              "Selection cancellation preserves all face presentation drawers");
        manager.hideObject(object->id()); manager.showObject(object->id());
        check(manager.isObjectVisible(object->id()) && before.text == FdsWriter::render(*project).text,
              "Hide/show preserves presentation identity and physical export");
        manager.clear(false);
    }

    IfcImportOptions transformed;
    transformed.additionalScale = 2.0; transformed.originX = 3.0;
    transformed.sourceYAxisUp = true; transformed.mergeStrategy = QStringLiteral("MERGE_ALL");
    const auto merged = importer.importFile(styledPath, transformed);
    check(merged.success(), "Transform and merge import succeeds");
    inspectMaterials(firstShape(merged.rootObject));
    IfcImportOptions disabled;
    disabled.preserveMaterials = false;
    const auto ignored = firstShape(importer.importFile(styledPath, disabled).rootObject);
    check(ignored && !ignored->hasSourceAppearance() && ignored->hasFallbackAppearance(),
          "Disabling material preservation deliberately uses labelled fallback");
    disabled.preserveMaterials = true; disabled.simplification = QStringLiteral("BOUNDING_BOX");
    const auto simplified = importer.importFile(styledPath, disabled);
    const auto box = firstShape(simplified.rootObject);
    check(box && !box->hasSourceAppearance() && box->faceAppearances().isEmpty() &&
          !simplified.warnings.isEmpty(), "Bounding-box topology drops source face mapping with an explicit warning");

    FcIfcObject replaced(QStringLiteral("Replacement"), QStringLiteral("IfcWall"), QStringLiteral("replacement"));
    replaced.setShape(BRepPrimAPI_MakeBox(1., 1., 1.).Shape());
    check(replaced.setFaceAppearance(1, object->appearanceForFace(1)) &&
          !replaced.setFaceAppearance(7, object->appearanceForFace(1)), "Face assignments validate actual topology");
    replaced.setShape(BRepPrimAPI_MakeBox(2., 2., 2.).Shape());
    check(replaced.faceAppearances().isEmpty(), "Replacing geometry clears stale face associations");
    return failures == 0 ? 0 : 1;
}
