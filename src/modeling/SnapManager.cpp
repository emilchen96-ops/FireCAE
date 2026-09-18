#include "modeling/SnapManager.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Curve.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
double snappedValue(double value, double step)
{
    return step > 0.0 ? std::round(value / step) * step : value;
}
}

const SnapSettings& SnapManager::settings() const
{
    return m_settings;
}

void SnapManager::setSettings(const SnapSettings& settings)
{
    m_settings = settings;
    m_settings.worldGridStep = std::max(settings.worldGridStep, 1.0e-9);
    m_settings.fdsGridStep = std::max(settings.fdsGridStep, 1.0e-9);
    m_settings.angleStepDegrees = std::max(settings.angleStepDegrees, 1.0e-9);
}

void SnapManager::setTemporarilyDisabled(bool disabled)
{
    m_temporarilyDisabled = disabled;
}

bool SnapManager::isTemporarilyDisabled() const
{
    return m_temporarilyDisabled;
}

bool SnapManager::isEnabled() const
{
    return m_settings.enabled && !m_temporarilyDisabled;
}

SnapResult SnapManager::snapPoint(const gp_Pnt& rawPoint,
                                  const QVector<TopoDS_Shape>& candidates,
                                  double geometricTolerance) const
{
    SnapResult result{rawPoint, SnapTarget::None, false, 0.0};
    if (!isEnabled()) return result;

    double bestGeometricDistance = std::max(0.0, geometricTolerance);
    auto considerGeometric = [&](const gp_Pnt& point, SnapTarget target) {
        const double distance = rawPoint.Distance(point);
        if (distance <= bestGeometricDistance) {
            bestGeometricDistance = distance;
            result = {point, target, true, distance};
        }
    };

    for (const TopoDS_Shape& shape : candidates) {
        if (shape.IsNull()) continue;
        if (m_settings.objectCenter) {
            Bnd_Box bounds;
            BRepBndLib::Add(shape, bounds);
            if (!bounds.IsVoid()) {
                Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
                Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
                bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
                considerGeometric(gp_Pnt((xMin + xMax) * 0.5,
                                         (yMin + yMax) * 0.5,
                                         (zMin + zMax) * 0.5),
                                  SnapTarget::ObjectCenter);
            }
        }
        if (m_settings.vertex) {
            for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
                considerGeometric(BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())),
                                  SnapTarget::Vertex);
            }
        }
        if (m_settings.edge || m_settings.edgeMidpoint) {
            for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
                const TopoDS_Edge edgeShape = TopoDS::Edge(explorer.Current());
                Standard_Real first = 0.0;
                Standard_Real last = 0.0;
                const Handle(Geom_Curve) curve = BRep_Tool::Curve(edgeShape, first, last);
                if (curve.IsNull()) continue;
                if (m_settings.edgeMidpoint) {
                    considerGeometric(curve->Value((first + last) * 0.5),
                                      SnapTarget::EdgeMidpoint);
                }
                if (m_settings.edge) {
                    GeomAPI_ProjectPointOnCurve projection(rawPoint, curve, first, last);
                    if (projection.NbPoints() > 0) {
                        considerGeometric(projection.NearestPoint(), SnapTarget::Edge);
                    }
                }
            }
        }
        if (m_settings.face) {
            const TopoDS_Vertex pointVertex = BRepBuilderAPI_MakeVertex(rawPoint).Vertex();
            for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
                BRepExtrema_DistShapeShape distance(
                    pointVertex, TopoDS::Face(explorer.Current()));
                distance.Perform();
                if (distance.IsDone() && distance.NbSolution() > 0) {
                    considerGeometric(distance.PointOnShape2(1), SnapTarget::Face);
                }
            }
        }
    }
    if (m_settings.intersection && candidates.size() > 1) {
        // Only intersect shapes whose expanded bounds contain the cursor.  This
        // keeps architectural snapping responsive for large imported models
        // while still finding the intersection the user can actually select.
        QVector<int> nearby;
        nearby.reserve(std::min<int>(static_cast<int>(candidates.size()), 32));
        for (int index = 0; index < candidates.size() && nearby.size() < 32; ++index) {
            const TopoDS_Shape& shape = candidates[index];
            if (shape.IsNull()) continue;
            Bnd_Box bounds;
            BRepBndLib::Add(shape, bounds);
            bounds.Enlarge(std::max(geometricTolerance, 1.0e-6));
            if (!bounds.IsVoid() && !bounds.IsOut(rawPoint)) nearby.append(index);
        }
        for (int left = 0; left < nearby.size(); ++left) {
            for (int right = left + 1; right < nearby.size(); ++right) {
                BRepAlgoAPI_Section section(candidates[nearby[left]],
                                            candidates[nearby[right]],
                                            Standard_False);
                section.Approximation(Standard_True);
                section.Build();
                if (!section.IsDone()) continue;
                const TopoDS_Shape resultShape = section.Shape();
                for (TopExp_Explorer explorer(resultShape, TopAbs_VERTEX);
                     explorer.More(); explorer.Next()) {
                    considerGeometric(BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())),
                                      SnapTarget::Intersection);
                }
                for (TopExp_Explorer explorer(resultShape, TopAbs_EDGE);
                     explorer.More(); explorer.Next()) {
                    Standard_Real first = 0.0;
                    Standard_Real last = 0.0;
                    const Handle(Geom_Curve) curve = BRep_Tool::Curve(
                        TopoDS::Edge(explorer.Current()), first, last);
                    if (curve.IsNull()) continue;
                    GeomAPI_ProjectPointOnCurve projection(rawPoint, curve, first, last);
                    if (projection.NbPoints() > 0) {
                        considerGeometric(projection.NearestPoint(),
                                          SnapTarget::Intersection);
                    }
                }
            }
        }
    }
    if (result.snapped) return result;

    const auto gridCandidate = [&](double step, SnapTarget target) {
        const gp_Pnt point(snappedValue(rawPoint.X(), step),
                          snappedValue(rawPoint.Y(), step),
                          snappedValue(rawPoint.Z(), step));
        return SnapResult{point, target, true, rawPoint.Distance(point)};
    };
    SnapResult bestGrid = result;
    bestGrid.distance = std::numeric_limits<double>::max();
    if (m_settings.worldGrid) {
        bestGrid = gridCandidate(m_settings.worldGridStep, SnapTarget::WorldGrid);
    }
    if (m_settings.fdsGrid) {
        const SnapResult fds = gridCandidate(m_settings.fdsGridStep, SnapTarget::FdsGrid);
        if (!bestGrid.snapped || fds.distance < bestGrid.distance) bestGrid = fds;
    }
    return bestGrid;
}

