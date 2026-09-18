#include "import/GeometryImportService.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <Message_ProgressRange.hxx>
#include <Poly_Triangulation.hxx>
#include <RWGltf_CafReader.hxx>
#include <RWMesh_CafReader.hxx>
#include <RWObj_CafReader.hxx>
#include <RWStl.hxx>
#include <STEPControl_Reader.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace
{
const double kPi = std::acos(-1.0);

struct CafImportMetadata
{
    int materialCount = 0;
    int textureCount = 0;
    bool hasRepresentativeColor = false;
    double red = 0.82;
    double green = 0.84;
    double blue = 0.88;
    double alpha = 1.0;
};

double unitScale(const QString& unit)
{
    const QString value = unit.trimmed().toLower();
    if (value == QStringLiteral("mm")) return 0.001;
    if (value == QStringLiteral("cm")) return 0.01;
    if (value == QStringLiteral("ft") || value == QStringLiteral("feet")) return 0.3048;
    if (value == QStringLiteral("in") || value == QStringLiteral("inch")) return 0.0254;
    return 1.0;
}

TopoDS_Shape documentShape(const Handle(TDocStd_Document)& document)
{
    if (document.IsNull()) return {};
    const Handle(XCAFDoc_ShapeTool) shapeTool =
        XCAFDoc_DocumentTool::ShapeTool(document->Main());
    if (shapeTool.IsNull()) return {};
    TDF_LabelSequence roots;
    shapeTool->GetFreeShapes(roots);
    if (roots.IsEmpty()) return {};
    if (roots.Length() == 1) return shapeTool->GetShape(roots.Value(1));
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (Standard_Integer index = 1; index <= roots.Length(); ++index) {
        const TopoDS_Shape shape = shapeTool->GetShape(roots.Value(index));
        if (!shape.IsNull()) builder.Add(compound, shape);
    }
    return compound;
}

TopoDS_Shape readCafMesh(RWMesh_CafReader& reader, const QString& filePath,
                         CafImportMetadata* metadata)
{
    Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
    reader.SetDocument(document);
    reader.SetSystemLengthUnit(1.0);
    if (!reader.Perform(TCollection_AsciiString(filePath.toUtf8().constData()),
                        Message_ProgressRange())) {
        return {};
    }
    if (metadata) {
        const Handle(XCAFDoc_VisMaterialTool) materialTool =
            XCAFDoc_DocumentTool::VisMaterialTool(document->Main());
        if (!materialTool.IsNull()) {
            TDF_LabelSequence labels;
            materialTool->GetMaterials(labels);
            metadata->materialCount = labels.Length();
            for (Standard_Integer index = 1; index <= labels.Length(); ++index) {
                const Handle(XCAFDoc_VisMaterial) material =
                    XCAFDoc_VisMaterialTool::GetMaterial(labels.Value(index));
                if (material.IsNull()) continue;
                const Quantity_ColorRGBA color = material->BaseColor();
                if (!metadata->hasRepresentativeColor) {
                    metadata->hasRepresentativeColor = true;
                    metadata->red = color.GetRGB().Red();
                    metadata->green = color.GetRGB().Green();
                    metadata->blue = color.GetRGB().Blue();
                    metadata->alpha = color.Alpha();
                }
                if (material->HasPbrMaterial()) {
                    const XCAFDoc_VisMaterialPBR& pbr = material->PbrMaterial();
                    metadata->textureCount += !pbr.BaseColorTexture.IsNull();
                    metadata->textureCount += !pbr.MetallicRoughnessTexture.IsNull();
                    metadata->textureCount += !pbr.EmissiveTexture.IsNull();
                    metadata->textureCount += !pbr.OcclusionTexture.IsNull();
                    metadata->textureCount += !pbr.NormalTexture.IsNull();
                }
            }
        }
    }
    return documentShape(document);
}

TopoDS_Shape readShape(const QString& filePath, GeometryImportFormat format,
                       QString* errorMessage, CafImportMetadata* metadata)
{
    const QByteArray nativePath = QFile::encodeName(filePath);
    if (format == GeometryImportFormat::Stl) {
        const Handle(Poly_Triangulation) triangulation =
            RWStl::ReadFile(nativePath.constData());
        if (triangulation.IsNull()) {
            *errorMessage = QStringLiteral("OpenCascade could not read the STL triangulation.");
            return {};
        }
        BRep_Builder builder;
        TopoDS_Face face;
        builder.MakeFace(face, triangulation);
        return face;
    }
    if (format == GeometryImportFormat::Obj) {
        RWObj_CafReader reader;
        const TopoDS_Shape shape = readCafMesh(reader, filePath, metadata);
        if (shape.IsNull()) *errorMessage = QStringLiteral("OpenCascade could not read the OBJ file.");
        return shape;
    }
    if (format == GeometryImportFormat::Gltf || format == GeometryImportFormat::Glb) {
        RWGltf_CafReader reader;
        const TopoDS_Shape shape = readCafMesh(reader, filePath, metadata);
        if (shape.IsNull()) *errorMessage = QStringLiteral("OpenCascade could not read the glTF/GLB file.");
        return shape;
    }
    if (format == GeometryImportFormat::Step) {
        STEPControl_Reader reader;
        if (reader.ReadFile(nativePath.constData()) != IFSelect_RetDone ||
            reader.TransferRoots() <= 0) {
            *errorMessage = QStringLiteral("OpenCascade could not translate the STEP file.");
            return {};
        }
        return reader.OneShape();
    }
    if (format == GeometryImportFormat::Iges) {
        IGESControl_Reader reader;
        if (reader.ReadFile(nativePath.constData()) != IFSelect_RetDone ||
            reader.TransferRoots() <= 0) {
            *errorMessage = QStringLiteral("OpenCascade could not translate the IGES file.");
            return {};
        }
        return reader.OneShape();
    }
    if (format == GeometryImportFormat::Dxf) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *errorMessage = QStringLiteral("Could not read the ASCII DXF file.");
            return {};
        }
        const QByteArray firstBytes = file.peek(22);
        if (firstBytes.startsWith("AutoCAD Binary DXF")) {
            *errorMessage = QStringLiteral("Binary DXF is not supported; save it as ASCII DXF first.");
            return {};
        }
        QTextStream stream(&file);
        struct Pair { int code = 0; QString value; };
        QList<Pair> pairs;
        while (!stream.atEnd()) {
            bool ok = false;
            const int code = stream.readLine().trimmed().toInt(&ok);
            if (stream.atEnd()) break;
            const QString value = stream.readLine().trimmed();
            if (ok) pairs.append({code, value});
        }
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        int createdEntities = 0;
        for (qsizetype index = 0; index < pairs.size();) {
            if (pairs[index].code != 0) { ++index; continue; }
            const QString entity = pairs[index].value.toUpper();
            const qsizetype begin = ++index;
            while (index < pairs.size() && pairs[index].code != 0) ++index;
            const qsizetype end = index;
            auto coordinate = [&](int groupCode, double fallback = 0.0) {
                for (qsizetype pairIndex = begin; pairIndex < end; ++pairIndex) {
                    if (pairs[pairIndex].code == groupCode) {
                        bool ok = false;
                        const double value = pairs[pairIndex].value.toDouble(&ok);
                        return ok ? value : fallback;
                    }
                }
                return fallback;
            };
            if (entity == QStringLiteral("3DFACE")) {
                const gp_Pnt p1(coordinate(10), coordinate(20), coordinate(30));
                const gp_Pnt p2(coordinate(11), coordinate(21), coordinate(31));
                const gp_Pnt p3(coordinate(12), coordinate(22), coordinate(32));
                const gp_Pnt p4(coordinate(13, p3.X()), coordinate(23, p3.Y()),
                                coordinate(33, p3.Z()));
                BRepBuilderAPI_MakePolygon polygon;
                polygon.Add(p1); polygon.Add(p2); polygon.Add(p3);
                if (p4.Distance(p3) > 1.0e-12) polygon.Add(p4);
                polygon.Close();
                if (polygon.IsDone()) {
                    BRepBuilderAPI_MakeFace face(polygon.Wire());
                    if (face.IsDone()) { builder.Add(compound, face.Face()); ++createdEntities; }
                }
            } else if (entity == QStringLiteral("LINE")) {
                const gp_Pnt p1(coordinate(10), coordinate(20), coordinate(30));
                const gp_Pnt p2(coordinate(11), coordinate(21), coordinate(31));
                if (p1.Distance(p2) > 1.0e-12) {
                    BRepBuilderAPI_MakeEdge edge(p1, p2);
                    if (edge.IsDone()) { builder.Add(compound, edge.Edge()); ++createdEntities; }
                }
            } else if (entity == QStringLiteral("LWPOLYLINE")) {
                QList<gp_Pnt> points;
                const double elevation = coordinate(38);
                double pendingX = 0.0;
                bool hasX = false;
                int flags = 0;
                for (qsizetype pairIndex = begin; pairIndex < end; ++pairIndex) {
                    const Pair& pair = pairs[pairIndex];
                    if (pair.code == 70) flags = pair.value.toInt();
                    else if (pair.code == 10) {
                        pendingX = pair.value.toDouble();
                        hasX = true;
                    } else if (pair.code == 20 && hasX) {
                        points.append(gp_Pnt(pendingX, pair.value.toDouble(), elevation));
                        hasX = false;
                    }
                }
                if (points.size() >= 2) {
                    BRepBuilderAPI_MakePolygon polygon;
                    for (const gp_Pnt& point : points) polygon.Add(point);
                    if ((flags & 1) != 0) polygon.Close();
                    if (polygon.IsDone()) { builder.Add(compound, polygon.Wire()); ++createdEntities; }
                }
            }
        }
        if (createdEntities == 0) {
            *errorMessage = QStringLiteral(
                "The ASCII DXF contains no supported LINE, LWPOLYLINE, or 3DFACE entities.");
            return {};
        }
        return compound;
    }
    *errorMessage = GeometryImportService::unavailableReason(format);
    return {};
}

