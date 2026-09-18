#include "fds/FdsBlockConversionService.h"

#include "geometry/FcGeometryObject.h"
#include "modeling/BuildingGeometryService.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kEpsilon = 1.0e-9;

double volume(const FcFdsBounds& bounds)
{
    return std::max(0.0, bounds.xMax - bounds.xMin) *
           std::max(0.0, bounds.yMax - bounds.yMin) *
           std::max(0.0, bounds.zMax - bounds.zMin);
}

QString rawBounds(const FcFdsBounds& bounds)
{
    return QStringLiteral("%1,%2,%3,%4,%5,%6")
        .arg(bounds.xMin, 0, 'g', 15).arg(bounds.xMax, 0, 'g', 15)
        .arg(bounds.yMin, 0, 'g', 15).arg(bounds.yMax, 0, 'g', 15)
        .arg(bounds.zMin, 0, 'g', 15).arg(bounds.zMax, 0, 'g', 15);
}

QString rawRealArray(const QVector<double>& values)
{
    QStringList result;
    result.reserve(values.size());
    for (double value : values) result.append(QString::number(value, 'g', 15));
    return result.join(QLatin1Char(','));
}

QString rawIntegerArray(const QVector<int>& values)
{
    QStringList result;
    result.reserve(values.size());
    for (int value : values) result.append(QString::number(value));
    return result.join(QLatin1Char(','));
}

double snappedCoordinate(double value, double origin, double cell)
{
    return cell > kEpsilon
               ? origin + std::round((value - origin) / cell) * cell
               : value;
}

double enclosingCoordinate(double value, double origin, double cell, bool upper)
{
    if (cell <= kEpsilon) return value;
    const double scaled = (value - origin) / cell;
    const double nearest = std::round(scaled);
    if (std::abs(scaled - nearest) <= 1.0e-6) {
        return origin + nearest * cell;
    }
    return origin + (upper ? std::ceil(scaled) : std::floor(scaled)) * cell;
}

bool containsCenter(const FcFdsMesh& mesh, const FcFdsBounds& bounds)
{
    const FcFdsBounds& domain = mesh.bounds();
    const double x = (bounds.xMin + bounds.xMax) * 0.5;
    const double y = (bounds.yMin + bounds.yMax) * 0.5;
    const double z = (bounds.zMin + bounds.zMax) * 0.5;
    return x >= domain.xMin && x <= domain.xMax &&
           y >= domain.yMin && y <= domain.yMax &&
           z >= domain.zMin && z <= domain.zMax;
}

FdsBlockTarget targetFor(const FcGeometryObject& object, bool useGeomForArbitrary)
{
    const FcGeometryKind kind = object.geometryKind();
    if (kind == FcGeometryKind::WallVent) return FdsBlockTarget::Vent;
    if (BuildingGeometryService::isOpeningKind(kind)) return FdsBlockTarget::Hole;
    const QString requestedRoute = object.geometryParameters()
                                       .value(QStringLiteral("fdsConversionRoute"),
                                              QStringLiteral("Auto"))
                                       .toString().trimmed().toUpper();
    if (requestedRoute == QStringLiteral("GEOM")) return FdsBlockTarget::Geom;
    if (requestedRoute == QStringLiteral("OBST")) {
        return FdsBlockTarget::RasterizedObstruction;
    }
    if (kind == FcGeometryKind::PolygonPrism ||
        kind == FcGeometryKind::ProfileExtrusion ||
        kind == FcGeometryKind::PathSweep ||
        kind == FcGeometryKind::PolylineSweep ||
        kind == FcGeometryKind::Room || kind == FcGeometryKind::Stair) {
        return useGeomForArbitrary ? FdsBlockTarget::Geom
                                   : FdsBlockTarget::RasterizedObstruction;
    }
    return FdsBlockTarget::Obstruction;
}

