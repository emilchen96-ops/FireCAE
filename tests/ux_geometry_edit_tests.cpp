#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsWriter.h"
#include "geometry/FcGeometryObject.h"
#include "modeling/BuildingGeometryService.h"
#include "modeling/GeometryEditService.h"

#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <QCoreApplication>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
using Vec = std::array<double, 3>;
void check(bool value, const QString& message)
{
    std::cout << (value ? "PASS: " : "FAIL: ") << message.toStdString() << std::endl;
    if (!value) ++failures;
}
bool almostEqual(double a, double b) { return std::abs(a - b) < 1.0e-6; }
double dot(const Vec& a, const Vec& b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
std::pair<double, double> support(const TopoDS_Shape& shape, const Vec& axis)
{
    double low = std::numeric_limits<double>::infinity(), high = -low;
    if (shape.IsNull()) return {low, high};
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (int i = 1; i <= vertices.Extent(); ++i) {
        const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
        const double projection = point.X()*axis[0] + point.Y()*axis[1] + point.Z()*axis[2];
        low = std::min(low, projection); high = std::max(high, projection);
    }
    return {low, high};
}
GeometryEditHandle handle(const BuildingGeometryRequest& request, const QString& key)
{
    const auto handles = GeometryEditService::handles(request);
    const auto found = std::find_if(handles.cbegin(), handles.cend(), [&](const auto& item) { return item.key == key; });
    check(found != handles.cend(), QStringLiteral("Handle exists: %1").arg(key));
    return found == handles.cend() ? GeometryEditHandle{} : *found;
}
BuildingGeometryRequest checkFixedFace(const BuildingGeometryRequest& before,
                                       const QString& key, double delta, bool lower)
{
    BuildingGeometryRequest after;
    QString error;
    const QVariantMap unchanged = BuildingGeometryService::requestToParameters(before);
    const bool success = GeometryEditService::moveHandle(before, key, delta, 0., &after, &error);
    check(success, QStringLiteral("Edit succeeds: %1 (%2)").arg(key, error));
    if (!success) return before;
    const auto axis = handle(before, key).direction;
    const auto a = support(BuildingGeometryService::createShape(before), axis);
    const auto b = support(BuildingGeometryService::createShape(after), axis);
    check(almostEqual(lower ? a.second : a.first, lower ? b.second : b.first),
          QStringLiteral("%1 preserves the opposite physical face").arg(key));
    check(almostEqual((lower ? b.first-a.first : b.second-a.second), delta),
          QStringLiteral("%1 moves its physical face by the exact requested displacement").arg(key));
    check(BuildingGeometryService::requestToParameters(before) == unchanged,
          QStringLiteral("%1 preview does not mutate its source request").arg(key));
    return after;
}
std::shared_ptr<FcGeometryObject> geometry(const QString& name, const BuildingGeometryRequest& request)
{
    QString error;
    const auto shape = BuildingGeometryService::createShape(request, &error);
    check(!shape.IsNull(), QStringLiteral("Geometry generated for %1: %2").arg(name, error));
    auto object = std::make_shared<FcGeometryObject>(name, shape);
    object->setGeometryKind(request.kind);
    object->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    return object;
}
QString convertedSignature(const FdsBlockConversionResult& converted)
{
    QStringList records;
    for (const auto& item : converted.fdsObjects) {
        const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(item);
        if (!record) { records.append(QStringLiteral("not-a-namelist")); continue; }
        QStringList fields{record->keyword()};
        for (const auto& parameter : record->parameters())
            fields.append(parameter.key + QLatin1Char('=') + parameter.value +
                          parameter.targetObjectIds.join(QLatin1Char(',')));
        records.append(fields.join(QLatin1Char(';')));
    }
    return records.join(QLatin1Char('\n'));
}
void expectInvalid(const BuildingGeometryRequest& request, const QString& key, double delta,
                   const QString& message)
{
    BuildingGeometryRequest after;
    after.x = 12345.;
    const QVariantMap before = BuildingGeometryService::requestToParameters(after);
    QString error;
    check(!GeometryEditService::moveHandle(request, key, delta, 0., &after, &error) &&
          !error.isEmpty() && BuildingGeometryService::requestToParameters(after) == before, message);
}
BuildingGeometryRequest checkTransform(const BuildingGeometryRequest& before, const gp_Trsf& transform,
                                       const QString& editKey, const QString& name)
{
    const auto unchanged = BuildingGeometryService::requestToParameters(before);
    BuildingGeometryRequest after;
    QString error;
    const bool transformed = GeometryEditService::transformRequest(before, transform, &after, &error);
    check(transformed, QStringLiteral("%1 keeps parametric data: %2").arg(name, error));
    if (!transformed) return before;
    const auto expected = BRepBuilderAPI_Transform(BuildingGeometryService::createShape(before), transform, true, true).Shape();
    check(GeometryEditService::matchesShape(after, expected, 1.0e-6, &error),
          QStringLiteral("%1 regenerates the transformed BREP: %2").arg(name, error));
    check(BuildingGeometryService::requestToParameters(before) == unchanged,
          QStringLiteral("%1 leaves the original parameters untouched").arg(name));
    const auto edited = checkFixedFace(after, editKey, 0.25, false);
    const auto direction = handle(after, editKey).direction;
    const auto transformedSupport = support(expected, direction);
    const auto editedSupport = support(BuildingGeometryService::createShape(edited), direction);
    check(almostEqual(transformedSupport.first, editedSupport.first) &&
          almostEqual(editedSupport.second - transformedSupport.second, 0.25),
          QStringLiteral("%1 then %2 edits the transformed position without jumping back").arg(name, editKey));
    return after;
}
void rejectTransform(const BuildingGeometryRequest& before, const gp_Trsf& transform, const QString& message)
{
    BuildingGeometryRequest after;
    after.x = 9876.;
    const auto unchanged = BuildingGeometryService::requestToParameters(after);
    QString error;
    check(!GeometryEditService::transformRequest(before, transform, &after, &error) && !error.isEmpty() &&
          BuildingGeometryService::requestToParameters(after) == unchanged, message);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    BuildingGeometryRequest box;
    box.x = 1.2; box.y = -2.3; box.z = 0.4;
    box.width = 4.; box.depth = 3.; box.height = 2.; box.rotationDegrees = 37.;
    for (const QString& key : {QStringLiteral("X-"), QStringLiteral("X+"),
                               QStringLiteral("Y-"), QStringLiteral("Y+"),
                               QStringLiteral("Z-"), QStringLiteral("Z+")})
        checkFixedFace(box, key, key.endsWith('-') ? -0.35 : 0.35, key.endsWith('-'));
    expectInvalid(box, QStringLiteral("X-"), box.width, QStringLiteral("A moving face cannot reach its fixed face"));
    expectInvalid(box, QStringLiteral("Y+"), -4., QStringLiteral("A moving face cannot cross its fixed face"));

    BuildingGeometryRequest snapped;
    check(GeometryEditService::moveHandle(box, QStringLiteral("X+"), 0.26, 0.1, &snapped) && almostEqual(snapped.width, 4.3),
          QStringLiteral("Positive drag snaps to the nearest physical increment"));
    check(GeometryEditService::moveHandle(box, QStringLiteral("X+"), -0.26, 0.1, &snapped) && almostEqual(snapped.width, 3.7),
          QStringLiteral("Negative drag snaps symmetrically"));
    check(GeometryEditService::moveHandle(box, QStringLiteral("X+"), 0.26, 0., &snapped) && almostEqual(snapped.width, 4.26),
          QStringLiteral("Zero snap preserves precise numeric input"));
    check(!GeometryEditService::moveHandle(box, QStringLiteral("X+"), 1., -0.1, &snapped), QStringLiteral("Negative snap is rejected"));
    expectInvalid(box, QStringLiteral("X+"), std::numeric_limits<double>::quiet_NaN(), QStringLiteral("Non-finite drag is rejected atomically"));
    expectInvalid(box, QStringLiteral("Not a handle"), 1., QStringLiteral("Unsupported handle is rejected atomically"));

    BuildingGeometryRequest wall;
    wall.kind = FcGeometryKind::Wall;
    wall.x = 1.; wall.y = 2.; wall.z = 0.2;
    wall.endX = 4.; wall.endY = 6.; wall.height = 3.; wall.thickness = 0.4;
    for (const auto baseline : {FcWallBaseline::Left, FcWallBaseline::Center, FcWallBaseline::Right}) {
        wall.baseline = baseline;
        const auto widened = checkFixedFace(wall, QStringLiteral("Thickness+"), 0.2, false);
        check(almostEqual(widened.thickness, 0.6), QStringLiteral("Wall thickness metadata matches the changed solid"));
        checkFixedFace(wall, QStringLiteral("Thickness-"), -0.2, true);
    }
    checkFixedFace(wall, QStringLiteral("Start"), -1., true);
    checkFixedFace(wall, QStringLiteral("End"), 1., false);
    checkFixedFace(wall, QStringLiteral("Z-"), -0.5, true);
    checkFixedFace(wall, QStringLiteral("Z+"), 0.5, false);
    expectInvalid(wall, QStringLiteral("Start"), 6., QStringLiteral("Start endpoint cannot flip past the fixed end"));
    expectInvalid(wall, QStringLiteral("End"), -6., QStringLiteral("End endpoint cannot flip past the fixed start"));
    expectInvalid(wall, QStringLiteral("Thickness+"), -0.5, QStringLiteral("Wall thickness cannot become negative"));
    wall.path = {{0., 0.}, {1., 0.}, {1., 1.}};
    check(GeometryEditService::handles(wall).isEmpty(), QStringLiteral("Path walls do not expose unsupported regular-wall handles"));

    BuildingGeometryRequest slab = box;
    slab.kind = FcGeometryKind::Slab; slab.height = slab.thickness = 0.3;
    const auto thicker = checkFixedFace(slab, QStringLiteral("Z+"), 0.2, false);
    check(almostEqual(thicker.height, 0.5) && almostEqual(thicker.thickness, 0.5), QStringLiteral("Slab height and thickness stay synchronized"));
    const auto lowered = checkFixedFace(slab, QStringLiteral("Z-"), -0.1, true);
    check(almostEqual(lowered.height, 0.4) && almostEqual(lowered.thickness, 0.4), QStringLiteral("Lower slab face keeps upper elevation fixed"));

    BuildingGeometryRequest polygon;
    polygon.kind = FcGeometryKind::PolygonPrism; polygon.z = 1.; polygon.height = 2.;
    polygon.profile = {{0., 0.}, {3., 0.}, {3., 2.}, {0., 2.}};
    BuildingGeometryRequest editedPolygon;
    check(GeometryEditService::moveHandle(polygon, QStringLiteral("Point:1:U"), 0.4, 0., &editedPolygon),
          QStringLiteral("Legacy XY polygon vertex can be edited"));
    check(editedPolygon.profile3d.size() == 4 && almostEqual(editedPolygon.profile3d[1][0], 3.4) &&
          almostEqual(editedPolygon.profile3d[1][2], 1.) && almostEqual(editedPolygon.extrusionDistance, 2.) &&
          !editedPolygon.extrusionNormal && almostEqual(editedPolygon.extrusionDirection[2], 1.),
          QStringLiteral("Legacy edit retains base elevation and extrusion when upgrading to 3D points"));
    const auto tallerPolygon = checkFixedFace(polygon, QStringLiteral("Extrusion"), 0.6, false);
    check(almostEqual(tallerPolygon.height, 2.6) && tallerPolygon.profile3d.isEmpty(), QStringLiteral("Legacy extrusion edits preserve legacy parameter form"));
    expectInvalid(polygon, QStringLiteral("Point:1:U"), -3., QStringLiteral("A point edit cannot introduce duplicate vertices"));
    expectInvalid(polygon, QStringLiteral("Point:1:U"), -4., QStringLiteral("A point edit cannot create a self-intersecting outline"));

    BuildingGeometryRequest tilted;
    tilted.kind = FcGeometryKind::ProfileExtrusion;
    tilted.profile3d = {{0., 0., 0.}, {2., 0., 2.}, {2., 3., 2.}, {0., 3., 0.}};
    tilted.extrusionDistance = 1.2;
    for (const QString& key : {QStringLiteral("Point:2:U"), QStringLiteral("Point:2:V")}) {
        BuildingGeometryRequest edited;
        check(GeometryEditService::moveHandle(tilted, key, 0.3, 0., &edited), QStringLiteral("Tilted profile supports %1").arg(key));
        bool planar = edited.profile3d.size() == 4;
        for (const auto& point : edited.profile3d) planar &= almostEqual(point[0], point[2]);
        check(planar, QStringLiteral("%1 stays in the tilted work plane").arg(key));
        check(almostEqual(dot(handle(tilted, key).direction, handle(tilted, QStringLiteral("Extrusion")).direction), 0.),
              QStringLiteral("%1 movement is perpendicular to the profile normal").arg(key));
    }
    const auto tiltedLonger = checkFixedFace(tilted, QStringLiteral("Extrusion"), 0.5, false);
    check(almostEqual(tiltedLonger.extrusionDistance, 1.7), QStringLiteral("Tilted normal extrusion updates exact distance"));
    tilted.extrusionNormal = false; tilted.extrusionDirection = {0., 0., 2.};
    BuildingGeometryRequest oblique;
    check(GeometryEditService::moveHandle(tilted, QStringLiteral("Extrusion"), 0.5, 0., &oblique) &&
          almostEqual(oblique.extrusionDistance, 1.7) && oblique.extrusionDirection == tilted.extrusionDirection,
          QStringLiteral("Custom extrusion direction is retained while distance changes"));
    const auto oldAlongZ = support(BuildingGeometryService::createShape(tilted), {0., 0., 1.});
    const auto newAlongZ = support(BuildingGeometryService::createShape(oblique), {0., 0., 1.});
    check(almostEqual(oldAlongZ.first, newAlongZ.first) && almostEqual(newAlongZ.second-oldAlongZ.second, 0.5),
          QStringLiteral("Custom direction is normalized for physical extrusion distance"));
    auto invalidProfile = tilted;
    invalidProfile.profile3d[2][2] += 0.2;
    expectInvalid(invalidProfile, QStringLiteral("Extrusion"), 0., QStringLiteral("Non-planar profile is rejected"));
    invalidProfile = tilted; invalidProfile.extrusionDirection = {1., 0., 1.};
    expectInvalid(invalidProfile, QStringLiteral("Extrusion"), 0., QStringLiteral("In-plane extrusion direction is rejected"));
    invalidProfile = tilted; invalidProfile.extrusionDirection = {0., 0., 0.};
    expectInvalid(invalidProfile, QStringLiteral("Extrusion"), 0., QStringLiteral("Zero extrusion direction is rejected"));
    expectInvalid(tilted, QStringLiteral("Extrusion"), -2., QStringLiteral("Extrusion cannot reverse through the base profile"));

    tilted.extraParameters = {{QStringLiteral("displayColor"), QStringLiteral("#45a2d1")},
        {QStringLiteral("displayOutline"), true}, {QStringLiteral("description"), QStringLiteral("倾斜轮廓")},
        {QStringLiteral("sourceIfcGlobalId"), QStringLiteral("source-guid")}, {QStringLiteral("width"), 999.}};
    BuildingGeometryRequest withMetadata;
    check(GeometryEditService::moveHandle(tilted, QStringLiteral("Point:2:V"), 0.25, 0., &withMetadata), QStringLiteral("Edit with metadata succeeds"));
    const auto parameters = BuildingGeometryService::requestToParameters(withMetadata);
    check(parameters.value(QStringLiteral("displayColor")) == tilted.extraParameters.value(QStringLiteral("displayColor")) &&
          parameters.value(QStringLiteral("sourceIfcGlobalId")) == tilted.extraParameters.value(QStringLiteral("sourceIfcGlobalId")) &&
          almostEqual(parameters.value(QStringLiteral("width")).toDouble(), tilted.width),
          QStringLiteral("Unknown metadata survives while authoritative geometry overrides stale metadata"));
    const auto decoded = BuildingGeometryService::requestFromParameters(withMetadata.kind, parameters);
    check(BuildingGeometryService::requestToParameters(decoded) == parameters, QStringLiteral("3D profile and custom direction parameters round-trip exactly"));
    FcProject project(QStringLiteral("Geometry edit regression"));
    auto editedObject = geometry(QStringLiteral("倾斜轮廓"), withMetadata);
    project.document()->geometryGroup()->addChild(editedObject);
    QTemporaryDir temporary;
    const QString file = temporary.filePath(QStringLiteral("geometry-edit.fcproj"));
    QString error;
    check(FcProjectSerializer::save(project, file, &error), QStringLiteral("Edited geometry project saves"));
    const auto loaded = FcProjectSerializer::load(file);
    const auto restored = loaded.success() ? std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(editedObject->id())) : nullptr;
    check(restored && restored->geometryParameters() == parameters && restored->geometryKind() == withMetadata.kind,
          QStringLiteral("UUID, geometry parameters, source metadata and display settings survive reopen"));
    if (restored) {
        GProp_GProps beforeMass, afterMass;
        BRepGProp::VolumeProperties(editedObject->shape(), beforeMass);
        BRepGProp::VolumeProperties(restored->shape(), afterMass);
        check(almostEqual(beforeMass.Mass(), afterMass.Mass()), QStringLiteral("Saved BREP matches the edited parametric solid"));
    }

    BuildingGeometryRequest physical;
    physical.width = 1.; physical.depth = 1.; physical.height = 1.;
    const QVariantMap approved{{QStringLiteral("BNDF_OBST"), QStringLiteral(".TRUE.")},
        {QStringLiteral("THICKEN"), QStringLiteral(".FALSE.")}, {QStringLiteral("PERMIT_HOLE"), QStringLiteral(".TRUE.")},
        {QStringLiteral("ALLOW_VENT"), QStringLiteral(".FALSE.")}, {QStringLiteral("REMOVABLE"), QStringLiteral(".TRUE.")},
        {QStringLiteral("BULK_DENSITY"), QStringLiteral("650")}};
    physical.extraParameters.insert(QStringLiteral("fdsAdditionalFields"), approved);
    auto physicalObject = geometry(QStringLiteral("Physical box"), physical);
    const auto mesh = std::make_shared<FcFdsMesh>(QStringLiteral("Domain"), QStringLiteral("MESH"),
        std::array<int, 3>{20,20,20}, FcFdsBounds{-1., 3., -1., 3., -1., 3.});
    const auto visible = FdsBlockConversionService::convert({physicalObject}, {mesh});
    check(visible.success() && visible.fdsObjects.size() == 1, QStringLiteral("Approved advanced OBST fields convert"));
    physicalObject->setVisible(false);
    const auto hidden = FdsBlockConversionService::convert({physicalObject}, {mesh});
    check(hidden.success() && convertedSignature(visible) == convertedSignature(hidden),
          QStringLiteral("Hidden geometry converts identically to visible geometry"));
    if (!visible.fdsObjects.isEmpty()) {
        const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(visible.fdsObjects.first());
        bool fieldsMatch = record != nullptr;
        QSet<QString> keys;
        if (record) {
            for (const auto& field : record->parameters()) {
                fieldsMatch &= !keys.contains(field.key); keys.insert(field.key);
            }
            for (auto it = approved.cbegin(); it != approved.cend(); ++it)
                fieldsMatch &= record->parameterValue(it.key()) == it.value().toString();
        }
        check(fieldsMatch, QStringLiteral("Each approved field exports once with its validated value"));
        FcProject exportProject(QStringLiteral("Validated advanced fields"));
        exportProject.document()->meshesGroup()->addChild(mesh);
        exportProject.document()->geometryGroup()->addChild(visible.fdsObjects.first());
        const auto exported = FdsWriter::render(exportProject);
        check(exported.success() && exported.text.contains(QStringLiteral("BNDF_OBST=.TRUE.")) &&
              exported.text.contains(QStringLiteral("BULK_DENSITY=650")), QStringLiteral("Advanced fields pass the actual FDS writer"));
    }
    for (const QVariantMap& invalid : {QVariantMap{{QStringLiteral("SURF_ID"), QStringLiteral("'override'")}},
                                      QVariantMap{{QStringLiteral("REMOVABLE"), QStringLiteral("TRUE")}},
                                      QVariantMap{{QStringLiteral("BULK_DENSITY"), QStringLiteral("-1")}},
                                      QVariantMap{{QStringLiteral("BNDF_OBST"), QStringLiteral(".TRUE.,XB=0,1,0,1,0,1")}}}) {
        auto invalidParameters = physicalObject->geometryParameters();
        invalidParameters.insert(QStringLiteral("fdsAdditionalFields"), invalid);
        physicalObject->setGeometryParameters(invalidParameters);
        const auto rejected = FdsBlockConversionService::convert({physicalObject}, {mesh});
        check(!rejected.success() && rejected.fdsObjects.isEmpty(), QStringLiteral("Invalid/conflicting advanced field is rejected: %1").arg(invalid.firstKey()));
    }
    physical.fdsConversionRoute = QStringLiteral("GEOM");
    physicalObject = geometry(QStringLiteral("GEOM with OBST-only fields"), physical);
    check(!FdsBlockConversionService::convert({physicalObject}, {mesh}).success(), QStringLiteral("OBST-only flags cannot silently enter a GEOM record"));

    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(7., -3., 2.));
    const auto movedBox = checkTransform(box, translation, QStringLiteral("X+"), QStringLiteral("Box move"));
    check(almostEqual(movedBox.x, box.x + 7.) && almostEqual(movedBox.y, box.y - 3.) && almostEqual(movedBox.z, box.z + 2.),
          QStringLiteral("Translation updates the canonical geometry origin"));
    gp_Trsf scaling;
    const gp_Pnt pivot(10., -3., 2.);
    scaling.SetScale(pivot, 2.5);
    const auto scaledBox = checkTransform(box, scaling, QStringLiteral("Y+"), QStringLiteral("Box uniform scale about nonzero pivot"));
    check(almostEqual(scaledBox.x, pivot.X() + 2.5 * (box.x - pivot.X())) &&
          almostEqual(scaledBox.width, box.width * 2.5) && almostEqual(scaledBox.height, box.height * 2.5),
          QStringLiteral("Uniform scale changes dimensions and pivot-relative position together"));
    gp_Trsf zRotation;
    zRotation.SetRotation(gp_Ax1(gp_Pnt(2., 1., 0.), gp_Dir(0., 0., 1.)), 53. * std::acos(-1.) / 180.);
    const auto rotatedBox = checkTransform(box, zRotation, QStringLiteral("X+"), QStringLiteral("Box Z rotation about nonzero pivot"));
    check(almostEqual(rotatedBox.rotationDegrees, 90.), QStringLiteral("Z rotation composes with the original box orientation"));
    gp_Trsf combined = scaling;
    combined.PreMultiply(zRotation); combined.PreMultiply(translation);
    checkTransform(slab, combined, QStringLiteral("Z+"), QStringLiteral("Slab combined move, scale and Z rotation"));
    BuildingGeometryRequest transformWall;
    transformWall.kind = FcGeometryKind::Wall; transformWall.x = 1.; transformWall.y = 2.;
    transformWall.endX = 4.; transformWall.endY = 6.; transformWall.z = 0.5;
    transformWall.thickness = 0.4;
    for (const auto baseline : {FcWallBaseline::Left, FcWallBaseline::Center, FcWallBaseline::Right}) {
        transformWall.baseline = baseline;
        const auto transformedWall = checkTransform(transformWall, combined, QStringLiteral("End"), QStringLiteral("Wall baseline transform"));
        check(almostEqual(transformedWall.thickness, 1.) && transformedWall.baseline == baseline,
              QStringLiteral("Wall scaling preserves its baseline convention and scales thickness"));
    }
    const auto movedLegacy = checkTransform(polygon, combined, QStringLiteral("Extrusion"), QStringLiteral("Legacy polygon Z-preserving transform"));
    check(movedLegacy.profile3d.isEmpty() && movedLegacy.profile.size() == polygon.profile.size(),
          QStringLiteral("Z-preserving transforms keep legacy XY profiles editable"));
    gp_Trsf xRotation;
    xRotation.SetRotation(gp_Ax1(gp_Pnt(1., -1., 2.), gp_Dir(1., 0., 0.)), 0.7);
    gp_Trsf spatial = xRotation;
    spatial.PreMultiply(scaling); spatial.PreMultiply(translation);
    const auto spatialLegacy = checkTransform(polygon, spatial, QStringLiteral("Extrusion"), QStringLiteral("Legacy polygon tilted transform"));
    check(spatialLegacy.profile.isEmpty() && spatialLegacy.profile3d.size() == 4 && !spatialLegacy.extrusionNormal &&
          almostEqual(spatialLegacy.extrusionDistance, polygon.height * 2.5),
          QStringLiteral("Tilted legacy polygon upgrades to actual 3D points and explicit extrusion direction"));
    checkTransform(tilted, spatial, QStringLiteral("Extrusion"), QStringLiteral("Spatial polygon with custom extrusion transform"));
    auto normalProfile = tilted; normalProfile.extrusionNormal = true;
    const auto spatialNormal = checkTransform(normalProfile, spatial, QStringLiteral("Extrusion"), QStringLiteral("Spatial polygon normal extrusion transform"));
    check(spatialNormal.extrusionNormal && spatialNormal.extraParameters == tilted.extraParameters,
          QStringLiteral("Normal extrusion and source/display metadata survive whole-object transformation"));

    rejectTransform(box, xRotation, QStringLiteral("Box tilt is rejected without stale parameter fallback"));
    rejectTransform(slab, xRotation, QStringLiteral("Slab tilt is rejected atomically"));
    rejectTransform(transformWall, xRotation, QStringLiteral("Wall tilt is rejected atomically"));
    rejectTransform(wall, translation, QStringLiteral("Path-wall transformation is not advertised as a regular-wall parameter update"));
    auto unsupported = box; unsupported.kind = FcGeometryKind::Generic;
    rejectTransform(unsupported, translation, QStringLiteral("Unsupported geometry transformation is rejected explicitly"));
    gp_Trsf negativeScale;
    negativeScale.SetScale(pivot, -1.);
    rejectTransform(box, negativeScale, QStringLiteral("Negative uniform scale is rejected"));
    rejectTransform(tilted, negativeScale, QStringLiteral("Spatial profiles reject handedness-changing scale"));
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(), gp_Dir(1., 0., 0.)));
    rejectTransform(box, mirror, QStringLiteral("Reflection is rejected instead of silently reversing geometry"));

    const auto originalBoxShape = BuildingGeometryService::createShape(box);
    const auto shiftedBoxShape = BRepBuilderAPI_Transform(originalBoxShape, translation, true).Shape();
    QString staleError;
    check(GeometryEditService::matchesShape(box, originalBoxShape), QStringLiteral("Consistent parametric shape passes stale-state guard"));
    check(!GeometryEditService::matchesShape(box, shiftedBoxShape, 1.0e-6, &staleError) && !staleError.isEmpty(),
          QStringLiteral("Previously shape-only translated geometry is detected before direct editing"));
    BuildingGeometryRequest cavityBox; cavityBox.width = cavityBox.depth = cavityBox.height = 2.;
    const auto hollow = BRepAlgoAPI_Cut(BuildingGeometryService::createShape(cavityBox),
        BRepPrimAPI_MakeBox(gp_Pnt(0.5, 0.5, 0.5), 1., 1., 1.).Shape()).Shape();
    check(!GeometryEditService::matchesShape(cavityBox, hollow), QStringLiteral("Same bounding box with different physical volume fails the guard"));
    BuildingGeometryRequest symmetric;
    symmetric.width = symmetric.depth = 2.; symmetric.height = 1.; symmetric.rotationDegrees = 30.;
    symmetric.x = -(std::cos(std::acos(-1.)/6.) - std::sin(std::acos(-1.)/6.));
    symmetric.y = -(std::sin(std::acos(-1.)/6.) + std::cos(std::acos(-1.)/6.));
    gp_Trsf oppositeOrientation;
    oppositeOrientation.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0.,0.,1.)), -std::acos(-1.)/3.);
    const auto differentFaces = BRepBuilderAPI_Transform(BuildingGeometryService::createShape(symmetric), oppositeOrientation, true).Shape();
    check(!GeometryEditService::matchesShape(symmetric, differentFaces),
          QStringLiteral("Different face locations fail even when bounds, volume and center remain equal"));
    return failures == 0 ? 0 : 1;
}