TopoDS_Shape transformedShape(const TopoDS_Shape& source,
                              const GeometryImportOptions& options)
{
    const TopExp_Explorer vertexExplorer(source, TopAbs_VERTEX);
    const bool hasTopologicalVertex = vertexExplorer.More();
    if (!hasTopologicalVertex) {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        bool addedTriangulation = false;
        for (TopExp_Explorer explorer(source, TopAbs_FACE); explorer.More(); explorer.Next()) {
            TopLoc_Location location;
            const Handle(Poly_Triangulation) sourceTriangles =
                BRep_Tool::Triangulation(TopoDS::Face(explorer.Current()), location);
            if (sourceTriangles.IsNull()) continue;
            const Handle(Poly_Triangulation) triangles = sourceTriangles->Copy();
            gp_Trsf rotation;
            if (options.upAxis == GeometryUpAxis::YUp) {
                rotation.SetRotation(
                    gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
                    kPi / 2.0);
            }
            const double scale = unitScale(options.sourceUnit) * options.customScale;
            for (Standard_Integer node = 1; node <= triangles->NbNodes(); ++node) {
                gp_Pnt point = triangles->Node(node);
                point.Transform(location.Transformation());
                point.SetCoord(point.X() * scale, point.Y() * scale, point.Z() * scale);
                if (options.upAxis == GeometryUpAxis::YUp) point.Transform(rotation);
                point.Translate(gp_Vec(options.originX, options.originY, options.originZ));
                triangles->SetNode(node, point);
                if (triangles->HasNormals() && options.upAxis == GeometryUpAxis::YUp) {
                    gp_Dir normal = triangles->Normal(node);
                    normal.Transform(rotation);
                    triangles->SetNormal(node, normal);
                }
            }
            triangles->UpdateCachedMinMax();
            TopoDS_Face transformedFace;
            builder.MakeFace(transformedFace, triangles);
            transformedFace.Orientation(explorer.Current().Orientation());
            builder.Add(compound, transformedFace);
            addedTriangulation = true;
        }
        if (addedTriangulation) return compound;
    }

    TopoDS_Shape shape = source;
    const double scale = unitScale(options.sourceUnit) * options.customScale;
    if (std::abs(scale - 1.0) > 1.0e-12) {
        gp_Trsf scaling;
        scaling.SetScale(gp_Pnt(0.0, 0.0, 0.0), scale);
        shape = BRepBuilderAPI_Transform(shape, scaling, Standard_True).Shape();
    }
    if (options.upAxis == GeometryUpAxis::YUp) {
        gp_Trsf rotation;
        rotation.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
                             kPi / 2.0);
        shape = BRepBuilderAPI_Transform(shape, rotation, Standard_True).Shape();
    }
    if (options.originX != 0.0 || options.originY != 0.0 || options.originZ != 0.0) {
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(options.originX, options.originY, options.originZ));
        shape = BRepBuilderAPI_Transform(shape, translation, Standard_True).Shape();
    }
    return shape;
}

int countSubShapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    int count = 0;
    for (TopExp_Explorer explorer(shape, type); explorer.More(); explorer.Next()) ++count;
    return count;
}

GeometryQualityReport inspectShape(TopoDS_Shape shape,
                                   const GeometryImportOptions& options,
                                   qint64 sourceBytes,
                                   qint64 elapsedMilliseconds)
{
    GeometryQualityReport report;
    report.sourceBytes = sourceBytes;
    report.elapsedMilliseconds = elapsedMilliseconds;
    report.solids = countSubShapes(shape, TopAbs_SOLID);
    report.shells = countSubShapes(shape, TopAbs_SHELL);
    report.faces = countSubShapes(shape, TopAbs_FACE);
    report.vertices = countSubShapes(shape, TopAbs_VERTEX);
    report.validTopology = BRepCheck_Analyzer(shape).IsValid();
    report.closed = shape.Closed() || report.solids > 0;

    const double deflection = std::max(1.0e-7, options.linearDeflection);
    BRepMesh_IncrementalMesh mesh(shape, deflection, Standard_False,
                                  options.angularDeflectionDegrees * kPi / 180.0,
                                  Standard_True);
    QHash<QString, int> vertexUse;
    QHash<QString, int> edgeUse;
    QSet<QString> triangleKeys;
    qint64 nodeReferences = 0;
    const auto pointKey = [](const gp_Pnt& point) {
        constexpr double tolerance = 1.0e-8;
        return QStringLiteral("%1,%2,%3")
            .arg(qRound64(point.X() / tolerance))
            .arg(qRound64(point.Y() / tolerance))
            .arg(qRound64(point.Z() / tolerance));
    };
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangles =
            BRep_Tool::Triangulation(TopoDS::Face(explorer.Current()), location);
        if (triangles.IsNull()) continue;
        report.triangles += triangles->NbTriangles();
        QVector<QString> nodeKeys(triangles->NbNodes() + 1);
        QVector<gp_Pnt> nodes(triangles->NbNodes() + 1);
        for (Standard_Integer node = 1; node <= triangles->NbNodes(); ++node) {
            gp_Pnt point = triangles->Node(node);
            point.Transform(location.Transformation());
            nodes[node] = point;
            nodeKeys[node] = pointKey(point);
            ++vertexUse[nodeKeys[node]];
            ++nodeReferences;
        }
        for (Standard_Integer triangle = 1;
             triangle <= triangles->NbTriangles(); ++triangle) {
            Standard_Integer n1 = 0, n2 = 0, n3 = 0;
            triangles->Triangle(triangle).Get(n1, n2, n3);
            QStringList vertexKeys{nodeKeys[n1], nodeKeys[n2], nodeKeys[n3]};
            std::sort(vertexKeys.begin(), vertexKeys.end());
            const QString triangleKey = vertexKeys.join(QLatin1Char('|'));
            if (triangleKeys.contains(triangleKey)) ++report.duplicateFaces;
            else triangleKeys.insert(triangleKey);
            const gp_Vec first(nodes[n1], nodes[n2]);
            const gp_Vec second(nodes[n1], nodes[n3]);
            if (first.Crossed(second).SquareMagnitude() <= 1.0e-20)
                ++report.degenerateTriangles;
            const int edges[3][2] = {{n1, n2}, {n2, n3}, {n3, n1}};
            for (const auto& edge : edges) {
                QString firstKey = nodeKeys[edge[0]];
                QString secondKey = nodeKeys[edge[1]];
                if (secondKey < firstKey) std::swap(firstKey, secondKey);
                ++edgeUse[firstKey + QLatin1Char('|') + secondKey];
            }
        }
    }
    report.duplicateVertices = std::max<qint64>(0, nodeReferences - vertexUse.size());
    for (auto iterator = edgeUse.cbegin(); iterator != edgeUse.cend(); ++iterator) {
        if (iterator.value() == 1) ++report.boundaryEdges;
        else if (iterator.value() > 2) ++report.nonManifoldEdges;
    }
    if (report.triangles > 0 && report.boundaryEdges == 0 &&
        report.nonManifoldEdges == 0) {
        report.closed = true;
    }

    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (!bounds.IsVoid()) {
        bounds.Get(report.minimumX, report.minimumY, report.minimumZ,
                   report.maximumX, report.maximumY, report.maximumZ);
        const double largest = std::max({std::abs(report.minimumX), std::abs(report.minimumY),
                                         std::abs(report.minimumZ), std::abs(report.maximumX),
                                         std::abs(report.maximumY), std::abs(report.maximumZ)});
        if (largest > 100000.0)
            report.warnings.append(QStringLiteral("Coordinates exceed 100 km; verify source units and origin."));
        const double span = std::max({report.maximumX - report.minimumX,
                                      report.maximumY - report.minimumY,
                                      report.maximumZ - report.minimumZ});
        if (span < 0.001 || span > 10000.0)
            report.warnings.append(QStringLiteral("Geometry size is unusual; verify the selected source unit."));
    }
    if (!report.validTopology)
        report.warnings.append(QStringLiteral("OpenCascade reports invalid topology."));
    if (report.duplicateFaces > 0)
        report.warnings.append(QStringLiteral("%1 duplicate triangle faces detected.")
                                   .arg(report.duplicateFaces));
    if (report.degenerateTriangles > 0)
        report.warnings.append(QStringLiteral("%1 degenerate triangles detected.")
                                   .arg(report.degenerateTriangles));
    if (report.nonManifoldEdges > 0)
        report.warnings.append(QStringLiteral("%1 non-manifold mesh edges detected.")
                                   .arg(report.nonManifoldEdges));
    if (report.boundaryEdges > 0)
        report.warnings.append(QStringLiteral("%1 open boundary edges detected.")
                                   .arg(report.boundaryEdges));
    if (!report.closed)
        report.warnings.append(QStringLiteral("Geometry is not closed; GEOM conversion may be safer than OBST."));
    return report;
}
}

