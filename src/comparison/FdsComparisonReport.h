#pragma once

#include "comparison/FdsInputComparator.h"
#include "results/FdsResultComparator.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct FdsComparisonReportOptions
{
    QString outputDirectory;
    QString baseFileName = QStringLiteral("firecae-comparison");
    QString title = QStringLiteral("FireCAE / FDS comparison report");
    QString fireCaeVersion;
    QString fdsVersion;
    QString solveEnvironment;
    QString runTimeSeconds;
    QStringList screenshots;
    QStringList reproduciblePaths;
};

struct FdsComparisonReportArtifacts
{
    QString htmlFile;
    QString pdfFile;
    QString csvFile;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
};

class FdsComparisonReport final
{
public:
    static FdsComparisonReportArtifacts write(
        const FdsInputComparison* inputComparison,
        const FdsResultComparison& resultComparison,
        const QVector<FdsUuidIdMapping>& uuidMappings,
        const FdsComparisonReportOptions& options);
};
