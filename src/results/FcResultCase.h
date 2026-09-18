#pragma once

#include "core/FcObject.h"

#include <QDateTime>
#include <QString>

enum class FcResultStatus
{
    NotLoaded,
    Ready,
    Incomplete,
    Failed,
    MissingFiles
};

QString resultStatusName(FcResultStatus status);

class FcResultCase final : public FcObject
{
public:
    explicit FcResultCase(const QString& name);

    const QString& resultDirectory() const;
    const QString& smvFilePath() const;
    const QString& fdsInputFilePath() const;
    FcResultStatus status() const;
    int resultFileCount() const;
    const QString& startTime() const;
    const QString& endTime() const;
    const QDateTime& lastScanTime() const;
    int warningCount() const;

    void updateMetadata(const QString& resultDirectory,
                        const QString& smvFilePath,
                        const QString& fdsInputFilePath,
                        FcResultStatus status,
                        int resultFileCount,
                        const QString& startTime,
                        const QString& endTime,
                        const QDateTime& lastScanTime,
                        int warningCount);

private:
    QString m_resultDirectory;
    QString m_smvFilePath;
    QString m_fdsInputFilePath;
    FcResultStatus m_status = FcResultStatus::NotLoaded;
    int m_resultFileCount = 0;
    QString m_startTime;
    QString m_endTime;
    QDateTime m_lastScanTime;
    int m_warningCount = 0;
};
