#include "results/FcResultCase.h"

QString resultStatusName(FcResultStatus status)
{
    switch (status) {
    case FcResultStatus::NotLoaded:
        return QStringLiteral("Not Loaded");
    case FcResultStatus::Ready:
        return QStringLiteral("Ready");
    case FcResultStatus::Incomplete:
        return QStringLiteral("Incomplete");
    case FcResultStatus::Failed:
        return QStringLiteral("Failed");
    case FcResultStatus::MissingFiles:
        return QStringLiteral("Missing Files");
    }
    return QStringLiteral("Unknown");
}

FcResultCase::FcResultCase(const QString& name)
    : FcObject(name, FcObjectType::ResultCase)
{
}

const QString& FcResultCase::resultDirectory() const { return m_resultDirectory; }
const QString& FcResultCase::smvFilePath() const { return m_smvFilePath; }
const QString& FcResultCase::fdsInputFilePath() const { return m_fdsInputFilePath; }
FcResultStatus FcResultCase::status() const { return m_status; }
int FcResultCase::resultFileCount() const { return m_resultFileCount; }
const QString& FcResultCase::startTime() const { return m_startTime; }
const QString& FcResultCase::endTime() const { return m_endTime; }
const QDateTime& FcResultCase::lastScanTime() const { return m_lastScanTime; }
int FcResultCase::warningCount() const { return m_warningCount; }

void FcResultCase::updateMetadata(const QString& resultDirectory,
                                  const QString& smvFilePath,
                                  const QString& fdsInputFilePath,
                                  FcResultStatus status,
                                  int resultFileCount,
                                  const QString& startTime,
                                  const QString& endTime,
                                  const QDateTime& lastScanTime,
                                  int warningCount)
{
    m_resultDirectory = resultDirectory;
    m_smvFilePath = smvFilePath;
    m_fdsInputFilePath = fdsInputFilePath;
    m_status = status;
    m_resultFileCount = resultFileCount;
    m_startTime = startTime;
    m_endTime = endTime;
    m_lastScanTime = lastScanTime;
    m_warningCount = warningCount;
}
