#include "modeling/BuildingGeometryService.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QVariantList>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kTolerance = 1.0e-7;

void setError(QString* error, const QString& text)
{
    if (error) *error = text;
}

bool positive(double value) { return std::isfinite(value) && value > kTolerance; }

double baselineOffset(FcWallBaseline baseline, double thickness)
{
    if (baseline == FcWallBaseline::Center) return -0.5 * thickness;
    if (baseline == FcWallBaseline::Right) return -thickness;
    return 0.0;
}

QString quantizedPointKey(double x, double y, double z, double tolerance)
{
    const double scale = 1.0 / std::max(tolerance, 1.0e-12);
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qlonglong>(std::llround(x * scale)))
        .arg(static_cast<qlonglong>(std::llround(y * scale)))
        .arg(static_cast<qlonglong>(std::llround(z * scale)));
}

TopoDS_Shape rotatedBox(double x, double y, double z,
                        double width, double depth, double height,
                        double rotationDegrees)
{
    if (!positive(width) || !positive(depth) || !positive(height)) return {};
    const TopoDS_Shape local = BRepPrimAPI_MakeBox(
        gp_Pnt(0.0, 0.0, 0.0), width, depth, height).Shape();
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                         rotationDegrees * std::acos(-1.0) / 180.0);
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(x, y, z));
    rotation.PreMultiply(translation);
    return BRepBuilderAPI_Transform(local, rotation, true).Shape();
}

std::array<double, 3> faceNormal(const TopoDS_Face& face,
                                 double deflection = 0.01)
{
    BRepMesh_IncrementalMesh mesher(face, std::max(deflection, 1.0e-6),
                                    Standard_False, 0.35, Standard_True);
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangles = BRep_Tool::Triangulation(face, location);
    if (triangles.IsNull()) return {0.0, 0.0, 0.0};
    const gp_Trsf transform = location.Transformation();
    for (int index = 1; index <= triangles->NbTriangles(); ++index) {
        int first = 0, second = 0, third = 0;
        triangles->Triangle(index).Get(first, second, third);
        gp_Pnt a = triangles->Node(first).Transformed(transform);
        gp_Pnt b = triangles->Node(second).Transformed(transform);
        gp_Pnt c = triangles->Node(third).Transformed(transform);
        if (face.Orientation() == TopAbs_REVERSED) std::swap(b, c);
        const gp_Vec ab(a, b);
        const gp_Vec ac(a, c);
        gp_Vec normal = ab.Crossed(ac);
        if (normal.SquareMagnitude() <= 1.0e-20) continue;
        normal.Normalize();
        return {normal.X(), normal.Y(), normal.Z()};
    }
    return {0.0, 0.0, 0.0};
}

QString faceSignature(const TopoDS_Face& face, double tolerance,
                      GeometryFaceInfo* output = nullptr)
{
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    const gp_Pnt center = properties.CentreOfMass();
    const std::array<double, 3> normal = faceNormal(face);
    const double scale = 1.0 / std::max(tolerance, 1.0e-12);
    const QString canonical = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(static_cast<qlonglong>(std::llround(properties.Mass() * scale)))
        .arg(static_cast<qlonglong>(std::llround(center.X() * scale)))
        .arg(static_cast<qlonglong>(std::llround(center.Y() * scale)))
        .arg(static_cast<qlonglong>(std::llround(center.Z() * scale)))
        .arg(static_cast<qlonglong>(std::llround(normal[0] * 1.0e6)))
        .arg(static_cast<qlonglong>(std::llround(normal[1] * 1.0e6)))
        .arg(static_cast<qlonglong>(std::llround(normal[2] * 1.0e6)));
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    if (output) {
        output->area = properties.Mass();
        output->center = {center.X(), center.Y(), center.Z()};
        output->normal = normal;
    }
    return QStringLiteral("TopoFace:%1").arg(digest);
}

TopoDS_Shape orientedBox(double x, double y, double z,
                         double endX, double endY,
                         double thickness, double height,
                         FcWallBaseline baseline)
{
    const double dx = endX - x;
    const double dy = endY - y;
    const double length = std::hypot(dx, dy);
    if (!positive(length) || !positive(thickness) || !positive(height)) return {};

    const double transverseOffset = baselineOffset(baseline, thickness);

    const TopoDS_Shape local = BRepPrimAPI_MakeBox(
        gp_Pnt(0.0, transverseOffset, 0.0), length, thickness, height).Shape();
    gp_Trsf transform;
    transform.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                          std::atan2(dy, dx));
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(x, y, z));
    transform.PreMultiply(translation);
    return BRepBuilderAPI_Transform(local, transform, true).Shape();
}

