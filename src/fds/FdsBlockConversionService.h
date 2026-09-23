#pragma once

#include "fds/FcFdsModel.h"

#include <QStringList>
#include <QVector>

#include <memory>

class FcGeometryObject;

enum class FdsBlockTarget
{
    Obstruction,
    Hole,
    Vent,
    Geom,
    RasterizedObstruction
};

struct FdsBlockPreview
{
    QString sourceObjectId;
    QString sourceName;
    FdsBlockTarget target = FdsBlockTarget::Obstruction;
    FcFdsBounds requested;
    FcFdsBounds actual;
    double requestedVolume = 0.0;
    double actualVolume = 0.0;
    double volumeErrorPercent = 0.0;
    bool lost = false;
    QStringList warnings;
};

struct FdsBlockConversionResult
{
    QVector<FdsBlockPreview> previews;
    QVector<FcObject::Ptr> fdsObjects;
    QStringList errors;
    QStringList warnings;

    bool success() const { return errors.isEmpty(); }
};

class FdsBlockConversionService final
{
public:
    static FdsBlockConversionResult convert(
        const QVector<std::shared_ptr<FcGeometryObject>>& geometry,
        const QVector<std::shared_ptr<FcFdsMesh>>& meshes,
        bool useGeomForArbitrary = false);
    // Returns untranslated complete error sentences, without the object name.
    // Validate using the same route policy as the eventual conversion.
    static QStringList validateSurfaceAssignments(
        const FcGeometryObject& geometry, bool useGeomForArbitrary = true);
    static FcFdsBounds boundsForShape(const FcGeometryObject& geometry,
                                      bool* valid = nullptr);
    static FcFdsBounds snapBounds(const FcFdsBounds& requested,
                                  const FcFdsMesh& mesh);
    static QString targetName(FdsBlockTarget target);
};
