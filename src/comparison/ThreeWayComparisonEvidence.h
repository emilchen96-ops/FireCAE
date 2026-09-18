#pragma once

#include "comparison/FdsInputComparator.h"
#include "results/FdsResultComparator.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

enum class FdsEvidenceSourceKind
{
    NativeFds,
    FireCae,
    PyroSim
};

struct FdsEvidenceSource
{
    FdsEvidenceSourceKind kind = FdsEvidenceSourceKind::NativeFds;
    QString label;
    QString producer;
    QString smvFile;
    QString fdsFile;
    QString projectFile;
    QDateTime exportedAt;
};

struct FdsEvidenceValidation
{
    FdsEvidenceSource source;
    QString smvSha256;
    QString fdsSha256;
    QString projectSha256;
    QStringList pendingIssues;
    QStringList invalidIssues;
    QStringList warnings;

    bool ready() const
    {
        return pendingIssues.isEmpty() && invalidIssues.isEmpty();
    }
};

struct FdsThreeWayPairComparison
{
    FdsEvidenceSourceKind reference = FdsEvidenceSourceKind::NativeFds;
    FdsEvidenceSourceKind candidate = FdsEvidenceSourceKind::FireCae;
    FdsInputComparison inputComparison;
    FdsResultComparison resultComparison;
    bool attempted = false;

    bool passed() const
    {
        return attempted && inputComparison.success() &&
               inputComparison.semanticallyEquivalent() &&
               resultComparison.success() && resultComparison.passed();
    }
};

struct FdsThreeWayComparisonResult
{
    QString manifestFile;
    QString caseId;
    QVector<FdsEvidenceValidation> evidence;
    QVector<FdsThreeWayPairComparison> pairs;
    QStringList warnings;
    QString errorMessage;

    bool evidenceReady() const;
    bool comparisonsPassed() const;
    QString statusCode() const;
    QString markdownReport() const;
    QString jsonReport() const;
};

struct FdsThreeWayReportArtifacts
{
    QString markdownFile;
    QString jsonFile;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
};

class ThreeWayComparisonEvidence final
{
public:
    static FdsThreeWayComparisonResult loadAndCompare(const QString& manifestFile);
    static FdsThreeWayReportArtifacts writeReports(
        const FdsThreeWayComparisonResult& result,
        const QString& outputDirectory,
        const QString& baseFileName = QStringLiteral("three-way-comparison"));

    static QString sourceKey(FdsEvidenceSourceKind kind);
    static QString sourceDisplayName(FdsEvidenceSourceKind kind);
};
