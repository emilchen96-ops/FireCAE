#pragma once
#include "modeling/BuildingGeometryService.h"
#include <gp_Trsf.hxx>

struct GeometryEditHandle
{
    QString key;
    std::array<double, 3> position{};
    std::array<double, 3> direction{};
};

// Pure, deterministic edit operations. Deltas and snap increments are meters;
// previews do not mutate the model. The application commits one undo command.
class GeometryEditService final
{
public:
    static QVector<GeometryEditHandle> handles(const BuildingGeometryRequest& request);
    static bool moveHandle(const BuildingGeometryRequest& before, const QString& key,
                           double delta, double snapStep, BuildingGeometryRequest* after,
                           QString* error = nullptr);
    static bool transformRequest(const BuildingGeometryRequest& before,
                                 const gp_Trsf& transform,
                                 BuildingGeometryRequest* after,
                                 QString* error = nullptr);
    // Conservative stale-parameter guard: compares solid bounds, mass properties
    // and per-face area/centroids. This is not a general topology equivalence
    // proof. Hosted cuts must be compared to their correspondingly cut request.
    static bool matchesShape(const BuildingGeometryRequest& request,
                             const TopoDS_Shape& actual,
                             double tolerance = 1.0e-6,
                             QString* error = nullptr);
};
