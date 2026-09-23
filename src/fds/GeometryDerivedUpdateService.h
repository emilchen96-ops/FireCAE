#pragma once

#include "fds/FcFdsModel.h"
#include <QVector>
#include <memory>

class FcDocument;
class FcGeometryObject;

struct GeometryDerivedUpdate
{
    std::shared_ptr<FcFdsNamelist> target;
    std::vector<FcFdsParameter> beforeParameters;
    std::vector<FcFdsParameter> afterParameters;
};

struct GeometryDerivedUpdatePlan
{
    QVector<GeometryDerivedUpdate> updates;
    QStringList warnings;
    QStringList errors;
    bool success() const { return errors.isEmpty(); }
};

// Plans an atomic update of already converted FDS records, without mutation.
// Target UUIDs, FDS IDs, names, groups and incoming references remain intact.
class GeometryDerivedUpdateService final
{
public:
    static GeometryDerivedUpdatePlan plan(
        const FcDocument& document,
        const std::shared_ptr<FcGeometryObject>& sourceBefore,
        const std::shared_ptr<FcGeometryObject>& sourceAfter,
        const QVector<std::shared_ptr<FcFdsMesh>>& meshes);
};
