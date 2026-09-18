#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

#include <functional>
#include <memory>

class FcIfcObject;

struct IfcImportOptions
{
    double additionalScale = 1.0;
    bool sourceYAxisUp = false;
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    QStringList includedClasses;
    QString mergeStrategy = QStringLiteral("PRESERVE_HIERARCHY");
    QString simplification = QStringLiteral("NONE");
    QString conversionRoute = QStringLiteral("REFERENCE");
    bool preserveMaterials = true;
    bool preservePropertySets = true;
    bool initiallyVisible = true;
};

struct IfcPreflightReport
{
    QString sourceFile;
    QString schema;
    QString lengthUnit;
    qint64 sourceBytes = 0;
    int entityCount = 0;
    int productCount = 0;
    int storeyCount = 0;
    int spaceCount = 0;
    int materialRelationshipCount = 0;
    int propertySetCount = 0;
    QMap<QString, int> classCounts;
    QStringList productClasses;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty() && entityCount > 0; }
};

using IfcImportProgressCallback =
    std::function<void(int percent, const QString& stage)>;
using IfcImportCancellationCheck = std::function<bool()>;

struct IfcImportResult
{
    std::shared_ptr<FcIfcObject> rootObject;
    int geometryObjectCount = 0;
    int filteredObjectCount = 0;
    IfcPreflightReport preflight;
    QStringList failedComponents;
    QString errorMessage;
    QStringList warnings;
    bool cancelled = false;

    bool success() const;
};

class IfcImportService final
{
public:
    explicit IfcImportService(const QString& converterPath = QString());

    IfcImportResult importFile(
        const QString& filePath,
        const IfcImportOptions& options = {},
        const IfcImportProgressCallback& progress = {},
        const IfcImportCancellationCheck& cancelled = {}) const;
    const QString& converterPath() const;

    static IfcPreflightReport inspectFile(const QString& filePath);
    static QString defaultConverterPath();

private:
    QString m_converterPath;
};
