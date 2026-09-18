#pragma once

#include "geometry/FcGeometryObject.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

enum class GeometryImportFormat
{
    Unknown,
    Ifc,
    Stl,
    Obj,
    Gltf,
    Glb,
    Step,
    Iges,
    Fds,
    Fbx,
    Dae,
    Dxf,
    Dwg
};

enum class GeometryUpAxis
{
    ZUp,
    YUp
};

struct GeometryImportOptions
{
    QString sourceUnit = QStringLiteral("Auto");
    double customScale = 1.0;
    GeometryUpAxis upAxis = GeometryUpAxis::ZUp;
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    double linearDeflection = 0.01;
    double angularDeflectionDegrees = 20.0;
    bool mergeObjects = true;
    bool preserveMaterials = true;
    bool preserveTextures = true;
};

struct GeometryQualityReport
{
    int solids = 0;
    int shells = 0;
    int faces = 0;
    int vertices = 0;
    qint64 triangles = 0;
    qint64 duplicateVertices = 0;
    qint64 duplicateFaces = 0;
    qint64 degenerateTriangles = 0;
    qint64 boundaryEdges = 0;
    qint64 nonManifoldEdges = 0;
    int materialCount = 0;
    int textureCount = 0;
    bool hasRepresentativeColor = false;
    double representativeRed = 0.82;
    double representativeGreen = 0.84;
    double representativeBlue = 0.88;
    double representativeAlpha = 1.0;
    double minimumX = 0.0;
    double minimumY = 0.0;
    double minimumZ = 0.0;
    double maximumX = 0.0;
    double maximumY = 0.0;
    double maximumZ = 0.0;
    bool validTopology = false;
    bool closed = false;
    qint64 elapsedMilliseconds = 0;
    qint64 sourceBytes = 0;
    QStringList warnings;
};

struct GeometryImportResult
{
    std::shared_ptr<FcGeometryObject> object;
    GeometryImportFormat format = GeometryImportFormat::Unknown;
    GeometryQualityReport quality;
    QString errorMessage;
    QStringList warnings;
    bool cancelled = false;

    bool success() const { return object != nullptr && errorMessage.isEmpty(); }
};

using GeometryImportProgressCallback =
    std::function<void(int percent, const QString& stage)>;
using GeometryImportCancellationCheck = std::function<bool()>;

class GeometryImportService final
{
public:
    GeometryImportResult importFile(const QString& filePath,
                                    const GeometryImportOptions& options = {},
                                    const GeometryImportProgressCallback& progress = {},
                                    const GeometryImportCancellationCheck& cancelled = {}) const;

    static GeometryImportFormat detectFormat(const QString& filePath);
    static QString formatName(GeometryImportFormat format);
    static bool isSupported(GeometryImportFormat format);
    static QString unavailableReason(GeometryImportFormat format);
    static QString openFileFilter();
};