GeometryImportResult GeometryImportService::importFile(
    const QString& filePath, const GeometryImportOptions& options,
    const GeometryImportProgressCallback& progress,
    const GeometryImportCancellationCheck& cancelled) const
{
    GeometryImportResult result;
    const auto reportProgress = [&progress](int percent, const QString& stage) {
        if (progress) progress(qBound(0, percent, 100), stage);
    };
    const auto stopIfCancelled = [&result, &cancelled]() {
        if (!cancelled || !cancelled()) return false;
        result.cancelled = true;
        result.errorMessage = QStringLiteral("Geometry import cancelled.");
        return true;
    };
    reportProgress(2, QStringLiteral("Checking source file"));
    const QFileInfo source(filePath);
    result.format = detectFormat(filePath);
    if (!source.exists() || !source.isFile()) {
        result.errorMessage = QStringLiteral("Geometry file does not exist: %1").arg(filePath);
        return result;
    }
    if (!isSupported(result.format)) {
        result.errorMessage = unavailableReason(result.format);
        return result;
    }
    if (stopIfCancelled()) return result;
    QElapsedTimer timer;
    timer.start();
    CafImportMetadata cafMetadata;
    reportProgress(12, QStringLiteral("Reading %1 geometry")
                           .arg(formatName(result.format)));
    TopoDS_Shape shape = readShape(source.absoluteFilePath(), result.format,
                                   &result.errorMessage, &cafMetadata);
    if (shape.IsNull()) return result;
    reportProgress(48, QStringLiteral("Applying units and coordinates"));
    if (stopIfCancelled()) return result;
    shape = transformedShape(shape, options);
    if (shape.IsNull()) {
        result.errorMessage = QStringLiteral("The import transform produced an empty shape.");
        return result;
    }
    reportProgress(62, QStringLiteral("Inspecting topology and triangulation"));
    if (stopIfCancelled()) return result;
    result.quality = inspectShape(shape, options, source.size(), timer.elapsed());
    if (stopIfCancelled()) return result;
    result.quality.materialCount = cafMetadata.materialCount;
    result.quality.textureCount = cafMetadata.textureCount;
    result.quality.hasRepresentativeColor = cafMetadata.hasRepresentativeColor;
    result.quality.representativeRed = cafMetadata.red;
    result.quality.representativeGreen = cafMetadata.green;
    result.quality.representativeBlue = cafMetadata.blue;
    result.quality.representativeAlpha = cafMetadata.alpha;
    result.warnings = result.quality.warnings;
    result.object = std::make_shared<FcGeometryObject>(source.completeBaseName(), shape);
    QVariantMap parameters;
    parameters.insert(QStringLiteral("sourceFile"), source.absoluteFilePath());
    parameters.insert(QStringLiteral("importFormat"), formatName(result.format));
    parameters.insert(QStringLiteral("sourceUnit"), options.sourceUnit);
    parameters.insert(QStringLiteral("customScale"), options.customScale);
    parameters.insert(QStringLiteral("upAxis"), options.upAxis == GeometryUpAxis::ZUp
                                                     ? QStringLiteral("Z") : QStringLiteral("Y"));
    parameters.insert(QStringLiteral("originX"), options.originX);
    parameters.insert(QStringLiteral("originY"), options.originY);
    parameters.insert(QStringLiteral("originZ"), options.originZ);
    parameters.insert(QStringLiteral("triangleCount"), result.quality.triangles);
    parameters.insert(QStringLiteral("duplicateVertexCount"), result.quality.duplicateVertices);
    parameters.insert(QStringLiteral("duplicateFaceCount"), result.quality.duplicateFaces);
    parameters.insert(QStringLiteral("degenerateTriangleCount"), result.quality.degenerateTriangles);
    parameters.insert(QStringLiteral("boundaryEdgeCount"), result.quality.boundaryEdges);
    parameters.insert(QStringLiteral("nonManifoldEdgeCount"), result.quality.nonManifoldEdges);
    parameters.insert(QStringLiteral("materialCount"), result.quality.materialCount);
    parameters.insert(QStringLiteral("textureCount"), result.quality.textureCount);
    if (options.preserveMaterials && result.quality.hasRepresentativeColor) {
        parameters.insert(QStringLiteral("displayColorRed"), result.quality.representativeRed);
        parameters.insert(QStringLiteral("displayColorGreen"), result.quality.representativeGreen);
        parameters.insert(QStringLiteral("displayColorBlue"), result.quality.representativeBlue);
        parameters.insert(QStringLiteral("displayOpacity"), result.quality.representativeAlpha);
    }
    parameters.insert(QStringLiteral("validTopology"), result.quality.validTopology);
    parameters.insert(QStringLiteral("closed"), result.quality.closed);
    parameters.insert(QStringLiteral("importMilliseconds"), result.quality.elapsedMilliseconds);
    result.object->setGeometryParameters(parameters);
    reportProgress(100, QStringLiteral("Import preview complete"));
    return result;
}