TopoDS_Shape polygonPrism(const QVector<QPointF>& points, double z, double height)
{
    if (points.size() < 3 || !positive(height)) return {};
    BRepBuilderAPI_MakePolygon polygon;
    for (const QPointF& point : points) {
        polygon.Add(gp_Pnt(point.x(), point.y(), z));
    }
    polygon.Close();
    if (!polygon.IsDone()) return {};
    const TopoDS_Wire wire = polygon.Wire();
    BRepBuilderAPI_MakeFace face(wire);
    if (!face.IsDone()) return {};
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0.0, 0.0, height)).Shape();
}

TopoDS_Shape sweptPath(const QVector<QPointF>& points, double z,
                       double thickness, double height,
                       FcWallBaseline baseline)
{
    if (points.size() < 2 || !positive(thickness) || !positive(height)) return {};
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    int segmentCount = 0;
    for (qsizetype index = 1; index < points.size(); ++index) {
        const QPointF& first = points[index - 1];
        const QPointF& second = points[index];
        const TopoDS_Shape segment = orientedBox(
            first.x(), first.y(), z, second.x(), second.y(),
            thickness, height, baseline);
        if (!segment.IsNull()) {
            builder.Add(compound, segment);
            ++segmentCount;
        }
    }
    return segmentCount > 0 ? TopoDS_Shape(compound) : TopoDS_Shape{};
}

TopoDS_Shape stairShape(const BuildingGeometryRequest& request)
{
    if (request.stepCount < 1 || !positive(request.width) ||
        !positive(request.depth) || !positive(request.rise)) return {};
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    const double tread = request.depth / request.stepCount;
    const double riser = request.rise / request.stepCount;
    for (int index = 0; index < request.stepCount; ++index) {
        const double stepHeight = riser * (index + 1);
        const TopoDS_Shape step = BRepPrimAPI_MakeBox(
            gp_Pnt(request.x, request.y + tread * index, request.z),
            request.width, tread, stepHeight).Shape();
        builder.Add(compound, step);
    }
    return compound;
}

TopoDS_Shape rampShape(const BuildingGeometryRequest& request)
{
    if (!positive(request.width) || !positive(request.depth) || !positive(request.rise)) {
        return {};
    }
    const QVector<QPointF> side = {
        QPointF(request.y, request.z),
        QPointF(request.y + request.depth, request.z),
        QPointF(request.y + request.depth, request.z + request.rise)};
    BRepBuilderAPI_MakePolygon polygon;
    for (const QPointF& point : side) polygon.Add(gp_Pnt(request.x, point.x(), point.y()));
    polygon.Close();
    if (!polygon.IsDone()) return {};
    BRepBuilderAPI_MakeFace face(polygon.Wire());
    if (!face.IsDone()) return {};
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(request.width, 0.0, 0.0)).Shape();
}

TopoDS_Shape roomShape(const BuildingGeometryRequest& request)
{
    if (!positive(request.width) || !positive(request.depth) ||
        !positive(request.height) || !positive(request.thickness)) return {};
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    const double x2 = request.x + request.width;
    const double y2 = request.y + request.depth;
    const BuildingGeometryRequest walls[4] = {
        {FcGeometryKind::Wall, request.x, request.y, request.z, x2, request.y,
         0, 0, request.height, request.thickness},
        {FcGeometryKind::Wall, x2, request.y, request.z, x2, y2,
         0, 0, request.height, request.thickness},
        {FcGeometryKind::Wall, x2, y2, request.z, request.x, y2,
         0, 0, request.height, request.thickness},
        {FcGeometryKind::Wall, request.x, y2, request.z, request.x, request.y,
         0, 0, request.height, request.thickness}};
    for (const BuildingGeometryRequest& wall : walls) {
        builder.Add(compound, orientedBox(wall.x, wall.y, wall.z,
                                          wall.endX, wall.endY,
                                          wall.thickness, wall.height,
                                          request.baseline));
    }
    return compound;
}

QVariantList pointsToVariant(const QVector<QPointF>& points)
{
    QVariantList result;
    for (const QPointF& point : points) {
        result.append(QVariantList{point.x(), point.y()});
    }
    return result;
}

