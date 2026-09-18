#pragma once

#include "results/FcResultCase.h"
#include "results/FcResultFile.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <vector>

struct FdsResultFileInfo
{
    QString name;
    FcResultFileType type = FcResultFileType::Other;
    QString filePath;
    qint64 fileSize = 0;
    bool exists = false;
    QDateTime lastModified;
    int columnCount = 0;
    qint64 dataRowCount = 0;
    QString startTime;
    QString endTime;
    QStringList columnNames;
    QString quantity;
    QString shortName;
    QString unit;
    int meshIndex = 0;
    QString slicePlaneKeyword;
    double slicePlaneValue = 0.0;
    bool vectorField = false;
};

struct FdsResultScanResult
{
    QString caseName, resultDirectory, smvFilePath, fdsInputFilePath;
    FcResultStatus status = FcResultStatus::Failed;
    QString startTime, endTime;
    QDateTime scanTime;
    std::vector<FdsResultFileInfo> files;
    QStringList warnings;
    QString errorMessage;
    bool success() const { return errorMessage.isEmpty(); }
};

class FdsResultScanner final
{
public:
    FdsResultScanResult scanSmvFile(const QString& smvFilePath) const;
    QStringList findSmvFiles(const QString& directoryPath) const;
private:
    static FcResultFileType classifyFile(const QString& fileName);
};