GeometryImportFormat GeometryImportService::detectFormat(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("ifc")) return GeometryImportFormat::Ifc;
    if (suffix == QStringLiteral("stl")) return GeometryImportFormat::Stl;
    if (suffix == QStringLiteral("obj")) return GeometryImportFormat::Obj;
    if (suffix == QStringLiteral("gltf")) return GeometryImportFormat::Gltf;
    if (suffix == QStringLiteral("glb")) return GeometryImportFormat::Glb;
    if (suffix == QStringLiteral("step") || suffix == QStringLiteral("stp")) return GeometryImportFormat::Step;
    if (suffix == QStringLiteral("iges") || suffix == QStringLiteral("igs")) return GeometryImportFormat::Iges;
    if (suffix == QStringLiteral("fds")) return GeometryImportFormat::Fds;
    if (suffix == QStringLiteral("fbx")) return GeometryImportFormat::Fbx;
    if (suffix == QStringLiteral("dae")) return GeometryImportFormat::Dae;
    if (suffix == QStringLiteral("dxf")) return GeometryImportFormat::Dxf;
    if (suffix == QStringLiteral("dwg")) return GeometryImportFormat::Dwg;
    return GeometryImportFormat::Unknown;
}

QString GeometryImportService::formatName(GeometryImportFormat format)
{
    switch (format) {
    case GeometryImportFormat::Ifc: return QStringLiteral("IFC");
    case GeometryImportFormat::Stl: return QStringLiteral("STL");
    case GeometryImportFormat::Obj: return QStringLiteral("OBJ");
    case GeometryImportFormat::Gltf: return QStringLiteral("glTF");
    case GeometryImportFormat::Glb: return QStringLiteral("GLB");
    case GeometryImportFormat::Step: return QStringLiteral("STEP");
    case GeometryImportFormat::Iges: return QStringLiteral("IGES");
    case GeometryImportFormat::Fds: return QStringLiteral("FDS");
    case GeometryImportFormat::Fbx: return QStringLiteral("FBX");
    case GeometryImportFormat::Dae: return QStringLiteral("DAE");
    case GeometryImportFormat::Dxf: return QStringLiteral("DXF");
    case GeometryImportFormat::Dwg: return QStringLiteral("DWG");
    default: return QStringLiteral("Unknown");
    }
}