gp_Vec SnapManager::constrainTranslation(const gp_Vec& translation) const
{
    if (!isEnabled() || !m_settings.orthogonal) return translation;
    const double values[3] = {translation.X(), translation.Y(), translation.Z()};
    int dominantAxis = 0;
    if (std::abs(values[1]) > std::abs(values[dominantAxis])) dominantAxis = 1;
    if (std::abs(values[2]) > std::abs(values[dominantAxis])) dominantAxis = 2;
    return gp_Vec(dominantAxis == 0 ? values[0] : 0.0,
                  dominantAxis == 1 ? values[1] : 0.0,
                  dominantAxis == 2 ? values[2] : 0.0);
}

double SnapManager::snapAngle(double degrees) const
{
    if (!isEnabled() || !m_settings.angle) return degrees;
    return snappedValue(degrees, m_settings.angleStepDegrees);
}

QString SnapManager::targetName(SnapTarget target)
{
    switch (target) {
    case SnapTarget::WorldGrid: return QStringLiteral("World Grid");
    case SnapTarget::FdsGrid: return QStringLiteral("FDS Grid");
    case SnapTarget::Vertex: return QStringLiteral("Vertex");
    case SnapTarget::EdgeMidpoint: return QStringLiteral("Edge Midpoint");
    case SnapTarget::Intersection: return QStringLiteral("Intersection");
    case SnapTarget::Edge: return QStringLiteral("Edge");
    case SnapTarget::Face: return QStringLiteral("Face");
    case SnapTarget::ObjectCenter: return QStringLiteral("Object Center");
    case SnapTarget::Orthogonal: return QStringLiteral("Orthogonal");
    case SnapTarget::Angle: return QStringLiteral("Angle");
    case SnapTarget::None: return QStringLiteral("None");
    }
    return QStringLiteral("None");
}