QVector<QPointF> pointsFromVariant(const QVariant& value)
{
    QVector<QPointF> result;
    for (const QVariant& item : value.toList()) {
        const QVariantList pair = item.toList();
        if (pair.size() == 2) result.append(QPointF(pair[0].toDouble(), pair[1].toDouble()));
    }
    return result;
}
}

TopoDS_Shape BuildingGeometryService::createShape(const BuildingGeometryRequest& request,
                                                   QString* error)
{
    setError(error, {});
    try {
        TopoDS_Shape shape;
        switch (request.kind) {
        case FcGeometryKind::Wall:
            shape = request.path.size() >= 2
                        ? sweptPath(request.path, request.z, request.thickness,
                                    request.height, request.baseline)
                        : orientedBox(request.x, request.y, request.z,
                                      request.endX, request.endY,
                                      request.thickness, request.height,
                                      request.baseline);
            break;
        case FcGeometryKind::Beam:
            shape = orientedBox(request.x, request.y, request.z,
                                request.endX, request.endY,
                                request.thickness, request.width,
                                request.baseline);
            break;
        case FcGeometryKind::PolylineSweep:
        case FcGeometryKind::PathSweep:
            shape = sweptPath(request.path, request.z, request.thickness,
                              request.height, request.baseline);
            break;
        case FcGeometryKind::Cylinder:
        case FcGeometryKind::Column:
            if (request.kind == FcGeometryKind::Column && request.radius <= kTolerance) {
                shape = BRepPrimAPI_MakeBox(gp_Pnt(request.x, request.y, request.z),
                                            request.width, request.depth,
                                            request.height).Shape();
            } else if (positive(request.radius) && positive(request.height)) {
                shape = BRepPrimAPI_MakeCylinder(
                    gp_Ax2(gp_Pnt(request.x, request.y, request.z), gp_Dir(0, 0, 1)),
                    request.radius, request.height).Shape();
            }
            break;
        case FcGeometryKind::PolygonPrism:
        case FcGeometryKind::PolygonalOpening:
        case FcGeometryKind::ProfileExtrusion:
            shape = polygonPrism(request.profile, request.z, request.height);
            break;
        case FcGeometryKind::Stair:
            shape = stairShape(request);
            break;
        case FcGeometryKind::Ramp:
            shape = rampShape(request);
            break;
        case FcGeometryKind::Room:
            shape = roomShape(request);
            break;
        case FcGeometryKind::Box:
        case FcGeometryKind::Slab:
        case FcGeometryKind::Roof:
        case FcGeometryKind::RectangleProfile:
        case FcGeometryKind::RectangularOpening:
        case FcGeometryKind::Door:
        case FcGeometryKind::Window:
        case FcGeometryKind::SlabOpening:
        case FcGeometryKind::WallVent:
            if (positive(request.width) && positive(request.depth) && positive(request.height)) {
                shape = rotatedBox(request.x, request.y, request.z,
                                   request.width, request.depth,
                                   request.height, request.rotationDegrees);
            }
            break;
        case FcGeometryKind::Generic:
        case FcGeometryKind::BackgroundImage:
        default:
            setError(error, QStringLiteral("This geometry kind cannot create a solid."));
            return {};
        }
        if (shape.IsNull()) {
            setError(error, QStringLiteral("The dimensions or profile do not define a valid solid."));
            return {};
        }
        const GeometryValidationResult validation = validate(shape);
        if (!validation.valid) {
            setError(error, validation.errors.join(QStringLiteral(" ")));
            return {};
        }
        return shape;
    } catch (const Standard_Failure& failure) {
        setError(error, QStringLiteral("OpenCascade could not create the geometry: %1")
                            .arg(QString::fromLatin1(failure.GetMessageString())));
    } catch (...) {
        setError(error, QStringLiteral("An unexpected geometry creation error occurred."));
    }
    return {};
}

