#include "modeling/GeometryEditDependencyService.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "fds/FcFdsModel.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QSet>
#include <cmath>
#include <functional>

namespace {
constexpr double tolerance = 1.0e-6;
bool locked(const FcObject& object)
{
    for (const FcObject* current = &object; current; current = current->parent())
        if (current->isLocked()) return true;
    return false;
}
double baseline(FcWallBaseline value, double thickness)
{
    return value == FcWallBaseline::Center ? -thickness * .5 :
           value == FcWallBaseline::Right ? -thickness : 0.0;
}
bool straightWall(const BuildingGeometryRequest& wall)
{
    return wall.kind == FcGeometryKind::Wall && wall.path.isEmpty() &&
           std::hypot(wall.endX - wall.x, wall.endY - wall.y) > tolerance;
}
bool supportedOpening(FcGeometryKind kind)
{
    return kind == FcGeometryKind::RectangularOpening || kind == FcGeometryKind::Door ||
           kind == FcGeometryKind::Window;
}
QVariantMap geometryParameters(BuildingGeometryRequest request)
{
    request.extraParameters.clear();
    request.fdsConversionRoute.clear();
    if ((request.kind == FcGeometryKind::PolygonPrism || request.kind == FcGeometryKind::ProfileExtrusion) && request.profile3d.isEmpty()) {
        for (const auto& point : request.profile) request.profile3d.append({point.x(), point.y(), request.z});
        request.extrusionNormal = false;
        request.extrusionDirection = {0.0, 0.0, 1.0};
        request.extrusionDistance = request.height;
    }
    const QVariantMap parameters = BuildingGeometryService::requestToParameters(request);
    QStringList keys;
    switch (request.kind) {
    case FcGeometryKind::Wall:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("endX"),QStringLiteral("endY"),QStringLiteral("thickness"),QStringLiteral("height"),QStringLiteral("baseline"),QStringLiteral("path")}; break;
    case FcGeometryKind::Beam:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("endX"),QStringLiteral("endY"),QStringLiteral("thickness"),QStringLiteral("width"),QStringLiteral("baseline")}; break;
    case FcGeometryKind::PolygonPrism:
    case FcGeometryKind::ProfileExtrusion:
        keys = request.profile3d.isEmpty()
            ? QStringList{QStringLiteral("profile"),QStringLiteral("z"),QStringLiteral("height")}
            : QStringList{QStringLiteral("profile3d"),QStringLiteral("extrusionNormal"),QStringLiteral("extrusionDirection"),QStringLiteral("extrusionDistance")}; break;
    case FcGeometryKind::PolygonalOpening:
        keys = {QStringLiteral("profile"),QStringLiteral("z"),QStringLiteral("height")}; break;
    case FcGeometryKind::PathSweep:
    case FcGeometryKind::PolylineSweep:
        keys = {QStringLiteral("path"),QStringLiteral("z"),QStringLiteral("thickness"),QStringLiteral("height"),QStringLiteral("baseline")}; break;
    case FcGeometryKind::Cylinder:
    case FcGeometryKind::Column:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("radius"),QStringLiteral("height")};
        if (request.kind == FcGeometryKind::Column && request.radius <= tolerance)
            keys.append({QStringLiteral("width"),QStringLiteral("depth")});
        break;
    case FcGeometryKind::Stair:
    case FcGeometryKind::Ramp:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("width"),QStringLiteral("depth"),QStringLiteral("rise")};
        if (request.kind == FcGeometryKind::Stair) keys.append(QStringLiteral("stepCount"));
        break;
    case FcGeometryKind::Room:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("width"),QStringLiteral("depth"),QStringLiteral("height"),QStringLiteral("thickness"),QStringLiteral("baseline")}; break;
    default:
        keys = {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z"),QStringLiteral("width"),QStringLiteral("depth"),QStringLiteral("height"),QStringLiteral("rotationDegrees")}; break;
    }
    QVariantMap relevant;
    for (const QString& key : keys) relevant.insert(key, parameters.value(key));
    return relevant;
}
double surfaceArea(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0.0;
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties);
    return properties.Mass();
}
bool burning(const FcObject::Ptr& object)
{
    if (const auto surface = std::dynamic_pointer_cast<FcFdsSurface>(object))
        return surface->heatReleaseRatePerArea() > 0.0;
    if (const auto surface = std::dynamic_pointer_cast<FcFdsNamelist>(object))
        for (const FcFdsParameter& parameter : surface->parameters()) {
            const QString key = parameter.key.toUpper();
            if (key == QStringLiteral("HRRPUA") || key == QStringLiteral("MLRPUA") ||
                key == QStringLiteral("MASS_FLUX") || key == QStringLiteral("RAMP_Q")) return true;
        }
    return false;
}
}

