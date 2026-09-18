#include "visualization/FdsSceneSynchronizer.h"

#include "fds/FdsScene.h"
#include "visualization/GeometryDisplayManager.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
double domainDiagonal(const FdsScene& scene)
{
    if (!scene.hasDomainBounds) return 1.0;
    const double dx = scene.domainBounds.xMax - scene.domainBounds.xMin;
    const double dy = scene.domainBounds.yMax - scene.domainBounds.yMin;
    const double dz = scene.domainBounds.zMax - scene.domainBounds.zMin;
    return std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 0.1);
}

TopoDS_Shape shapeForPrimitive(const FdsScenePrimitive& primitive,
                               double diagonal)
{
    if (primitive.kind == FdsScenePrimitiveKind::Point) {
        const double radius = std::clamp(diagonal * 0.012, 0.01, 0.20);
        return BRepPrimAPI_MakeSphere(
                   gp_Pnt(primitive.start.x, primitive.start.y, primitive.start.z),
                   radius)
            .Shape();
    }
    if (primitive.kind == FdsScenePrimitiveKind::Line) {
        const gp_Pnt start(primitive.start.x, primitive.start.y, primitive.start.z);
        const gp_Pnt end(primitive.end.x, primitive.end.y, primitive.end.z);
        if (start.Distance(end) <= 1.0e-9) return {};
        return BRepBuilderAPI_MakeEdge(start, end).Shape();
    }

    FcFdsBounds bounds = primitive.bounds;
    if (primitive.kind == FdsScenePrimitiveKind::Plane) {
        const double thickness = std::clamp(diagonal * 0.002, 0.002, 0.05);
        if (bounds.xMax - bounds.xMin <= 1.0e-12) {
            bounds.xMin -= thickness * 0.5;
            bounds.xMax += thickness * 0.5;
        }
        if (bounds.yMax - bounds.yMin <= 1.0e-12) {
            bounds.yMin -= thickness * 0.5;
            bounds.yMax += thickness * 0.5;
        }
        if (bounds.zMax - bounds.zMin <= 1.0e-12) {
            bounds.zMin -= thickness * 0.5;
            bounds.zMax += thickness * 0.5;
        }
    }
    const double dx = bounds.xMax - bounds.xMin;
    const double dy = bounds.yMax - bounds.yMin;
    const double dz = bounds.zMax - bounds.zMin;
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) return {};
    return BRepPrimAPI_MakeBox(gp_Pnt(bounds.xMin, bounds.yMin, bounds.zMin),
                               dx, dy, dz)
        .Shape();
}
}

FdsSceneSynchronizer::FdsSceneSynchronizer(
    GeometryDisplayManager* displayManager)
    : m_displayManager(displayManager)
{
}

FdsSceneSyncResult FdsSceneSynchronizer::rebuild(const FcProject& project)
{
    FdsSceneSyncResult result;
    if (!m_displayManager) {
        result.warnings.append(QStringLiteral("3D display manager is unavailable."));
        return result;
    }

    for (const QString& objectId : std::as_const(m_displayedObjectIds)) {
        m_displayManager->removeObject(objectId);
    }
    m_displayedObjectIds.clear();

    const FdsScene scene = FdsSceneBuilder::build(project);
    result.warnings = scene.warnings;
    const double diagonal = domainDiagonal(scene);
    for (const FdsScenePrimitive& primitive : scene.primitives) {
        const TopoDS_Shape shape = shapeForPrimitive(primitive, diagonal);
        if (shape.IsNull()) {
            result.warnings.append(
                QStringLiteral("Could not create 3D shape for %1 (%2).")
                    .arg(primitive.name, primitive.keyword));
            continue;
        }
        GeometryDisplayStyle style;
        style.red = primitive.color.red;
        style.green = primitive.color.green;
        style.blue = primitive.color.blue;
        style.transparency = 1.0 - primitive.color.alpha;
        style.wireframe = primitive.wireframe;
        style.visible = primitive.visible;
        if (m_displayManager->appendShape(primitive.objectId, shape, style)) {
            m_displayedObjectIds.insert(primitive.objectId);
            ++result.presentationCount;
        }
    }
    result.businessObjectCount = m_displayedObjectIds.size();
    m_displayManager->updateViewer();
    return result;
}

void FdsSceneSynchronizer::clear()
{
    if (m_displayManager) {
        for (const QString& objectId : std::as_const(m_displayedObjectIds)) {
            m_displayManager->removeObject(objectId);
        }
        m_displayManager->updateViewer();
    }
    m_displayedObjectIds.clear();
}

const QSet<QString>& FdsSceneSynchronizer::displayedObjectIds() const
{
    return m_displayedObjectIds;
}