QVariantMap BuildingGeometryService::requestToParameters(
    const BuildingGeometryRequest& request)
{
    return {{QStringLiteral("x"), request.x},
            {QStringLiteral("y"), request.y},
            {QStringLiteral("z"), request.z},
            {QStringLiteral("endX"), request.endX},
            {QStringLiteral("endY"), request.endY},
            {QStringLiteral("width"), request.width},
            {QStringLiteral("depth"), request.depth},
            {QStringLiteral("height"), request.height},
            {QStringLiteral("thickness"), request.thickness},
            {QStringLiteral("radius"), request.radius},
            {QStringLiteral("rise"), request.rise},
            {QStringLiteral("rotationDegrees"), request.rotationDegrees},
            {QStringLiteral("fdsConversionRoute"), request.fdsConversionRoute},
            {QStringLiteral("stepCount"), request.stepCount},
            {QStringLiteral("baseline"), static_cast<int>(request.baseline)},
            {QStringLiteral("profile"), pointsToVariant(request.profile)},
            {QStringLiteral("path"), pointsToVariant(request.path)}};
}

BuildingGeometryRequest BuildingGeometryService::requestFromParameters(
    FcGeometryKind kind, const QVariantMap& parameters)
{
    BuildingGeometryRequest request;
    request.kind = kind;
    const auto number = [&parameters](const char* key, double fallback) {
        const QVariant value = parameters.value(QString::fromLatin1(key));
        return value.isValid() ? value.toDouble() : fallback;
    };
    request.x = number("x", request.x);
    request.y = number("y", request.y);
    request.z = number("z", request.z);
    request.endX = number("endX", request.endX);
    request.endY = number("endY", request.endY);
    request.width = number("width", request.width);
    request.depth = number("depth", request.depth);
    request.height = number("height", request.height);
    request.thickness = number("thickness", request.thickness);
    request.radius = number("radius", request.radius);
    request.rise = number("rise", request.rise);
    request.rotationDegrees = number("rotationDegrees", request.rotationDegrees);
    request.fdsConversionRoute = parameters.value(
        QStringLiteral("fdsConversionRoute"), request.fdsConversionRoute).toString();
    request.stepCount = parameters.value(QStringLiteral("stepCount"), request.stepCount).toInt();
    request.baseline = static_cast<FcWallBaseline>(
        parameters.value(QStringLiteral("baseline"), static_cast<int>(request.baseline)).toInt());
    request.profile = pointsFromVariant(parameters.value(QStringLiteral("profile")));
    request.path = pointsFromVariant(parameters.value(QStringLiteral("path")));
    return request;
}

GeometryValidationResult BuildingGeometryService::validate(const TopoDS_Shape& shape)
{
    GeometryValidationResult result;
    if (shape.IsNull()) {
        result.errors.append(QStringLiteral("Geometry is empty."));
        return result;
    }
    try {
        BRepCheck_Analyzer analyzer(shape, true);
        result.valid = analyzer.IsValid();
        if (!result.valid) result.errors.append(QStringLiteral("OpenCascade topology validation failed."));
        int solidCount = 0;
        int shellCount = 0;
        for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
            ++solidCount;
        }
        for (TopExp_Explorer explorer(shape, TopAbs_SHELL); explorer.More(); explorer.Next()) {
            ++shellCount;
        }
        result.closed = solidCount > 0;
        if (!result.closed && shellCount > 0) {
            result.warnings.append(QStringLiteral("Geometry contains shells but no closed solid."));
        }
        for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
            GProp_GProps properties;
            BRepGProp::LinearProperties(explorer.Current(), properties);
            if (properties.Mass() <= kTolerance) {
                result.hasDegenerateEdges = true;
                break;
            }
        }
        if (result.hasDegenerateEdges) {
            result.errors.append(QStringLiteral("Geometry contains a degenerate edge."));
            result.valid = false;
        }
        TopTools_IndexedDataMapOfShapeListOfShape facesByEdge;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesByEdge);
        for (int index = 1; index <= facesByEdge.Extent(); ++index) {
            if (facesByEdge.FindFromIndex(index).Extent() > 2) {
                result.nonManifold = true;
                break;
            }
        }
        if (result.nonManifold) {
            result.errors.append(QStringLiteral("Geometry contains a non-manifold edge shared by more than two faces."));
            result.valid = false;
        }

        QSet<QString> vertexKeys;
        for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
            const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current()));
            ++result.vertexCount;
            const QString key = quantizedPointKey(point.X(), point.Y(), point.Z(), 1.0e-7);
            if (vertexKeys.contains(key)) ++result.duplicateVertexCount;
            else vertexKeys.insert(key);
        }
        result.hasDuplicateVertices = result.duplicateVertexCount > 0;
        if (result.hasDuplicateVertices) {
            result.warnings.append(
                QStringLiteral("Geometry topology contains %1 coincident vertex occurrence(s); repair can merge them.")
                    .arg(result.duplicateVertexCount));
        }

        QSet<QString> faceKeys;
        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
            ++result.faceCount;
            const QString key = faceSignature(TopoDS::Face(explorer.Current()), 1.0e-7);
            if (faceKeys.contains(key)) ++result.duplicateFaceCount;
            else faceKeys.insert(key);
        }
        result.hasDuplicateFaces = result.duplicateFaceCount > 0;
        if (result.hasDuplicateFaces) {
            result.errors.append(
                QStringLiteral("Geometry contains %1 duplicate topological face(s).")
                    .arg(result.duplicateFaceCount));
            result.valid = false;
        }
    } catch (const Standard_Failure& failure) {
        result.valid = false;
        result.errors.append(QStringLiteral("Geometry validation failed: %1")
                                 .arg(QString::fromLatin1(failure.GetMessageString())));
    }
    return result;
}

