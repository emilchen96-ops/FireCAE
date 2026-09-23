#pragma once

#include "modeling/BuildingGeometryService.h"

class FcDocument;

// Shared by property commits and direct manipulation. These helpers never
// mutate business objects; the caller commits one undoable transaction.
class GeometryEditDependencyService final
{
public:
    static bool geometryChanged(const FcGeometryObject& object,
                                const BuildingGeometryRequest& next);
    static QStringList validate(const FcDocument& document,
                                const FcGeometryObject& object,
                                const BuildingGeometryRequest& next);
    static BuildingGeometryRequest updatedHostedOpening(
        const BuildingGeometryRequest& opening,
        const BuildingGeometryRequest& oldHost,
        const BuildingGeometryRequest& newHost,
        QString* error = nullptr);
    static QString validateOpeningPlacement(const BuildingGeometryRequest& opening,
                                           const BuildingGeometryRequest& host);
    static TopoDS_Shape displayShape(
        const FcGeometryObject& host,
        const QVector<std::shared_ptr<FcGeometryObject>>& geometry,
        QStringList* warnings = nullptr);
};
