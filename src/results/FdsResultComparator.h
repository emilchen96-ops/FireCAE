#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

struct FdsCsvQuantityComparison
{
    QString fileKey;
    QString referenceFile;
    QString candidateFile;
    QString quantity;
    int sampleCount = 0;
    double referenceFinal = 0.0;
    double candidateFinal = 0.0;
    double referencePeak = 0.0;
    double candidatePeak = 0.0;
    double referencePeakTime = 0.0;
    double candidatePeakTime = 0.0;
    double referenceMean = 0.0;
    double candidateMean = 0.0;
    double relativeFinalError = 0.0;
    double rootMeanSquareError = 0.0;
    double normalizedRootMeanSquareError = 0.0;
    double maximumAbsoluteError = 0.0;
    bool withinTolerance = false;
    QVector<double> sampleTimes;
    QVector<double> referenceSamples;
    QVector<double> candidateSamples;
    QString unit;
    QString timeUnit;
};

struct FdsResultProvenance
{
    QString fdsInputFile;
    QString fdsInputSha256;
    QString outputLogFile;
    QString solverRevision;
    QString completedTime;
    QString simulationEndTime;
    bool normalTermination = false;
};

struct FdsResultComparison
{
    QString referenceCase;
    QString candidateCase;
    QString referenceSmvFile;
    QString candidateSmvFile;
    double relativeTolerance = 0.05;
    double absoluteTolerance = 1.0e-6;
    FdsResultProvenance referenceProvenance;
    FdsResultProvenance candidateProvenance;
    int matchedFileCount = 0;
    QStringList missingCandidateFiles;
    QStringList missingCandidateQuantities;
    QStringList candidateOnlyFiles;
    QStringList warnings;
    std::vector<FdsCsvQuantityComparison> quantities;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
    bool provenanceComparable() const;
    QStringList provenanceIssues() const;
    bool passed() const;
    QString csvReport() const;
    QString htmlReport() const;
};

class FdsResultComparator final
{
public:
    static FdsResultComparison compareSmvFiles(
        const QString& referenceSmvFile,
        const QString& candidateSmvFile,
        double relativeTolerance = 0.05,
        double absoluteTolerance = 1.0e-6);
};
