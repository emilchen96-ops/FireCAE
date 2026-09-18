#pragma once

#include "geometry/FcGeometryObject.h"

#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <TopoDS_Shape.hxx>

#include <array>
#include <memory>

class FcGeometryObject;

enum class FcWallBaseline
{
    Left,
    Center,
    Right
};

enum class FcBooleanOperation
{
    Union,
    Difference,
    Intersection,
    Split
};

struct BuildingGeometryRequest
{
    FcGeometryKind kind = FcGeometryKind::Box;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double endX = 4.0;
    double endY = 0.0;
    double width = 4.0;
    double depth = 3.0;
    double height = 3.0;
    double thickness = 0.2;
    double radius = 0.25;
    double rise = 3.0;
    double rotationDegrees = 0.0;
    QString fdsConversionRoute = QStringLiteral("Auto");
    int stepCount = 12;
    FcWallBaseline baseline = FcWallBaseline::Center;
    QVector<QPointF> profile;
    QVector<QPointF> path;
};

struct GeometryFaceInfo
{
    QString key;
    QString label;
    int faceIndex = 0;
    double area = 0.0;
    std::array<double, 3> center{0.0, 0.0, 0.0};
    std::array<double, 3> normal{0.0, 0.0, 0.0};
};

struct FdsGeomTriangulation
{
    QVector<double> vertices;
    QVector<std::array<int, 3>> triangles;
    QStringList triangleFaceKeys;
    QStringList warnings;

    bool valid() const
    {
        return vertices.size() >= 9 && !triangles.isEmpty() &&
               triangles.size() == triangleFaceKeys.size();
    }
};

struct GeometryValidationResult
{
    bool valid = false;
    bool closed = false;
    bool hasDegenerateEdges = false;
    bool hasSelfIntersection = false;
    bool nonManifold = false;
    bool hasDuplicateVertices = false;
    bool hasDuplicateFaces = false;
    int vertexCount = 0;
    int faceCount = 0;
    int duplicateVertexCount = 0;
    int duplicateFaceCount = 0;
    QStringList errors;
    QStringList warnings;
};

class BuildingGeometryService final
{
public:
    static TopoDS_Shape createShape(const BuildingGeometryRequest& request,
                                    QString* error = nullptr);
    static QVariantMap requestToParameters(const BuildingGeometryRequest& request);
    static BuildingGeometryRequest requestFromParameters(FcGeometryKind kind,
                                                         const QVariantMap& parameters);
    static GeometryValidationResult validate(const TopoDS_Shape& shape);
    static TopoDS_Shape heal(const TopoDS_Shape& shape, QString* error = nullptr);
    static QVector<GeometryFaceInfo> faceInfos(const TopoDS_Shape& shape,
                                               double linearTolerance = 1.0e-6);
    static FdsGeomTriangulation triangulateForFds(const TopoDS_Shape& shape,
                                                  double deflection = 0.01);
    static BuildingGeometryRequest rebaseWall(const BuildingGeometryRequest& request,
                                              FcWallBaseline baseline,
                                              bool preserveFootprint = true);
    static QPointF snapWallEndpoint(
        const QPointF& point,
        const QVector<std::shared_ptr<FcGeometryObject>>& existingWalls,
        double tolerance,
        QString* connectedWallId = nullptr);
    static BuildingGeometryRequest attachOpeningToWall(
        const BuildingGeometryRequest& opening,
        const BuildingGeometryRequest& wall);
    static BuildingGeometryRequest followEditedHostWall(
        const BuildingGeometryRequest& opening,
        const BuildingGeometryRequest& oldWall,
        const BuildingGeometryRequest& newWall);
    static TopoDS_Shape booleanOperation(const TopoDS_Shape& first,
                                         const TopoDS_Shape& second,
                                         FcBooleanOperation operation,
                                         QString* error = nullptr);
    static bool isOpeningKind(FcGeometryKind kind);
};