QString GeometryEditDependencyService::validateOpeningPlacement(
    const BuildingGeometryRequest& opening, const BuildingGeometryRequest& host)
{
    if (!straightWall(host) || !supportedOpening(opening.kind))
        return QStringLiteral("Automatic opening edits require a rectangular opening on a straight wall. Edit other host types explicitly.");
    const double length = std::hypot(host.endX - host.x, host.endY - host.y);
    const double tx = (host.endX - host.x) / length, ty = (host.endY - host.y) / length;
    const QString route = host.fdsConversionRoute.trimmed().toUpper();
    if ((route != QStringLiteral("AUTO") && route != QStringLiteral("OBST")) ||
        (std::abs(tx) > tolerance && std::abs(ty) > tolerance))
        return QStringLiteral("Wall openings currently require an axis-aligned wall using Auto or OBST conversion. FDS HOLE does not cut native GEOM and rotated holes would be approximated by oversized bounding boxes.");
    const double u = (opening.x - host.x) * tx + (opening.y - host.y) * ty;
    const double v = -(opening.x - host.x) * ty + (opening.y - host.y) * tx;
    if (u < -tolerance || u + opening.width > length + tolerance ||
        opening.z < host.z - tolerance || opening.z + opening.height > host.z + host.height + tolerance)
        return QStringLiteral("The opening would extend beyond its host wall. Move or resize the opening before shrinking the wall.");
    const double side = baseline(host.baseline, host.thickness);
    if (v > side + tolerance || v + opening.depth < side + host.thickness - tolerance)
        return QStringLiteral("The opening must pass completely through the wall thickness.");
    return {};
}

BuildingGeometryRequest GeometryEditDependencyService::updatedHostedOpening(
    const BuildingGeometryRequest& opening, const BuildingGeometryRequest& oldHost,
    const BuildingGeometryRequest& newHost, QString* error)
{
    if (error) error->clear();
    BuildingGeometryRequest result = opening;
    if (!straightWall(oldHost) || !straightWall(newHost) || !supportedOpening(opening.kind)) {
        if (error) *error = QStringLiteral("Automatic opening edits require a rectangular opening on a straight wall. Edit other host types explicitly.");
        return result;
    }
    const QString oldError = validateOpeningPlacement(opening, oldHost);
    if (!oldError.isEmpty()) { if (error) *error = oldError; return result; }
    result = BuildingGeometryService::followEditedHostWall(opening, oldHost, newHost);
    const double oldLength = std::hypot(oldHost.endX - oldHost.x, oldHost.endY - oldHost.y);
    const double oldTx = (oldHost.endX - oldHost.x) / oldLength, oldTy = (oldHost.endY - oldHost.y) / oldLength;
    const double oldV = -(opening.x - oldHost.x) * oldTy + (opening.y - oldHost.y) * oldTx;
    const double oldSide = baseline(oldHost.baseline, oldHost.thickness);
    const double frontMargin = oldSide - oldV;
    const double backMargin = oldV + opening.depth - (oldSide + oldHost.thickness);
    const double newLength = std::hypot(newHost.endX - newHost.x, newHost.endY - newHost.y);
    const double newTx = (newHost.endX - newHost.x) / newLength, newTy = (newHost.endY - newHost.y) / newLength;
    const double currentV = -(result.x - newHost.x) * newTy + (result.y - newHost.y) * newTx;
    const double desiredV = baseline(newHost.baseline, newHost.thickness) - frontMargin;
    result.x -= newTy * (desiredV - currentV);
    result.y += newTx * (desiredV - currentV);
    result.depth = newHost.thickness + frontMargin + backMargin;
    if (error) *error = validateOpeningPlacement(result, newHost);
    return result;
}