QVector<GeometryFaceInfo> BuildingGeometryService::faceInfos(
    const TopoDS_Shape& shape, double linearTolerance)
{
    QVector<GeometryFaceInfo> result;
    if (shape.IsNull()) return result;
    try {
        int index = 0;
        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
            GeometryFaceInfo info;
            info.faceIndex = ++index;
            info.key = faceSignature(TopoDS::Face(explorer.Current()),
                                     linearTolerance, &info);
            info.label = QStringLiteral("Face %1 — area %2 m², center (%3, %4, %5), normal (%6, %7, %8)")
                             .arg(info.faceIndex)
                             .arg(info.area, 0, 'g', 6)
                             .arg(info.center[0], 0, 'g', 5)
                             .arg(info.center[1], 0, 'g', 5)
                             .arg(info.center[2], 0, 'g', 5)
                             .arg(info.normal[0], 0, 'f', 3)
                             .arg(info.normal[1], 0, 'f', 3)
                             .arg(info.normal[2], 0, 'f', 3);
            result.append(info);
        }
    } catch (const Standard_Failure&) {
        result.clear();
    }
    return result;
}

FdsGeomTriangulation BuildingGeometryService::triangulateForFds(
    const TopoDS_Shape& shape, double deflection)
{
    FdsGeomTriangulation result;
    if (shape.IsNull()) {
        result.warnings.append(QStringLiteral("Cannot triangulate empty geometry."));
        return result;
    }
    try {
        BRepMesh_IncrementalMesh mesher(shape, std::max(deflection, 1.0e-6),
                                        Standard_False, 0.35, Standard_True);
        QHash<QString, int> vertexByPosition;
        int faceIndex = 0;
        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
            const TopoDS_Face face = TopoDS::Face(explorer.Current());
            ++faceIndex;
            GeometryFaceInfo info;
            info.faceIndex = faceIndex;
            const QString faceKey = faceSignature(face, 1.0e-6, &info);
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangles =
                BRep_Tool::Triangulation(face, location);
            if (triangles.IsNull()) {
                result.warnings.append(
                    QStringLiteral("Face %1 could not be triangulated.").arg(faceIndex));
                continue;
            }
            const gp_Trsf transform = location.Transformation();
            const auto vertexIndex = [&](int sourceIndex) {
                const gp_Pnt point = triangles->Node(sourceIndex).Transformed(transform);
                const QString key = quantizedPointKey(
                    point.X(), point.Y(), point.Z(), 1.0e-8);
                const auto existing = vertexByPosition.constFind(key);
                if (existing != vertexByPosition.cend()) return existing.value();
                const int index = result.vertices.size() / 3 + 1;
                result.vertices.append(point.X());
                result.vertices.append(point.Y());
                result.vertices.append(point.Z());
                vertexByPosition.insert(key, index);
                return index;
            };
            for (int triangleIndex = 1;
                 triangleIndex <= triangles->NbTriangles(); ++triangleIndex) {
                int first = 0, second = 0, third = 0;
                triangles->Triangle(triangleIndex).Get(first, second, third);
                if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
                const std::array<int, 3> indices{
                    vertexIndex(first), vertexIndex(second), vertexIndex(third)};
                if (indices[0] == indices[1] || indices[1] == indices[2] ||
                    indices[0] == indices[2]) {
                    result.warnings.append(
                        QStringLiteral("A degenerate triangle on face %1 was skipped.")
                            .arg(faceIndex));
                    continue;
                }
                result.triangles.append(indices);
                result.triangleFaceKeys.append(faceKey);
            }
        }
        if (!result.valid()) {
            result.warnings.append(QStringLiteral("Triangulation did not produce a valid FDS GEOM mesh."));
        }
    } catch (const Standard_Failure& failure) {
        result.vertices.clear();
        result.triangles.clear();
        result.triangleFaceKeys.clear();
        result.warnings.append(
            QStringLiteral("OpenCascade triangulation failed: %1")
                .arg(QString::fromLatin1(failure.GetMessageString())));
    }
    return result;
}