void collapseSmallestAxisToPlane(FcFdsBounds& bounds)
{
    const double extents[3] = {bounds.xMax - bounds.xMin,
                               bounds.yMax - bounds.yMin,
                               bounds.zMax - bounds.zMin};
    int axis = extents[1] < extents[0] ? 1 : 0;
    if (extents[2] < extents[axis]) axis = 2;
    // A VENT must lie on a solid boundary.  Collapsing to the box midpoint
    // places it inside the host wall/slab and FDS rejects it as unattached.
    // The input origin represents the selected host face, so retain the
    // minimum coordinate of the thinnest axis.
    if (axis == 0) bounds.xMax = bounds.xMin;
    else if (axis == 1) bounds.yMax = bounds.yMin;
    else bounds.zMax = bounds.zMin;
}
}

FdsBlockConversionResult FdsBlockConversionService::convert(
    const QVector<std::shared_ptr<FcGeometryObject>>& geometry,
    const QVector<std::shared_ptr<FcFdsMesh>>& meshes,
    bool useGeomForArbitrary)
{
    FdsBlockConversionResult result;
    int obstNumber = 1;
    int holeNumber = 1;
    int ventNumber = 1;
    int geomNumber = 1;
    for (const auto& source : geometry) {
        if (!source || !source->hasShape() || !source->isVisible() ||
            source->geometryKind() == FcGeometryKind::BackgroundImage) continue;
        const QString conversionRoute = source->geometryParameters()
                                            .value(QStringLiteral("fdsConversionRoute"),
                                                   QStringLiteral("Auto"))
                                            .toString().trimmed().toUpper();
        if (conversionRoute == QStringLiteral("IGNORE") ||
            conversionRoute == QStringLiteral("REFERENCE")) {
            result.warnings.append(
                QStringLiteral("Geometry '%1' was skipped by its FDS conversion strategy (%2).")
                    .arg(source->name(), conversionRoute));
            continue;
        }
        QVector<FcFdsBounds> requestedPieces;
        const FdsBlockTarget sourceTarget = targetFor(*source, useGeomForArbitrary);
        const BuildingGeometryRequest sourceRequest =
            BuildingGeometryService::requestFromParameters(
                source->geometryKind(), source->geometryParameters());
        if (sourceTarget == FdsBlockTarget::Geom) {
            bool validBounds = false;
            const FcFdsBounds bounds = boundsForShape(*source, &validBounds);
            if (validBounds) requestedPieces.append(bounds);
        } else if (source->geometryKind() == FcGeometryKind::Room) {
            const double half = sourceRequest.thickness * 0.5;
            const double x2 = sourceRequest.x + sourceRequest.width;
            const double y2 = sourceRequest.y + sourceRequest.depth;
            const double z2 = sourceRequest.z + sourceRequest.height;
            requestedPieces = {
                {sourceRequest.x, x2, sourceRequest.y - half, sourceRequest.y + half,
                 sourceRequest.z, z2},
                {x2 - half, x2 + half, sourceRequest.y, y2,
                 sourceRequest.z, z2},
                {sourceRequest.x, x2, y2 - half, y2 + half,
                 sourceRequest.z, z2},
                {sourceRequest.x - half, sourceRequest.x + half,
                 sourceRequest.y, y2, sourceRequest.z, z2}};
        } else if (source->geometryKind() == FcGeometryKind::Stair &&
                   sourceRequest.stepCount > 0) {
            const double tread = sourceRequest.depth / sourceRequest.stepCount;
            const double riser = sourceRequest.rise / sourceRequest.stepCount;
            for (int step = 0; step < sourceRequest.stepCount; ++step) {
                requestedPieces.append(
                    {sourceRequest.x, sourceRequest.x + sourceRequest.width,
                     sourceRequest.y + tread * step,
                     sourceRequest.y + tread * (step + 1),
                     sourceRequest.z, sourceRequest.z + riser * (step + 1)});
            }
        } else {
            bool validBounds = false;
            const FcFdsBounds bounds = boundsForShape(*source, &validBounds);
            if (validBounds) requestedPieces.append(bounds);
        }
        if (requestedPieces.isEmpty()) {
            result.errors.append(QStringLiteral("%1: cannot calculate geometry bounds.")
                                     .arg(source->name()));
            continue;
        }
        for (int partIndex = 0; partIndex < requestedPieces.size(); ++partIndex) {
        FdsBlockPreview preview;
        preview.sourceObjectId = source->id();
        preview.sourceName = requestedPieces.size() > 1
                                 ? QStringLiteral("%1 [part %2]")
                                       .arg(source->name()).arg(partIndex + 1)
                                 : source->name();
        preview.requested = requestedPieces[partIndex];
        preview.actual = preview.requested;
        preview.target = sourceTarget;
        if (preview.target == FdsBlockTarget::Vent) {
            collapseSmallestAxisToPlane(preview.requested);
            preview.actual = preview.requested;
        }
        const auto mesh = std::find_if(meshes.cbegin(), meshes.cend(),
                                       [&preview](const auto& candidate) {
            return candidate && containsCenter(*candidate, preview.requested);
        });
        if (preview.target == FdsBlockTarget::Geom) {
            // Native GEOM preserves the source triangulation.  Grid snapping is
            // reported only for the alternative rasterized OBST route.
            preview.actual = preview.requested;
        } else if (mesh != meshes.cend()) {
            preview.actual = snapBounds(preview.requested, **mesh);
        } else {
            preview.warnings.append(QStringLiteral("No mesh contains the object center; requested bounds are used."));
        }
        preview.requestedVolume = volume(preview.requested);
        preview.actualVolume = volume(preview.actual);
        preview.lost = preview.requestedVolume > kEpsilon &&
                       preview.actualVolume <= kEpsilon &&
                       preview.target != FdsBlockTarget::Vent;
        if (preview.requestedVolume > kEpsilon) {
            preview.volumeErrorPercent =
                100.0 * (preview.actualVolume - preview.requestedVolume) /
                preview.requestedVolume;
        }
        if (preview.lost) {
            preview.warnings.append(QStringLiteral("Object is thinner than the mesh and disappears after snapping."));
        } else if (std::abs(preview.volumeErrorPercent) > 10.0) {
            preview.warnings.append(QStringLiteral("Snapped volume differs by %1%.")
                                        .arg(preview.volumeErrorPercent, 0, 'f', 1));
        }
        for (const QString& warning : preview.warnings) {
            result.warnings.append(QStringLiteral("%1: %2").arg(source->name(), warning));
        }

        FcObject::Ptr converted;
        if (preview.target == FdsBlockTarget::Obstruction ||
            preview.target == FdsBlockTarget::RasterizedObstruction) {
            const QString id = QStringLiteral("AUTO_OBST_%1").arg(obstNumber++, 3, 10, QLatin1Char('0'));
            auto namelist = std::make_shared<FcFdsNamelist>(
                preview.sourceName + QStringLiteral(" [FDS]"),
                FcObjectType::Obstruction, QStringLiteral("OBST"), id);
            namelist->addRawParameter(QStringLiteral("XB"), rawBounds(preview.actual));
            QStringList directionalSurfaces;
            bool completeDirectionalAssignment = true;
            for (const QString& face : {QStringLiteral("X-"), QStringLiteral("X+"),
                                        QStringLiteral("Y-"), QStringLiteral("Y+"),
                                        QStringLiteral("Z-"), QStringLiteral("Z+")}) {
                const QString surface = source->faceSurfaceIds().value(
                    face, source->defaultSurfaceId());
                directionalSurfaces.append(surface);
                completeDirectionalAssignment = completeDirectionalAssignment && !surface.isEmpty();
            }
            if (completeDirectionalAssignment) {
                namelist->addReferenceParameter(QStringLiteral("SURF_ID6"),
                                                directionalSurfaces);
            } else if (!source->defaultSurfaceId().isEmpty()) {
                namelist->addReferenceParameter(QStringLiteral("SURF_ID"),
                                                {source->defaultSurfaceId()});
            }
            converted = namelist;
        } else if (preview.target == FdsBlockTarget::Vent) {
            const QString id = QStringLiteral("AUTO_VENT_%1").arg(ventNumber++, 3, 10, QLatin1Char('0'));
            auto namelist = std::make_shared<FcFdsNamelist>(
                preview.sourceName + QStringLiteral(" [FDS]"),
                FcObjectType::Vent, QStringLiteral("VENT"), id);
            namelist->addRawParameter(QStringLiteral("XB"), rawBounds(preview.actual));
            if (!source->defaultSurfaceId().isEmpty()) {
                namelist->addReferenceParameter(QStringLiteral("SURF_ID"),
                                                {source->defaultSurfaceId()});
            } else {
                namelist->addStringParameter(QStringLiteral("SURF_ID"),
                                             QStringLiteral("INERT"));
                result.warnings.append(
                    QStringLiteral("%1: no VENT surface is assigned; INERT is used. Assign an OPEN, supply/exhaust, or HVAC surface explicitly for an active vent.")
                        .arg(source->name()));
            }
            converted = namelist;
        } else if (preview.target == FdsBlockTarget::Geom) {
            const QString id = QStringLiteral("AUTO_GEOM_%1")
                                   .arg(geomNumber++, 3, 10, QLatin1Char('0'));
            const FdsGeomTriangulation triangles =
                BuildingGeometryService::triangulateForFds(source->shape());
            for (const QString& warning : triangles.warnings) {
                result.warnings.append(QStringLiteral("%1: %2")
                                           .arg(source->name(), warning));
            }
            if (!triangles.valid()) {
                result.errors.append(
                    QStringLiteral("%1: native GEOM triangulation failed.")
                        .arg(source->name()));
                result.previews.append(preview);
                continue;
            }
            auto namelist = std::make_shared<FcFdsNamelist>(
                preview.sourceName + QStringLiteral(" [FDS GEOM]"),
                FcObjectType::Geometry, QStringLiteral("GEOM"), id);
            namelist->addRawParameter(QStringLiteral("VERTS"),
                                      rawRealArray(triangles.vertices));

            QStringList surfaceUuids;
            if (!source->defaultSurfaceId().isEmpty()) {
                surfaceUuids.append(source->defaultSurfaceId());
            }
            for (auto assignment = source->faceSurfaceIds().cbegin();
                 assignment != source->faceSurfaceIds().cend(); ++assignment) {
                if (assignment.key().startsWith(QStringLiteral("TopoFace:")) &&
                    !assignment.value().isEmpty() &&
                    !surfaceUuids.contains(assignment.value())) {
                    surfaceUuids.append(assignment.value());
                }
            }
            if (surfaceUuids.isEmpty()) {
                namelist->addStringParameter(QStringLiteral("SURF_ID"),
                                             QStringLiteral("INERT"));
            } else {
                namelist->addReferenceParameter(QStringLiteral("SURF_ID"), surfaceUuids);
            }

            QVector<int> faces;
            faces.reserve(triangles.triangles.size() * 4);
            for (qsizetype triangleIndex = 0;
                 triangleIndex < triangles.triangles.size(); ++triangleIndex) {
                const std::array<int, 3>& triangle = triangles.triangles[triangleIndex];
                const QString assignedSurface = source->faceSurfaceIds().value(
                    triangles.triangleFaceKeys.value(triangleIndex),
                    source->defaultSurfaceId());
                int surfaceIndex = surfaceUuids.indexOf(assignedSurface) + 1;
                if (surfaceIndex <= 0) surfaceIndex = 1;
                faces.append(triangle[0]);
                faces.append(triangle[1]);
                faces.append(triangle[2]);
                faces.append(surfaceIndex);
            }
            namelist->addRawParameter(QStringLiteral("FACES"),
                                      rawIntegerArray(faces));
            converted = namelist;
        } else {
            const bool hole = preview.target == FdsBlockTarget::Hole;
            const int number = holeNumber++;
            const QString id = QStringLiteral("AUTO_%1_%2")
                                   .arg(QStringLiteral("HOLE"))
                                   .arg(number, 3, 10, QLatin1Char('0'));
            auto namelist = std::make_shared<FcFdsNamelist>(
                preview.sourceName + QStringLiteral(" [FDS]"),
                FcObjectType::Obstruction, QStringLiteral("HOLE"), id);
            namelist->addRawParameter(QStringLiteral("XB"), rawBounds(preview.actual));
            if (hole && source->isDynamicOpening() &&
                !source->controlObjectId().isEmpty()) {
                namelist->addReferenceParameter(QStringLiteral("CTRL_ID"),
                                                {source->controlObjectId()});
            }
            converted = namelist;
        }
        if (converted) {
            QStringList tags = converted->tags();
            tags.append(QStringLiteral("firecae:auto-converted"));
            tags.append(QStringLiteral("source-uuid:%1").arg(source->id()));
            const QString ifcGlobalId = source->geometryParameters()
                                            .value(QStringLiteral("sourceIfcGlobalId"))
                                            .toString();
            const QString ifcClass = source->geometryParameters()
                                         .value(QStringLiteral("sourceIfcClass"))
                                         .toString();
            if (!ifcGlobalId.isEmpty())
                tags.append(QStringLiteral("source-ifc-globalid:%1").arg(ifcGlobalId));
            if (!ifcClass.isEmpty())
                tags.append(QStringLiteral("source-ifc-class:%1").arg(ifcClass));
            converted->setTags(tags);
            result.fdsObjects.append(converted);
        }
        result.previews.append(preview);
        }
    }
    if (result.previews.isEmpty() && result.errors.isEmpty()) {
        result.warnings.append(QStringLiteral("No visible building geometry is available for conversion."));
    }
    return result;
}