QStringList GeometryEditDependencyService::validate(const FcDocument& document,
    const FcGeometryObject& object, const BuildingGeometryRequest& next)
{
    QStringList errors;
    if (locked(object)) errors.append(QStringLiteral("The selected geometry or its parent is locked."));
    const auto before = BuildingGeometryService::requestFromParameters(object.geometryKind(), object.geometryParameters());
    if (!geometryChanged(object, next)) return errors;
    if (!object.hostObjectId().isEmpty()) {
        const auto host = std::dynamic_pointer_cast<FcGeometryObject>(document.findObject(object.hostObjectId()));
        if (!host) errors.append(QStringLiteral("The opening host no longer exists. Reassign its host before editing."));
        else if (locked(*host)) errors.append(QStringLiteral("The opening host is locked. Unlock it before changing the opening."));
        else {
            const QString placement = validateOpeningPlacement(next,
                BuildingGeometryService::requestFromParameters(host->geometryKind(), host->geometryParameters()));
            if (!placement.isEmpty()) errors.append(placement);
        }
    }
    QString shapeError;
    const auto nextShape = BuildingGeometryService::createShape(next, &shapeError);
    if (nextShape.IsNull()) { errors.append(shapeError); return errors; }
    QSet<QString> surfaces;
    surfaces.insert(object.defaultSurfaceId());
    for (const QString& id : object.faceSurfaceIds()) surfaces.insert(id);
    if (std::abs(surfaceArea(object.shape()) - surfaceArea(nextShape)) > tolerance)
        for (const QString& id : surfaces)
            if (!id.isEmpty() && burning(document.findObject(id))) {
                errors.append(QStringLiteral("This edit changes a burning surface area. Detach the fire surface and explicitly choose total-power or heat-release-rate-per-area behavior before reassigning it."));
                break;
            }
    std::function<void(const FcObject::Ptr&)> inspect = [&](const FcObject::Ptr& candidate) {
        if (!candidate) return;
        if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(candidate);
            geometry && geometry->id() != object.id() && geometry->hostObjectId() == object.id()) {
            if (locked(*geometry)) errors.append(QStringLiteral("An attached opening is locked. Unlock it before editing the host."));
            const auto oldOpening = BuildingGeometryService::requestFromParameters(geometry->geometryKind(), geometry->geometryParameters());
            QString openingError;
            const auto changed = updatedHostedOpening(oldOpening, before, next, &openingError);
            if (!openingError.isEmpty()) errors.append(openingError);
            if (!geometry->faceSurfaceIds().isEmpty() && geometryParameters(oldOpening) != geometryParameters(changed))
                errors.append(QStringLiteral("An attached opening has face-specific surfaces. Clear or reassign those faces before modifying its host."));
        }
        if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(candidate)) {
            for (const auto& parameter : namelist->parameters())
                if (parameter.kind == FcFdsParameterKind::ObjectReferences && parameter.targetObjectIds.contains(object.id()))
                    errors.append(QStringLiteral("A solver object references this geometry. Its attachment cannot be updated automatically; detach or reassign the reference before changing geometry."));
        }
        for (const auto& child : candidate->children()) inspect(child);
    };
    for (const auto& group : document.groups()) inspect(group);
    errors.removeDuplicates();
    return errors;
}

bool GeometryEditDependencyService::geometryChanged(const FcGeometryObject& object,
    const BuildingGeometryRequest& next)
{
    return geometryParameters(BuildingGeometryService::requestFromParameters(
        object.geometryKind(), object.geometryParameters())) != geometryParameters(next) ||
        object.geometryKind() != next.kind;
}

TopoDS_Shape GeometryEditDependencyService::displayShape(const FcGeometryObject& host,
    const QVector<std::shared_ptr<FcGeometryObject>>& geometry, QStringList* warnings)
{
    TopoDS_Shape result = host.shape();
    if (result.IsNull() || BuildingGeometryService::isOpeningKind(host.geometryKind())) return result;
    for (const auto& opening : geometry) {
        if (!opening || opening->hostObjectId() != host.id() || !opening->hasShape() ||
            opening->isDynamicOpening() || !BuildingGeometryService::isOpeningKind(opening->geometryKind()) ||
            opening->geometryKind() == FcGeometryKind::WallVent) continue;
        const QString route = opening->geometryParameters().value(QStringLiteral("fdsConversionRoute")).toString().toUpper();
        if (route == QStringLiteral("IGNORE") || route == QStringLiteral("REFERENCE")) continue;
        QString error;
        const TopoDS_Shape cut = BuildingGeometryService::booleanOperation(result, opening->shape(), FcBooleanOperation::Difference, &error);
        if (!cut.IsNull()) result = cut;
        else if (warnings) warnings->append(QStringLiteral("A static opening could not be cut from its display host; inspect the opening dimensions."));
    }
    return result;
}
