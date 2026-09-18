#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class FcProject;

struct FdsInputDifference
{
    QString category;
    QString recordKey;
    QString parameter;
    QString referenceValue;
    QString candidateValue;
    bool acceptable = false;
    QString explanation;
};

struct FdsUuidIdMapping
{
    QString uuid;
    QString keyword;
    QString fdsId;
    QString objectName;
};

struct FdsInputComparison
{
    QString referenceFile;
    QString candidateFile;
    QString referenceSha256;
    QString candidateSha256;
    int referenceRecordCount = 0;
    int candidateRecordCount = 0;
    int referenceMeshCount = 0;
    int candidateMeshCount = 0;
    qint64 referenceCellCount = 0;
    qint64 candidateCellCount = 0;
    QMap<QString, int> referenceKeywordCounts;
    QMap<QString, int> candidateKeywordCounts;
    QVector<FdsInputDifference> differences;
    QString rawTextDiff;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
    bool semanticallyEquivalent() const;
    int unacceptableDifferenceCount() const;
    QString csvReport() const;
    QString htmlReport() const;
};

class FdsInputComparator final
{
public:
    static FdsInputComparison compareFiles(const QString& referenceFile,
                                           const QString& candidateFile);
    static QVector<FdsUuidIdMapping> uuidToFdsIdMappings(const FcProject& project);
};