BuildingGeometryRequest BuildingGeometryService::rebaseWall(
    const BuildingGeometryRequest& request, FcWallBaseline baseline,
    bool preserveFootprint)
{
    BuildingGeometryRequest result = request;
    if (request.kind != FcGeometryKind::Wall || request.baseline == baseline) {
        result.baseline = baseline;
        return result;
    }
    if (preserveFootprint) {
        const double dx = request.endX - request.x;
        const double dy = request.endY - request.y;
        const double length = std::hypot(dx, dy);
        if (length > kTolerance) {
            const double normalX = -dy / length;
            const double normalY = dx / length;
            const double shift = baselineOffset(request.baseline, request.thickness) -
                                 baselineOffset(baseline, request.thickness);
            result.x += normalX * shift;
            result.y += normalY * shift;
            result.endX += normalX * shift;
            result.endY += normalY * shift;
            for (QPointF& point : result.path) {
                point += QPointF(normalX * shift, normalY * shift);
            }
        }
    }
    result.baseline = baseline;
    return result;
}

QPointF BuildingGeometryService::snapWallEndpoint(
    const QPointF& point,
    const QVector<std::shared_ptr<FcGeometryObject>>& existingWalls,
    double tolerance, QString* connectedWallId)
{
    if (connectedWallId) connectedWallId->clear();
    QPointF best = point;
    double bestDistance = std::max(0.0, tolerance);
    for (const auto& wall : existingWalls) {
        if (!wall || wall->geometryKind() != FcGeometryKind::Wall) continue;
        const BuildingGeometryRequest request = requestFromParameters(
            wall->geometryKind(), wall->geometryParameters());
        QVector<QPointF> endpoints;
        if (request.path.size() >= 2) {
            endpoints = request.path;
        } else {
            endpoints = {QPointF(request.x, request.y),
                         QPointF(request.endX, request.endY)};
        }
        for (const QPointF& endpoint : endpoints) {
            const double distance = std::hypot(endpoint.x() - point.x(),
                                               endpoint.y() - point.y());
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = endpoint;
                if (connectedWallId) *connectedWallId = wall->id();
            }
        }
        for (qsizetype segment = 1; segment < endpoints.size(); ++segment) {
            const QPointF first = endpoints[segment - 1];
            const QPointF second = endpoints[segment];
            const QPointF direction = second - first;
            const double lengthSquared = QPointF::dotProduct(direction, direction);
            if (lengthSquared <= kTolerance * kTolerance) continue;
            const double parameter = std::clamp(
                QPointF::dotProduct(point - first, direction) / lengthSquared,
                0.0, 1.0);
            const QPointF projection = first + direction * parameter;
            const double distance = std::hypot(projection.x() - point.x(),
                                               projection.y() - point.y());
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = projection;
                if (connectedWallId) *connectedWallId = wall->id();
            }
        }
    }
    return best;
}

BuildingGeometryRequest BuildingGeometryService::attachOpeningToWall(
    const BuildingGeometryRequest& opening, const BuildingGeometryRequest& wall)
{
    BuildingGeometryRequest result = opening;
    const double dx = wall.endX - wall.x;
    const double dy = wall.endY - wall.y;
    const double length = std::hypot(dx, dy);
    if (length <= kTolerance) return result;
    const double tangentX = dx / length;
    const double tangentY = dy / length;
    const double normalX = -tangentY;
    const double normalY = tangentX;
    const double localU = (opening.x - wall.x) * tangentX +
                          (opening.y - wall.y) * tangentY;
    const double localV = (opening.x - wall.x) * normalX +
                          (opening.y - wall.y) * normalY;
    result.x = wall.x + tangentX * localU + normalX * localV;
    result.y = wall.y + tangentY * localU + normalY * localV;
    result.z = wall.z + (opening.z - wall.z);
    result.rotationDegrees = std::atan2(dy, dx) * 180.0 / std::acos(-1.0);
    return result;
}

