#pragma once

#include "fds/FcProjectSerializer.h"

#include <QDateTime>
#include <QList>
#include <QString>

class FcProject;

struct ProjectRecoveryEntry
{
    QString snapshotPath;
    QString metadataPath;
    QString originalProjectPath;
    QString projectName;
    QString reason;
    QDateTime createdAt;
    bool acknowledged = false;
};

class ProjectRecoveryManager final
{
public:
    explicit ProjectRecoveryManager(const QString& recoveryDirectory = {});

    const QString& recoveryDirectory() const;
    bool writeSnapshot(const FcProject& project,
                       const FcProjectRuntimeSettings& runtimeSettings,
                       const QString& originalProjectPath,
                       const QString& reason,
                       QString* snapshotPath = nullptr,
                       QString* errorMessage = nullptr) const;
    QList<ProjectRecoveryEntry> entries() const;
    bool acknowledge(const QString& snapshotPath, QString* errorMessage = nullptr) const;
    bool discard(const QString& snapshotPath, QString* errorMessage = nullptr) const;
    int discardForProject(const QString& originalProjectPath) const;
    int prune(int maximumCount) const;
    bool backupProjectFile(const QString& projectFilePath,
                           int maximumBackups = 10,
                           QString* backupPath = nullptr,
                           QString* errorMessage = nullptr) const;

    static QString defaultRecoveryDirectory();

private:
    QString m_recoveryDirectory;
};