bool GeometryImportService::isSupported(GeometryImportFormat format)
{
    return format == GeometryImportFormat::Stl || format == GeometryImportFormat::Obj ||
           format == GeometryImportFormat::Gltf || format == GeometryImportFormat::Glb ||
           format == GeometryImportFormat::Step || format == GeometryImportFormat::Iges ||
           format == GeometryImportFormat::Dxf;
}

QString GeometryImportService::unavailableReason(GeometryImportFormat format)
{
    if (format == GeometryImportFormat::Ifc)
        return QStringLiteral("IFC files use the dedicated IfcOpenShell import path.");
    if (format == GeometryImportFormat::Fds)
        return QStringLiteral("FDS files use File > Import FDS Input.");
    if (format == GeometryImportFormat::Dwg)
        return QStringLiteral("DWG import is disabled because no licensed DWG SDK is installed.");
    if (format == GeometryImportFormat::Fbx || format == GeometryImportFormat::Dae)
        return QStringLiteral("%1 import is disabled because the Assimp plug-in is not installed.")
            .arg(formatName(format));
    return QStringLiteral("This geometry format is not supported.");
}

QString GeometryImportService::openFileFilter()
{
    return QStringLiteral(
        "Supported geometry (*.ifc *.stl *.obj *.gltf *.glb *.step *.stp *.iges *.igs *.dxf);;"
        "Industry Foundation Classes (*.ifc);;"
        "Triangle meshes (*.stl *.obj *.gltf *.glb);;"
        "CAD exchange (*.step *.stp *.iges *.igs *.dxf);;All files (*.*)");
}