BuildingGeometryRequest BuildingGeometryService::followEditedHostWall(
    const BuildingGeometryRequest& opening,
    const BuildingGeometryRequest& oldWall,
    const BuildingGeometryRequest& newWall)
{
    const double oldDx = oldWall.endX - oldWall.x;
    const double oldDy = oldWall.endY - oldWall.y;
    const double newDx = newWall.endX - newWall.x;
    const double newDy = newWall.endY - newWall.y;
    const double oldLength = std::hypot(oldDx, oldDy);
    const double newLength = std::hypot(newDx, newDy);
    if (oldLength <= kTolerance || newLength <= kTolerance) return opening;
    const double oldTx = oldDx / oldLength;
    const double oldTy = oldDy / oldLength;
    const double oldNx = -oldTy;
    const double oldNy = oldTx;
    const double localU = (opening.x - oldWall.x) * oldTx +
                          (opening.y - oldWall.y) * oldTy;
    const double localV = (opening.x - oldWall.x) * oldNx +
                          (opening.y - oldWall.y) * oldNy;
    const double newTx = newDx / newLength;
    const double newTy = newDy / newLength;
    const double newNx = -newTy;
    const double newNy = newTx;
    BuildingGeometryRequest result = opening;
    result.x = newWall.x + newTx * localU + newNx * localV;
    result.y = newWall.y + newTy * localU + newNy * localV;
    result.z = newWall.z + (opening.z - oldWall.z);
    result.rotationDegrees = std::atan2(newDy, newDx) * 180.0 / std::acos(-1.0);
    return result;
}

TopoDS_Shape BuildingGeometryService::heal(const TopoDS_Shape& shape, QString* error)
{
    setError(error, {});
    if (shape.IsNull()) {
        setError(error, QStringLiteral("Cannot heal empty geometry."));
        return {};
    }
    try {
        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        ShapeUpgrade_UnifySameDomain unify(fixer.Shape(), true, true, true);
        unify.Build();
        const TopoDS_Shape healed = unify.Shape();
        if (!validate(healed).valid) {
            setError(error, QStringLiteral("The healed geometry is still invalid."));
            return {};
        }
        return healed;
    } catch (const Standard_Failure& failure) {
        setError(error, QStringLiteral("Geometry repair failed: %1")
                            .arg(QString::fromLatin1(failure.GetMessageString())));
        return {};
    }
}

TopoDS_Shape BuildingGeometryService::booleanOperation(
    const TopoDS_Shape& first, const TopoDS_Shape& second,
    FcBooleanOperation operation, QString* error)
{
    setError(error, {});
    if (first.IsNull() || second.IsNull()) {
        setError(error, QStringLiteral("Boolean operands must both contain geometry."));
        return {};
    }
    try {
        TopoDS_Shape result;
        switch (operation) {
        case FcBooleanOperation::Union: {
            BRepAlgoAPI_Fuse algorithm(first, second);
            algorithm.SetFuzzyValue(1.0e-7);
            algorithm.Build();
            if (algorithm.IsDone()) result = algorithm.Shape();
            break;
        }
        case FcBooleanOperation::Difference:
        case FcBooleanOperation::Split: {
            BRepAlgoAPI_Cut algorithm(first, second);
            algorithm.SetFuzzyValue(1.0e-7);
            algorithm.Build();
            if (algorithm.IsDone()) result = algorithm.Shape();
            break;
        }
        case FcBooleanOperation::Intersection: {
            BRepAlgoAPI_Common algorithm(first, second);
            algorithm.SetFuzzyValue(1.0e-7);
            algorithm.Build();
            if (algorithm.IsDone()) result = algorithm.Shape();
            break;
        }
        }
        if (result.IsNull()) {
            setError(error, QStringLiteral("The boolean operation produced no result."));
            return {};
        }
        return heal(result, error);
    } catch (const Standard_Failure& failure) {
        setError(error, QStringLiteral("Boolean operation failed: %1")
                            .arg(QString::fromLatin1(failure.GetMessageString())));
    }
    return {};
}

bool BuildingGeometryService::isOpeningKind(FcGeometryKind kind)
{
    return kind == FcGeometryKind::RectangularOpening ||
           kind == FcGeometryKind::PolygonalOpening ||
           kind == FcGeometryKind::Door || kind == FcGeometryKind::Window ||
           kind == FcGeometryKind::SlabOpening || kind == FcGeometryKind::WallVent;
}
