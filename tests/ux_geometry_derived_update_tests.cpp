#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsWriter.h"
#include "fds/GeometryDerivedUpdateService.h"
#include "geometry/FcGeometryObject.h"
#include "modeling/BuildingGeometryService.h"
#include "modeling/GeometryEditService.h"
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Vec.hxx>
#include <QCoreApplication>
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
QString signature(const std::vector<FcFdsParameter>& parameters)
{
    QStringList fields;
    for (const auto& field : parameters)
        fields.append(field.key + QLatin1Char('=') + field.value + QString::number(static_cast<int>(field.kind)) +
                      field.targetObjectIds.join(QLatin1Char(',')));
    return fields.join(QLatin1Char(';'));
}
std::shared_ptr<FcGeometryObject> makeGeometry(const BuildingGeometryRequest& request,
                                             const QString& id = {})
{
    const auto shape = BuildingGeometryService::createShape(request);
    check(!shape.IsNull(), "Source shape can be created");
    auto object = std::make_shared<FcGeometryObject>(QStringLiteral("Source geometry"), shape);
    object->setGeometryKind(request.kind);
    object->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    if (!id.isEmpty()) check(object->restorePersistentId(id), "Preview retains source UUID");
    return object;
}
struct Fixture
{
    std::unique_ptr<FcProject> project;
    std::shared_ptr<FcGeometryObject> source;
    QVector<std::shared_ptr<FcFdsMesh>> meshes;
    QVector<std::shared_ptr<FcFdsNamelist>> derived;
};
Fixture fixture(const BuildingGeometryRequest& request, bool convert = true)
{
    Fixture result;
    result.project = std::make_unique<FcProject>(QStringLiteral("Derived update regression"));
    result.source = makeGeometry(request);
    result.project->document()->geometryGroup()->addChild(result.source);
    auto mesh = std::make_shared<FcFdsMesh>(QStringLiteral("Domain"), QStringLiteral("DOMAIN"),
        std::array<int,3>{50,50,50}, FcFdsBounds{-1.,9.,-1.,9.,-1.,9.});
    result.project->document()->meshesGroup()->addChild(mesh);
    result.meshes.append(mesh);
    if (convert) {
        const auto converted = FdsBlockConversionService::convert({result.source}, result.meshes);
        check(converted.success() && !converted.fdsObjects.isEmpty(), "Source has existing converted FDS records");
        for (const auto& item : converted.fdsObjects) {
            const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(item);
            check(record != nullptr, "Conversion record is a namelist");
            if (record) {
                result.derived.append(record);
                result.project->document()->geometryGroup()->addChild(record);
            }
        }
    }
    return result;
}
void checkBlocked(const GeometryDerivedUpdatePlan& plan, const char* message)
{
    check(!plan.success() && !plan.errors.isEmpty() && plan.updates.isEmpty(), message);
}
bool sameBounds(const FcFdsBounds& a, const FcFdsBounds& b)
{
    return std::abs(a.xMin-b.xMin) < 1.0e-11 && std::abs(a.xMax-b.xMax) < 1.0e-11 &&
           std::abs(a.yMin-b.yMin) < 1.0e-11 && std::abs(a.yMax-b.yMax) < 1.0e-11 &&
           std::abs(a.zMin-b.zMin) < 1.0e-11 && std::abs(a.zMax-b.zMax) < 1.0e-11;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    BuildingGeometryRequest before;
    before.width = before.depth = before.height = 1.;
    BuildingGeometryRequest after;
    check(GeometryEditService::moveHandle(before, QStringLiteral("X+"), 0.6, 0., &after), "Box edit preview succeeds");
    auto sample = fixture(before);
    const auto preview = makeGeometry(after, sample.source->id());
    preview->setName(QStringLiteral("Renamed geometry"));
    check(!sample.derived.isEmpty(), "Derived test object is available");
    if (sample.derived.isEmpty()) return 1;
    const auto target = sample.derived.first();
    target->setName(QStringLiteral("User's derived label"));
    target->setFdsId(QStringLiteral("USER_OBSTRUCTION"));
    const QString targetId = target->id();
    const auto originalParameters = target->parameters();
    const QString originalSignature = signature(originalParameters);
    const auto oldExport = FdsWriter::render(*sample.project);
    const auto plan = GeometryDerivedUpdateService::plan(*sample.project->document(), sample.source, preview, sample.meshes);
    check(plan.success() && plan.updates.size() == 1 && plan.updates.first().target == target,
          "Renaming source or derived record does not break UUID-based update planning");
    check(signature(target->parameters()) == originalSignature &&
          sample.source->geometryParameters() == BuildingGeometryService::requestToParameters(before),
          "Planning does not change FDS records or source geometry");
    if (!plan.success() || plan.updates.isEmpty()) return 1;
    check(signature(plan.updates.first().beforeParameters) == originalSignature &&
          signature(plan.updates.first().afterParameters) != originalSignature,
          "Plan contains exact undo state and changed derived geometry");
    for (const auto& update : plan.updates) update.target->setParameters(update.afterParameters);
    const auto newExport = FdsWriter::render(*sample.project);
    check(oldExport.success() && newExport.success() && oldExport.text != newExport.text &&
          newExport.text.contains(QStringLiteral("USER_OBSTRUCTION")),
          "Applying a plan changes exported XB while preserving the user's FDS ID");
    check(target->id() == targetId && target->fdsId() == QStringLiteral("USER_OBSTRUCTION") &&
          target->name() == QStringLiteral("User's derived label"), "Applying a plan preserves UUID, FDS ID and user name");
    for (const auto& update : plan.updates) update.target->setParameters(update.beforeParameters);
    check(signature(target->parameters()) == originalSignature && FdsWriter::render(*sample.project).text == oldExport.text,
          "Undo restores the exact original FDS export");
    for (const auto& update : plan.updates) update.target->setParameters(update.afterParameters);
    sample.source->setShape(preview->shape());
    sample.source->setGeometryParameters(preview->geometryParameters());
    QTemporaryDir temporary;
    const QString savedPath = temporary.filePath(QStringLiteral("derived-update.fcproj"));
    QString error;
    check(FcProjectSerializer::save(*sample.project, savedPath, &error), "Updated source and derived records save together");
    const auto loaded = FcProjectSerializer::load(savedPath);
    const auto loadedTarget = loaded.success() ? std::dynamic_pointer_cast<FcFdsNamelist>(loaded.project->document()->findObject(targetId)) : nullptr;
    check(loadedTarget && loadedTarget->fdsId() == target->fdsId() &&
          signature(loadedTarget->parameters()) == signature(target->parameters()) &&
          FdsWriter::render(*loaded.project).text == newExport.text,
          "Reopen retains derived target identity, changed XB and identical export");

    auto unconverted = fixture(before, false);
    auto unconvertedAfter = makeGeometry(after, unconverted.source->id());
    const auto noConversion = GeometryDerivedUpdateService::plan(*unconverted.project->document(), unconverted.source, unconvertedAfter, unconverted.meshes);
    check(noConversion.success() && noConversion.updates.isEmpty(), "Editing unconverted geometry never creates physical FDS records implicitly");
    checkBlocked(GeometryDerivedUpdateService::plan(*unconverted.project->document(), unconverted.source, preview, unconverted.meshes),
                 "Mismatched preview UUID is rejected");
    FcProject emptyProject(QStringLiteral("Empty"));
    checkBlocked(GeometryDerivedUpdateService::plan(*emptyProject.document(), unconverted.source, unconvertedAfter, unconverted.meshes),
                 "Geometry absent from the document is rejected");

    auto manual = fixture(before);
    auto manualAfter = makeGeometry(after, manual.source->id());
    manual.derived.first()->addStringParameter(QStringLiteral("COLOR"), QStringLiteral("RED"));
    const auto manualParameters = signature(manual.derived.first()->parameters());
    checkBlocked(GeometryDerivedUpdateService::plan(*manual.project->document(), manual.source, manualAfter, manual.meshes),
                 "Manually edited automatic FDS record blocks the geometry edit");
    check(signature(manual.derived.first()->parameters()) == manualParameters, "Manual physical fields are never overwritten by planning");
    const auto regenerated = FdsBlockConversionService::convert({manual.source}, manual.meshes);
    if (!regenerated.fdsObjects.isEmpty()) manual.derived.first()->setParameters(
        std::dynamic_pointer_cast<FcFdsNamelist>(regenerated.fdsObjects.first())->parameters());
    manual.derived.first()->setLocked(true);
    checkBlocked(GeometryDerivedUpdateService::plan(*manual.project->document(), manual.source, manualAfter, manual.meshes),
                 "Locked linked FDS record blocks updates");
    manual.derived.first()->setLocked(false);
    QStringList tags = manual.derived.first()->tags();
    tags.removeAll(QStringLiteral("firecae:auto-converted"));
    manual.derived.first()->setTags(tags);
    checkBlocked(GeometryDerivedUpdateService::plan(*manual.project->document(), manual.source, manualAfter, manual.meshes),
                 "Removing the automatic tag does not silently orphan a source-UUID dependency");

    auto route = fixture(before);
    auto changedRoute = after; changedRoute.fdsConversionRoute = QStringLiteral("GEOM");
    checkBlocked(GeometryDerivedUpdateService::plan(*route.project->document(), route.source,
        makeGeometry(changedRoute, route.source->id()), route.meshes), "Changing the derived record keyword requires explicit reconversion");
    changedRoute.fdsConversionRoute = QStringLiteral("REFERENCE");
    checkBlocked(GeometryDerivedUpdateService::plan(*route.project->document(), route.source,
        makeGeometry(changedRoute, route.source->id()), route.meshes), "Removing physical conversion requires explicit reconversion");

    BuildingGeometryRequest stairs;
    stairs.kind = FcGeometryKind::Stair; stairs.stepCount = 3; stairs.width = 1.; stairs.depth = 3.; stairs.rise = 3.;
    auto multiple = fixture(stairs);
    auto changedStairs = stairs; changedStairs.rise = 3.6;
    const auto multiPlan = GeometryDerivedUpdateService::plan(*multiple.project->document(), multiple.source,
        makeGeometry(changedStairs, multiple.source->id()), multiple.meshes);
    check(multiPlan.success() && multiPlan.updates.size() == 3, "Multi-record source updates each existing physical piece");
    if (multiPlan.success()) {
        bool sameOrder = true;
        for (qsizetype i = 0; i < multiPlan.updates.size(); ++i)
            sameOrder &= multiPlan.updates[i].target == multiple.derived[i];
        check(sameOrder, "Piece correspondence retains original target identities");
    }
    changedStairs.stepCount = 4;
    checkBlocked(GeometryDerivedUpdateService::plan(*multiple.project->document(), multiple.source,
        makeGeometry(changedStairs, multiple.source->id()), multiple.meshes), "Changing the number of physical pieces is rejected atomically");
    if (multiple.derived.size() > 1) {
        multiple.derived.last()->addRawParameter(QStringLiteral("BULK_DENSITY"), QStringLiteral("1000"));
        changedStairs.stepCount = 3;
        const auto lateFailure = GeometryDerivedUpdateService::plan(*multiple.project->document(), multiple.source,
            makeGeometry(changedStairs, multiple.source->id()), multiple.meshes);
        checkBlocked(lateFailure, "A late manually changed piece clears every earlier planned update");
    }

    auto referenced = fixture(before, false);
    auto control = std::make_shared<FcFdsNamelist>(QStringLiteral("Control"), FcObjectType::Control,
                                                 QStringLiteral("CTRL"), QStringLiteral("CTRL_1"));
    auto surface = std::make_shared<FcFdsSurface>(QStringLiteral("Surface"), QStringLiteral("SURFACE_1"), 0.);
    referenced.project->document()->controlsGroup()->addChild(control);
    referenced.project->document()->surfacesGroup()->addChild(surface);
    referenced.source->setControlObjectId(control->id()); referenced.source->setDefaultSurfaceId(surface->id());
    const auto referenceConversion = FdsBlockConversionService::convert({referenced.source}, referenced.meshes);
    for (const auto& record : referenceConversion.fdsObjects) referenced.project->document()->geometryGroup()->addChild(record);
    const auto referenceAfter = makeGeometry(after, referenced.source->id());
    referenceAfter->setControlObjectId(control->id()); referenceAfter->setDefaultSurfaceId(surface->id());
    const auto referencePlan = GeometryDerivedUpdateService::plan(*referenced.project->document(), referenced.source,
        referenceAfter, referenced.meshes);
    bool preservesReferences = referencePlan.success() && referencePlan.updates.size() == 1;
    bool foundControl = false, foundSurface = false;
    if (preservesReferences) for (const auto& field : referencePlan.updates.first().afterParameters) {
        if (field.key == QStringLiteral("CTRL_ID")) foundControl = field.targetObjectIds == QStringList{control->id()};
        if (field.key == QStringLiteral("SURF_ID6")) foundSurface = field.targetObjectIds == QStringList(6, surface->id());
    }
    check(preservesReferences && foundControl && foundSurface, "Geometry updates preserve control and surface UUID references");

    BuildingGeometryRequest alignedWall;
    alignedWall.kind = FcGeometryKind::Wall;
    alignedWall.x = 0.8; alignedWall.endX = 2.8;
    alignedWall.y = alignedWall.endY = 0.5;
    alignedWall.height = 2.2; alignedWall.thickness = 0.2;
    auto aligned = makeGeometry(alignedWall);
    const auto fineMesh = std::make_shared<FcFdsMesh>(QStringLiteral("Fine grid"), QStringLiteral("FINE_GRID"),
        std::array<int,3>{36,24,24}, FcFdsBounds{0.,3.6,0.,2.4,0.,2.4});
    const FcFdsBounds exactBounds{0.8,2.8,0.4,0.6,0.,2.2};
    bool boundsValid = false;
    check(sameBounds(FdsBlockConversionService::boundsForShape(*aligned, &boundsValid), exactBounds) && boundsValid,
          "Aligned wall bounds exclude BREP tolerance padding");
    const auto preciseConversion = FdsBlockConversionService::convert({aligned}, {fineMesh});
    check(preciseConversion.success() && preciseConversion.previews.size() == 1 &&
          sameBounds(preciseConversion.previews.first().requested, exactBounds) &&
          sameBounds(preciseConversion.previews.first().actual, exactBounds),
          "An exactly aligned 0.1 m wall boundary does not gain an extra FDS cell layer");
    if (!preciseConversion.fdsObjects.isEmpty()) {
        const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(preciseConversion.fdsObjects.first());
        check(record && record->parameterValue(QStringLiteral("XB")) == QStringLiteral("0.8,2.8,0.4,0.6,0,2.2"),
              "Aligned wall exports its exact intended XB through production conversion");
    }
    BRepMesh_IncrementalMesh mesher(aligned->shape(), 0.05);
    BRep_Builder builder;
    for (TopExp_Explorer faces(aligned->shape(), TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face face = TopoDS::Face(faces.Current());
        TopLoc_Location location;
        const auto triangles = BRep_Tool::Triangulation(face, location);
        if (!triangles.IsNull()) triangles->Deflection(0.05);
        builder.UpdateFace(face, 0.001);
    }
    const auto displayMeshed = FdsBlockConversionService::convert({aligned}, {fineMesh});
    check(sameBounds(FdsBlockConversionService::boundsForShape(*aligned), exactBounds) &&
          displayMeshed.success() && displayMeshed.fdsObjects.size() == 1 &&
          std::dynamic_pointer_cast<FcFdsNamelist>(displayMeshed.fdsObjects.first())->parameterValue(QStringLiteral("XB")) ==
              QStringLiteral("0.8,2.8,0.4,0.6,0,2.2"),
          "Display triangulation deflection and face tolerances cannot inflate solver bounds");

    const FcFdsBounds genuinelyOffGrid{0.8-1.0e-5, 2.8+1.0e-5, 0.4-1.0e-5, 0.6+1.0e-5, -1.0e-5, 2.2+1.0e-5};
    check(sameBounds(FdsBlockConversionService::snapBounds(genuinelyOffGrid, *fineMesh),
                     FcFdsBounds{0.7,2.9,0.3,0.7,-0.1,2.3}),
          "Genuinely off-grid bounds still use outward enclosing cells");
    const FcFdsBounds roundoffOnly{0.8-1.0e-14,2.8+1.0e-14,0.4,0.6,0.,2.2};
    check(sameBounds(FdsBlockConversionService::snapBounds(roundoffOnly, *fineMesh), exactBounds),
          "Floating-point roundoff on exact grid planes remains snapped to those planes");
    FcFdsMesh shiftedMesh(QStringLiteral("Shifted grid"), QStringLiteral("SHIFTED"),
        std::array<int,3>{40,40,40}, FcFdsBounds{-1.,3.,-1.,3.,-1.,3.});
    check(sameBounds(FdsBlockConversionService::snapBounds(exactBounds, shiftedMesh), exactBounds),
          "Exact boundaries also remain exact on a grid with negative origin");

    Handle(Poly_Triangulation) triangleMesh = new Poly_Triangulation(3,1,false);
    triangleMesh->SetNode(1, gp_Pnt(0.,0.,0.)); triangleMesh->SetNode(2, gp_Pnt(2.,0.2,0.));
    triangleMesh->SetNode(3, gp_Pnt(0.,0.,2.2)); triangleMesh->SetTriangle(1, Poly_Triangle(1,2,3));
    triangleMesh->Deflection(0.2);
    TopoDS_Face triangleFace;
    builder.MakeFace(triangleFace, triangleMesh);
    gp_Trsf meshPlacement; meshPlacement.SetTranslation(gp_Vec(0.8,0.4,0.));
    FcGeometryObject meshOnly(QStringLiteral("Located IFC-style mesh"), triangleFace.Moved(TopLoc_Location(meshPlacement)));
    check(sameBounds(FdsBlockConversionService::boundsForShape(meshOnly, &boundsValid), exactBounds) && boundsValid,
          "Pure triangulated IFC-style faces retain exact located bounds without deflection padding");

    auto legacy = fixture(alignedWall);
    auto legacyParameters = legacy.derived.first()->parameters();
    for (auto& parameter : legacyParameters)
        if (parameter.key == QStringLiteral("XB")) parameter.value = QStringLiteral("0.7,2.9,0.3,0.7,-0.1,2.3");
    legacy.derived.first()->setParameters(legacyParameters);
    auto wallAfter = alignedWall; wallAfter.height = 2.4;
    checkBlocked(GeometryDerivedUpdateService::plan(*legacy.project->document(), legacy.source,
        makeGeometry(wallAfter, legacy.source->id()), legacy.meshes),
        "Older padded derived FDS records are not silently overwritten; explicit reconversion is required");

    const QString defaultSurface = QStringLiteral("default-surface-uuid");
    const QString overrideSurface = QStringLiteral("override-surface-uuid");
    auto surfaceGeometry = makeGeometry(before);
    const auto topologyFaces = BuildingGeometryService::faceInfos(surfaceGeometry->shape());
    check(topologyFaces.size() == 6, "Surface fixture has six actual topology faces");
    if (topologyFaces.isEmpty()) return 1;
    const QString topologyFace = topologyFaces.first().key;
    const auto rejectedSurfaceConversion = [&]() {
        const auto result = FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true);
        return !FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty() &&
            !result.success() && result.fdsObjects.isEmpty() && result.previews.isEmpty();
    };
    surfaceGeometry->setFaceSurfaceIds({{QStringLiteral("X+"), overrideSurface}});
    check(rejectedSurfaceConversion(), "Partial OBST axis assignments without a default are rejected, not discarded");
    surfaceGeometry->setDefaultSurfaceId(defaultSurface);
    auto surfaceConversion = FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true);
    QStringList directionalReferences;
    if (!surfaceConversion.fdsObjects.isEmpty()) {
        const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(surfaceConversion.fdsObjects.first());
        for (const auto& parameter : record->parameters())
            if (parameter.key == QStringLiteral("SURF_ID6")) directionalReferences = parameter.targetObjectIds;
    }
    check(surfaceConversion.success() && directionalReferences == QStringList{defaultSurface, overrideSurface,
          defaultSurface, defaultSurface, defaultSurface, defaultSurface},
          "OBST axis override with an explicit default preserves all six surface references");
    surfaceGeometry->setFaceSurfaceIds({{topologyFace, overrideSurface}});
    check(rejectedSurfaceConversion(), "OBST rejects topology face assignments instead of silently ignoring them");
    surfaceGeometry->setFaceSurfaceIds({{QStringLiteral("not-a-face"), overrideSurface}});
    check(rejectedSurfaceConversion(), "OBST rejects unknown face keys");
    QMap<QString,QString> allAxisFaces;
    for (const QString& key : {QStringLiteral("X-"), QStringLiteral("X+"), QStringLiteral("Y-"),
                               QStringLiteral("Y+"), QStringLiteral("Z-"), QStringLiteral("Z+")})
        allAxisFaces.insert(key, overrideSurface);
    surfaceGeometry->setFaceSurfaceIds(allAxisFaces);
    surfaceGeometry->setDefaultSurfaceId({});
    check(FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty() &&
          FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true).success(),
          "Complete OBST axis assignments are valid without a default");

    auto surfaceParameters = surfaceGeometry->geometryParameters();
    surfaceParameters.insert(QStringLiteral("fdsConversionRoute"), QStringLiteral("GEOM"));
    surfaceGeometry->setGeometryParameters(surfaceParameters);
    check(rejectedSurfaceConversion(), "GEOM rejects axis surface assignments instead of silently ignoring them");
    surfaceGeometry->setFaceSurfaceIds({{topologyFace, overrideSurface}});
    check(rejectedSurfaceConversion(), "Partial GEOM assignments without a default cannot spread the first surface");
    surfaceGeometry->setDefaultSurfaceId(defaultSurface);
    const auto geomMappingIsExact = [&]() {
        const auto result = FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true);
        if (!result.success() || result.fdsObjects.size() != 1) return false;
        const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(result.fdsObjects.first());
        if (!record) return false;
        QStringList surfaces;
        for (const auto& parameter : record->parameters())
            if (parameter.key == QStringLiteral("SURF_ID")) surfaces = parameter.targetObjectIds;
        const auto faces = record->parameterValue(QStringLiteral("FACES")).split(QLatin1Char(','));
        const auto triangles = BuildingGeometryService::triangulateForFds(surfaceGeometry->shape());
        if (!triangles.valid() || faces.size() != triangles.triangles.size() * 4) return false;
        for (qsizetype index = 0; index < triangles.triangleFaceKeys.size(); ++index) {
            const QString expected = surfaceGeometry->faceSurfaceIds().value(
                triangles.triangleFaceKeys[index], surfaceGeometry->defaultSurfaceId());
            const int surfaceIndex = faces[index * 4 + 3].toInt() - 1;
            if (surfaceIndex < 0 || surfaceIndex >= surfaces.size() || surfaces[surfaceIndex] != expected) return false;
        }
        return true;
    };
    check(geomMappingIsExact(), "GEOM partial assignment with a default maps every triangle to its intended surface");
    surfaceGeometry->setFaceSurfaceIds({{QStringLiteral("TopoFace:missing-face"), overrideSurface}});
    check(rejectedSurfaceConversion(), "GEOM rejects a topology key absent from the actual shape");
    surfaceGeometry->setFaceSurfaceIds({{topologyFace, QString{}}});
    check(rejectedSurfaceConversion(), "Empty per-face surface references cannot override a valid default");
    QMap<QString,QString> allTopologyFaces;
    for (qsizetype index = 0; index < topologyFaces.size(); ++index)
        allTopologyFaces.insert(topologyFaces[index].key, index % 2 ? defaultSurface : overrideSurface);
    surfaceGeometry->setFaceSurfaceIds(allTopologyFaces);
    surfaceGeometry->setDefaultSurfaceId({});
    check(FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty() && geomMappingIsExact(),
          "Complete GEOM face assignments without a default preserve each triangle's own surface");
    surfaceGeometry->setFaceSurfaceIds({});
    surfaceGeometry->setDefaultSurfaceId(defaultSurface);
    check(geomMappingIsExact(), "A uniform default GEOM surface remains compatible");
    surfaceParameters.insert(QStringLiteral("fdsConversionRoute"), QStringLiteral("Auto"));
    surfaceGeometry->setGeometryParameters(surfaceParameters);
    check(FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty() &&
          FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true).success(),
          "A uniform default OBST surface remains compatible");
    surfaceGeometry->setGeometryKind(FcGeometryKind::RectangularOpening);
    check(rejectedSurfaceConversion(), "HOLE rejects default surface assignments");
    surfaceGeometry->setDefaultSurfaceId({});
    surfaceGeometry->setFaceSurfaceIds({{QStringLiteral("X+"), overrideSurface}});
    check(rejectedSurfaceConversion(), "HOLE rejects per-face surface assignments");
    surfaceGeometry->setFaceSurfaceIds({});
    check(FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty(),
          "An unassigned HOLE remains compatible");
    surfaceGeometry->setGeometryKind(FcGeometryKind::WallVent);
    surfaceGeometry->setDefaultSurfaceId(defaultSurface);
    check(FdsBlockConversionService::validateSurfaceAssignments(*surfaceGeometry).isEmpty() &&
          FdsBlockConversionService::convert({surfaceGeometry}, sample.meshes, true).success(),
          "Wall VENT retains its supported default surface");
    surfaceGeometry->setFaceSurfaceIds({{QStringLiteral("X+"), overrideSurface}});
    check(rejectedSurfaceConversion(), "Wall VENT rejects per-face surface assignments");
    return failures == 0 ? 0 : 1;
}