FcFdsBounds FdsBlockConversionService::boundsForShape(
    const FcGeometryObject& geometry, bool* valid)
{
    FcFdsBounds bounds;
    if (valid) *valid = false;
    if (!geometry.hasShape()) return bounds;
    try {
        Bnd_Box box;
        BRepBndLib::Add(geometry.shape(), box);
        if (box.IsVoid()) return bounds;
        box.Get(bounds.xMin, bounds.yMin, bounds.zMin,
                bounds.xMax, bounds.yMax, bounds.zMax);
        if (valid) *valid = true;
    } catch (const Standard_Failure&) {
        return {};
    }
    return bounds;
}

FcFdsBounds FdsBlockConversionService::snapBounds(const FcFdsBounds& requested,
                                                   const FcFdsMesh& mesh)
{
    const FcFdsBounds& domain = mesh.bounds();
    const auto cells = mesh.cells();
    const double dx = (domain.xMax - domain.xMin) / std::max(1, cells[0]);
    const double dy = (domain.yMax - domain.yMin) / std::max(1, cells[1]);
    const double dz = (domain.zMax - domain.zMin) / std::max(1, cells[2]);
    const auto snapAxis = [](double minimum, double maximum,
                             double origin, double cell) {
        if (std::abs(maximum - minimum) <= kEpsilon) {
            const double plane = snappedCoordinate(minimum, origin, cell);
            return std::pair<double, double>{plane, plane};
        }
        return std::pair<double, double>{
            enclosingCoordinate(minimum, origin, cell, false),
            enclosingCoordinate(maximum, origin, cell, true)};
    };
    const auto x = snapAxis(requested.xMin, requested.xMax, domain.xMin, dx);
    const auto y = snapAxis(requested.yMin, requested.yMax, domain.yMin, dy);
    const auto z = snapAxis(requested.zMin, requested.zMax, domain.zMin, dz);
    FcFdsBounds actual{x.first, x.second, y.first, y.second, z.first, z.second};
    return actual;
}

QString FdsBlockConversionService::targetName(FdsBlockTarget target)
{
    switch (target) {
    case FdsBlockTarget::Hole: return QStringLiteral("HOLE");
    case FdsBlockTarget::Vent: return QStringLiteral("VENT");
    case FdsBlockTarget::Geom: return QStringLiteral("GEOM");
    case FdsBlockTarget::RasterizedObstruction: return QStringLiteral("Rasterized OBST");
    case FdsBlockTarget::Obstruction:
    default: return QStringLiteral("OBST");
    }
}
