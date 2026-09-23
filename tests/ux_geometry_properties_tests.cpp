#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "geometry/FcGeometryObject.h"
#include "modeling/BuildingGeometryService.h"
#include "modeling/GeometryEditDependencyService.h"
#include "ui/BuildingElementDialog.h"
#include "ui/UiLanguage.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!condition) ++failures;
}
template<class Widget> Widget* widget(QObject& parent, const char* name)
{
    auto* result = parent.findChild<Widget*>(QString::fromLatin1(name));
    check(result != nullptr, name);
    return result;
}
void capturePropertyPages(BuildingElementDialog& dialog, const QString& prefix)
{
    const QString directory = qEnvironmentVariable("FIRECAE_UX_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    check(QDir().mkpath(directory), "offscreen screenshot directory exists");
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("GeometryPropertiesTabs"));
    if (!tabs) return;
    dialog.resize(800, 780);
    dialog.show(); // This executable forces offscreen before QApplication.
    const QStringList names{QStringLiteral("general"), QStringLiteral("geometry"),
                            QStringLiteral("surfaces"), QStringLiteral("advanced")};
    for (int index = 0; index < tabs->count(); ++index) {
        tabs->setCurrentIndex(index);
        QApplication::sendPostedEvents();
        QApplication::processEvents();
        const QString file = QDir(directory).filePath(prefix + QLatin1Char('-') + names.value(index) + QStringLiteral(".png"));
        check(dialog.grab().save(file), "Chinese property-page offscreen evidence saved");
    }
    dialog.hide();
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    if (!qEnvironmentVariableIsEmpty("FIRECAE_UX_SCREENSHOT_DIR")) {
        // The offscreen plugin does not enumerate the Windows desktop fonts.
        // Load a local CJK font in this test process only, never change the app
        // or system font settings merely to obtain legible evidence.
        const QString fontPath = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
            .filePath(QStringLiteral("Fonts/msyh.ttc"));
        const int fontId = QFontDatabase::addApplicationFont(fontPath);
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        check(fontId >= 0 && !families.isEmpty(), "offscreen Chinese evidence has an actual CJK font");
        if (!families.isEmpty()) application.setFont(QFont(families.first(), 9));
    }
#endif
    QTemporaryDir temporary;
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.UxGeometry.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Properties"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);
    FcProject project(QStringLiteral("Properties tests"));
    BuildingGeometryRequest request;
    request.kind = FcGeometryKind::ProfileExtrusion;
    request.profile3d = {{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}};
    request.extrusionDistance = 4.0;
    request.extraParameters.insert(QStringLiteral("sourceIfcGlobalId"), QStringLiteral("unchanged-provenance"));
    request.extraParameters.insert(QStringLiteral("description"), QStringLiteral("description"));
    QString error;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request, &error);
    check(!shape.IsNull() && error.isEmpty(), "3D planar polygon extrusion created");
    GProp_GProps mass;
    if (!shape.IsNull()) BRepGProp::VolumeProperties(shape, mass);
    check(std::abs(mass.Mass() - 24.0) < 1.0e-6, "extrusion physical volume correct");
    const auto roundtrip = BuildingGeometryService::requestFromParameters(request.kind,
        BuildingGeometryService::requestToParameters(request));
    check(roundtrip.profile3d == request.profile3d && roundtrip.extrusionDistance == 4.0,
          "XYZ vertices and extrusion round-trip without float precision loss");
    check(roundtrip.extraParameters.value(QStringLiteral("sourceIfcGlobalId")).toString() == QStringLiteral("unchanged-provenance"),
          "editing preserves unknown source metadata");
    auto invalid = request;
    invalid.profile3d[2][2] = 0.2;
    check(BuildingGeometryService::createShape(invalid, &error).IsNull(), "non-planar profile rejected");
    invalid = request;
    invalid.profile3d = {{0,0,0}, {2,3,0}, {0,3,0}, {2,0,0}};
    check(BuildingGeometryService::createShape(invalid, &error).IsNull(), "self-intersecting profile rejected");
    invalid = request;
    invalid.extrusionNormal = false;
    invalid.extrusionDirection = {1,0,0};
    check(BuildingGeometryService::createShape(invalid, &error).IsNull(), "in-plane extrusion direction rejected");
    invalid.extrusionDirection = {0,0,0};
    check(BuildingGeometryService::createShape(invalid, &error).IsNull(), "zero extrusion direction rejected");
    invalid.extrusionDirection = {0,0,7};
    check(!BuildingGeometryService::createShape(invalid, &error).IsNull(), "custom direction normalizes vector magnitude");
    BuildingGeometryRequest legacy;
    legacy.kind = FcGeometryKind::PolygonPrism;
    legacy.profile = {{0,0}, {2,0}, {2,3}, {0,3}};
    auto legacyParameters = BuildingGeometryService::requestToParameters(legacy);
    check(BuildingGeometryService::requestFromParameters(legacy.kind, legacyParameters).profile == legacy.profile,
          "legacy XY point pairs survive parameter serialization");
    legacyParameters.insert(QStringLiteral("profile"), QVariantList{0.,0.,2.,0.,2.,3.,0.,3.});
    check(BuildingGeometryService::requestFromParameters(legacy.kind, legacyParameters).profile == legacy.profile,
          "older flattened XY point arrays remain readable");

    auto object = std::make_shared<FcGeometryObject>(QStringLiteral("Same name"), shape);
    object->setGeometryKind(request.kind);
    object->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    project.document()->geometryGroup()->addChild(object);
    auto duplicateName = std::make_shared<FcGeometryObject>(QStringLiteral("Same name"), shape);
    duplicateName->setGeometryKind(request.kind);
    duplicateName->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    project.document()->geometryGroup()->addChild(duplicateName);
    const QVariantMap original = object->geometryParameters();
    BuildingElementDialog dialog(request.kind, &project);
    dialog.setExistingObject(object);
    auto* tabs = widget<QTabWidget>(dialog, "GeometryPropertiesTabs");
    check(tabs && tabs->count() == 4, "four actual property pages available");
    auto* table = widget<QTableWidget>(dialog, "GeometryProfilePointsTable");
    check(table && table->rowCount() == 4 && table->columnCount() == 3, "XYZ table reflects existing profile");
    check(dialog.validateInput(&error), "unchanged spatial geometry dialog validates");
    widget<QLineEdit>(dialog, "GeometryDescriptionEdit")->setText(QStringLiteral("changed"));
    table->item(1, 0)->setText(QStringLiteral("2.5"));
    check(dialog.request().profile3d[1][0] == 2.5, "table edits update canonical request");
    check(dialog.request().extraParameters.value(QStringLiteral("sourceIfcGlobalId")).toString() == QStringLiteral("unchanged-provenance"),
          "dialog edit retains provenance");
    dialog.reject();
    check(object->geometryParameters() == original && duplicateName->geometryParameters() == original,
          "cancel changes neither the selected object nor same-name neighbor");

    BuildingGeometryRequest box;
    box.fdsConversionRoute = QStringLiteral("OBST");
    box.rotationDegrees = 15.0;
    auto boxObject = std::make_shared<FcGeometryObject>(QStringLiteral("Box"), BuildingGeometryService::createShape(box));
    boxObject->setGeometryKind(box.kind);
    boxObject->setGeometryParameters(BuildingGeometryService::requestToParameters(box));
    project.document()->geometryGroup()->addChild(boxObject);
    BuildingElementDialog boxDialog(box.kind, &project);
    boxDialog.setExistingObject(boxObject);
    check(boxDialog.request().rotationDegrees == 15.0, "existing rotation not silently reset by properties");
    auto* flag = widget<QCheckBox>(boxDialog, "GeometryFds_BNDF_OBST");
    check(flag->checkState() == Qt::PartiallyChecked, "unset physics flags retain FDS defaults");
    flag->setCheckState(Qt::Checked);
    check(boxDialog.request().extraParameters.value(QStringLiteral("fdsAdditionalFields")).toMap()
        .value(QStringLiteral("BNDF_OBST")).toString() == QStringLiteral(".TRUE."), "general flag produces explicit typed exporter option");
    check(boxDialog.validateInput(&error), "explicit OBST flag validates");
    auto* advanced = widget<QTableWidget>(boxDialog, "GeometryAdvancedFieldsTable");
    advanced->setRowCount(1);
    advanced->setItem(0, 0, new QTableWidgetItem(QStringLiteral("BNDF_OBST")));
    advanced->setItem(0, 1, new QTableWidgetItem(QStringLiteral(".FALSE.")));
    check(!boxDialog.validateInput(&error), "advanced/general conflicting fields rejected");
    advanced->item(0, 0)->setText(QStringLiteral("SURF_ID"));
    check(!boxDialog.validateInput(&error), "raw references cannot bypass typed surface controls");
    advanced->setRowCount(0);
    auto* route = widget<QComboBox>(boxDialog, "FdsGeometryConversionRouteCombo");
    route->setCurrentIndex(route->findData(QStringLiteral("GEOM")));
    check(!boxDialog.validateInput(&error), "OBST flags rejected on GEOM route");
    route->setCurrentIndex(route->findData(QStringLiteral("OBST")));

    const QString missingSurface = QStringLiteral("{ffffffff-0000-4000-8000-000000000001}");
    boxObject->setDefaultSurfaceId(missingSurface);
    BuildingElementDialog brokenReference(box.kind, &project);
    brokenReference.setExistingObject(boxObject);
    check(brokenReference.surfaceObjectId() == missingSurface &&
          widget<QComboBox>(brokenReference, "GeometrySurfaceCombo")->currentText().startsWith(QStringLiteral("Missing reference:")),
          "missing default surface is shown as a placeholder and retains its original UUID");
    check(!brokenReference.validateInput(&error), "missing default surface blocks commit rather than silently selecting None");
    brokenReference.reject();
    check(boxObject->defaultSurfaceId() == missingSurface, "viewing or cancelling a broken reference leaves original data intact");
    auto* repairSurface = widget<QComboBox>(brokenReference, "GeometrySurfaceCombo");
    repairSurface->setCurrentIndex(repairSurface->findData(QString()));
    check(brokenReference.validateInput(&error) && brokenReference.surfaceObjectId().isEmpty(),
          "explicitly selecting None acknowledges and clears the missing reference");
    boxObject->setDefaultSurfaceId({});
    boxObject->setControlObjectId(missingSurface);
    BuildingElementDialog brokenControl(box.kind, &project);
    brokenControl.setExistingObject(boxObject);
    check(brokenControl.controlObjectId() == missingSurface && !brokenControl.validateInput(&error),
          "missing activation reference is retained and cannot silently disappear");
    boxObject->setControlObjectId({});

    auto surface = std::make_shared<FcFdsNamelist>(QStringLiteral("Surface"), FcObjectType::Surface,
        QStringLiteral("SURF"), QStringLiteral("TEST_SURFACE"));
    project.document()->surfacesGroup()->addChild(surface);
    BuildingElementDialog kindSwitch(FcGeometryKind::RectangularOpening, &project);
    auto* hosts = widget<QComboBox>(kindSwitch, "OpeningHostCombo");
    hosts->setCurrentIndex(hosts->findData(boxObject->id()));
    widget<QCheckBox>(kindSwitch, "DynamicOpeningCheck")->setChecked(true);
    auto* kinds = widget<QComboBox>(kindSwitch, "BuildingKindCombo");
    kinds->setCurrentIndex(kinds->findData(static_cast<int>(FcGeometryKind::Box)));
    check(kindSwitch.hostObjectId().isEmpty() && !kindSwitch.dynamicOpening(),
          "switching to a solid does not retain hidden opening-host semantics");
    const auto faces = BuildingGeometryService::faceInfos(boxObject->shape());
    QMap<QString, QString> faceAssignments;
    for (const auto& face : faces) faceAssignments.insert(face.key, surface->id());
    boxObject->setFaceSurfaceIds(faceAssignments);
    BuildingElementDialog faceDialog(box.kind, &project);
    faceDialog.setExistingObject(boxObject);
    auto* faceRoute = widget<QComboBox>(faceDialog, "FdsGeometryConversionRouteCombo");
    faceRoute->setCurrentIndex(faceRoute->findData(QStringLiteral("GEOM")));
    check(faceDialog.validateInput(&error), "existing real-face UUID assignment validates");
    widget<QDoubleSpinBox>(faceDialog, "BuildingWidthSpin")->setValue(7.0);
    check(!faceDialog.validateInput(&error), "geometry change cannot silently reuse obsolete face signatures");
    widget<QComboBox>(faceDialog, "GeometrySurfaceModeCombo")->setCurrentIndex(0);
    check(faceDialog.validateInput(&error), "explicit uniform surface choice discards obsolete overrides");

    BuildingGeometryRequest autoBox;
    auto autoBoxObject = std::make_shared<FcGeometryObject>(QStringLiteral("Automatic box"), BuildingGeometryService::createShape(autoBox));
    autoBoxObject->setGeometryKind(autoBox.kind);
    autoBoxObject->setGeometryParameters(BuildingGeometryService::requestToParameters(autoBox));
    QMap<QString, QString> automaticFaceAssignments;
    for (const auto& face : BuildingGeometryService::faceInfos(autoBoxObject->shape()))
        automaticFaceAssignments.insert(face.key, surface->id());
    autoBoxObject->setFaceSurfaceIds(automaticFaceAssignments);
    BuildingElementDialog autoBoxFaces(autoBox.kind, &project);
    autoBoxFaces.setExistingObject(autoBoxObject);
    check(!autoBoxFaces.validateInput(&error), "Auto box cannot silently discard topological face surfaces when exporting OBST");

    auto geomBox = autoBox;
    geomBox.fdsConversionRoute = QStringLiteral("GEOM");
    autoBoxObject->setGeometryParameters(BuildingGeometryService::requestToParameters(geomBox));
    QMap<QString, QString> partialFaceAssignments;
    partialFaceAssignments.insert(automaticFaceAssignments.firstKey(), surface->id());
    autoBoxObject->setFaceSurfaceIds(partialFaceAssignments);
    BuildingElementDialog partialFaces(autoBox.kind, &project);
    partialFaces.setExistingObject(autoBoxObject);
    check(!partialFaces.validateInput(&error), "partially assigned GEOM faces require an explicit default surface");
    auto* defaultSurface = widget<QComboBox>(partialFaces, "GeometrySurfaceCombo");
    defaultSurface->setCurrentIndex(defaultSurface->findData(surface->id()));
    check(partialFaces.validateInput(&error), "GEOM partial face assignments validate once remaining faces have a default surface");
    object->setLocked(true);
    BuildingElementDialog locked(request.kind, &project);
    locked.setExistingObject(object);
    auto* lockedTabs = widget<QTabWidget>(locked, "GeometryPropertiesTabs");
    lockedTabs->setCurrentIndex(3);
    check(lockedTabs->isEnabled() && lockedTabs->currentIndex() == 3 &&
          widget<QLineEdit>(locked, "BuildingNameEdit")->isReadOnly() &&
          widget<QTableWidget>(locked, "GeometryAdvancedFieldsTable")->editTriggers() == QAbstractItemView::NoEditTriggers,
          "locked geometry pages remain navigable while fields cannot be modified");

    const QString file = temporary.filePath(QStringLiteral("properties.firecae"));
    check(FcProjectSerializer::save(project, file, &error), "spatial geometry project saved");
    auto loaded = FcProjectSerializer::load(file);
    check(loaded.success(), "spatial geometry project reopened");
    if (loaded.success()) {
        const auto restored = std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(object->id()));
        check(restored && BuildingGeometryService::requestFromParameters(restored->geometryKind(), restored->geometryParameters()).profile3d == request.profile3d,
              "archive preserves actual XYZ profile by persistent UUID");
    }

    FcProject openingProject(QStringLiteral("Opening dependencies"));
    BuildingGeometryRequest wall;
    wall.kind = FcGeometryKind::Wall;
    wall.endX = 4.0;
    wall.thickness = 0.2;
    wall.height = 3.0;
    auto host = std::make_shared<FcGeometryObject>(QStringLiteral("Host wall"), BuildingGeometryService::createShape(wall));
    host->setGeometryKind(wall.kind);
    host->setGeometryParameters(BuildingGeometryService::requestToParameters(wall));
    openingProject.document()->geometryGroup()->addChild(host);
    BuildingGeometryRequest opening;
    opening.kind = FcGeometryKind::RectangularOpening;
    opening.x = 1.0; opening.y = -0.2; opening.z = .5;
    opening.width = 1.0; opening.depth = .4; opening.height = 2.0;
    auto hole = std::make_shared<FcGeometryObject>(QStringLiteral("Opening"), BuildingGeometryService::createShape(opening));
    hole->setGeometryKind(opening.kind);
    hole->setGeometryParameters(BuildingGeometryService::requestToParameters(opening));
    hole->setHostObjectId(host->id());
    openingProject.document()->geometryGroup()->addChild(hole);
    check(GeometryEditDependencyService::validateOpeningPlacement(opening, wall).isEmpty(),
          "opening fits straight wall and passes through its thickness");
    auto geomWall = wall;
    geomWall.fdsConversionRoute = QStringLiteral("GEOM");
    check(!GeometryEditDependencyService::validateOpeningPlacement(opening, geomWall).isEmpty(),
          "HOLE cannot be falsely offered as a native GEOM wall cut");
    auto rotatedWall = wall;
    rotatedWall.endY = 4.0;
    check(!GeometryEditDependencyService::validateOpeningPlacement(opening, rotatedWall).isEmpty(),
          "rotated wall opening is rejected instead of exporting an oversized global bounding-box hole");
    auto thickWall = wall;
    thickWall.thickness = .5;
    const auto deeperOpening = GeometryEditDependencyService::updatedHostedOpening(opening, wall, thickWall, &error);
    check(error.isEmpty() && std::abs(deeperOpening.depth - .7) < 1.0e-9 &&
          std::abs(deeperOpening.y + .35) < 1.0e-9, "host thickness edit preserves opening through-cut and protrusion margins");
    check(GeometryEditDependencyService::validate(*openingProject.document(), *host, thickWall).isEmpty(),
          "safe wall thickness edit passes dependency validation");
    auto shortWall = wall;
    shortWall.endX = 1.5;
    check(!GeometryEditDependencyService::validate(*openingProject.document(), *host, shortWall).isEmpty(),
          "wall shrink that strands an opening is rejected");
    hole->setLocked(true);
    check(!GeometryEditDependencyService::validate(*openingProject.document(), *host, thickWall).isEmpty(),
          "locked attached opening prevents host mutation");
    hole->setLocked(false);
    hole->setVisible(false);
    const auto cutShape = GeometryEditDependencyService::displayShape(*host, {host, hole});
    GProp_GProps cutMass, hostMass;
    BRepGProp::VolumeProperties(cutShape, cutMass);
    BRepGProp::VolumeProperties(host->shape(), hostMass);
    check(std::abs(cutMass.Mass() - 2.0) < 1.0e-6 && std::abs(hostMass.Mass() - 2.4) < 1.0e-6,
          "static opening actually cuts host presentation without mutating host business geometry or depending on visibility");
    hole->setDynamicOpening(true);
    GProp_GProps dynamicMass;
    BRepGProp::VolumeProperties(GeometryEditDependencyService::displayShape(*host, {host, hole}), dynamicMass);
    check(std::abs(dynamicMass.Mass() - 2.4) < 1.0e-6, "dynamic opening is not falsely rendered as always open");
    auto fireSurface = std::make_shared<FcFdsSurface>(QStringLiteral("Fire"), QStringLiteral("FIRE"), 500.0);
    openingProject.document()->surfacesGroup()->addChild(fireSurface);
    host->setDefaultSurfaceId(fireSurface->id());
    check(!GeometryEditDependencyService::validate(*openingProject.document(), *host, thickWall).isEmpty(),
          "burning-area changes require an explicit power decision");
    host->setDefaultSurfaceId({});
    auto device = std::make_shared<FcFdsNamelist>(QStringLiteral("Attached device"), FcObjectType::Device, QStringLiteral("DEVC"), QStringLiteral("DETECTOR"));
    device->addReferenceParameter(QStringLiteral("GEOM_ID"), {host->id()});
    openingProject.document()->devicesGroup()->addChild(device);
    check(!GeometryEditDependencyService::validate(*openingProject.document(), *host, thickWall).isEmpty(),
          "unknown solver attachments block unsupported changes");
    UiLanguageManager::setCurrentLanguage(UiLanguage::ChineseSimplified);
    BuildingElementDialog chinesePartialFaces(autoBox.kind, &project);
    chinesePartialFaces.setExistingObject(autoBoxObject);
    check(!chinesePartialFaces.validateInput(&error) &&
          error.contains(QStringLiteral("必须明确选择默认表面")) && !error.contains(QStringLiteral("Partial face")),
          "missing default surface is explained in Chinese before committing partial face assignments");
    BuildingElementDialog chinese(FcGeometryKind::Box, &project);
    chinese.setExistingObject(boxObject);
    const QString summary = widget<QLabel>(chinese, "GeometryPreviewSummary")->text();
    check(summary.contains(QChar(0x4f53)) && !summary.contains(QChar(0x00c2)),
          "UTF-8 scientific units resolve Chinese translations without mojibake");
    check(!summary.contains(QStringLiteral("Geometry")) && !summary.contains(QStringLiteral("Vertices")) &&
          summary.contains(QStringLiteral("顶点数：8")),
          "Chinese preview uses unique topological vertices and has no English quality warning");
    check(widget<QLabel>(chinese, "GeometryBoundsSummary")->text().contains(QStringLiteral("Z: 0 … 3")),
          "exact box bounds do not expose geometry tolerance as a negative near-zero coordinate");
    auto* chineseKinds = widget<QComboBox>(chinese, "BuildingKindCombo");
    bool allKindsChinese = true;
    for (int index = 0; index < chineseKinds->count(); ++index) {
        bool hasChinese = false;
        for (const QChar character : chineseKinds->itemText(index))
            if (character.unicode() >= 0x4e00 && character.unicode() <= 0x9fff) hasChinese = true;
        allKindsChinese = allKindsChinese && hasChinese;
    }
    check(allKindsChinese && chineseKinds->itemText(chineseKinds->findData(static_cast<int>(FcGeometryKind::ProfileExtrusion))) ==
          QStringLiteral("轮廓拉伸体") && chineseKinds->itemText(chineseKinds->findData(static_cast<int>(FcGeometryKind::Ramp))) ==
          QStringLiteral("坡道"), "all geometry labels are Chinese and geometric ramps are not FDS time curves");
    QStringList buttonTexts;
    for (const QPushButton* button : chinese.findChildren<QPushButton*>()) buttonTexts.append(button->text());
    check(buttonTexts.contains(QStringLiteral("插入行")) && buttonTexts.contains(QStringLiteral("删除行")) &&
          buttonTexts.contains(QStringLiteral("剪切")) && buttonTexts.contains(QStringLiteral("粘贴")) &&
          !buttonTexts.contains(QStringLiteral("Insert Row")) && !buttonTexts.contains(QStringLiteral("Paste")),
          "table editing actions are fully translated in Chinese properties");
    const auto faceButtons = chinese.findChildren<QPushButton*>(QStringLiteral("GeometryFacePreviewButton"));
    bool allFacesChinese = !faceButtons.isEmpty();
    for (const QPushButton* button : faceButtons)
        allFacesChinese = allFacesChinese && button->text().startsWith(QStringLiteral("面 ")) &&
                          !button->text().contains(QStringLiteral("Face"));
    check(allFacesChinese, "topological face labels translate their template before formatting the face index and area");
    check(UiLanguageManager::text(QStringLiteral("Geometry contains shells but no closed solid.")) ==
          QStringLiteral("几何包含壳体，但没有封闭实体。") &&
          UiLanguageManager::text(QStringLiteral("Geometry topology contains %1 coincident vertex occurrence(s); repair can merge them."))
              .arg(3).contains(QStringLiteral("3 个位置重合")) &&
          UiLanguageManager::text(QStringLiteral("Geometry contains %1 duplicate topological face(s)."))
              .arg(2) == QStringLiteral("几何包含 2 个重复拓扑面。"),
          "quality warnings and formatted error counts have Chinese translations");
    capturePropertyPages(chinese, QStringLiteral("properties-zh"));
    BuildingElementDialog readonlyChinese(request.kind, &project);
    readonlyChinese.setExistingObject(object);
    capturePropertyPages(readonlyChinese, QStringLiteral("properties-zh-readonly"));
    return failures == 0 ? 0 : 1;
}
