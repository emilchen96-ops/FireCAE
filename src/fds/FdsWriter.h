#pragma once

#include <QString>
#include <QStringList>

#include <vector>

class FcProject;

struct FdsSourceMapEntry
{
    int line = 0;          // one-based
    int columnStart = 0;   // zero-based, inclusive
    int columnEnd = 0;     // zero-based, exclusive
    QString objectId;
    QString parameterKey;
};

struct FdsWriteResult
{
    QString text;
    QStringList errors;
    QStringList warnings;
    std::vector<FdsSourceMapEntry> sourceMap;

    bool success() const { return errors.isEmpty(); }
};

struct FdsMeshStatistic
{
    QString objectId;
    QString name;
    QString fdsId;
    qint64 instanceCount = 0;
    qint64 cellsPerInstance = 0;
    qint64 totalCellCount = 0;
};

struct FdsModelStatistics
{
    std::vector<FdsMeshStatistic> meshes;
    qint64 expandedMeshCount = 0;
    qint64 totalCellCount = 0;
    QStringList warnings;
};

class FdsWriter
{
public:
    static FdsModelStatistics modelStatistics(const FcProject& project);
    // Validate a prospective binding using the active scenario, without
    // mutating the document or requiring the rest of the model to be complete.
    static QStringList validateControlledTarget(const FcProject& project,
                                                const QString& objectId);
    static FdsWriteResult render(const FcProject& project);
    static bool writeFile(const FcProject& project,
                          const QString& filePath,
                          QString* errorMessage = nullptr);
};
