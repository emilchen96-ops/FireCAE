#pragma once

#include <QString>
#include <QStringList>

#include <memory>

class FcProject;

struct FcProjectRuntimeSettings
{
    QString resultDirectory;
    QString solverExecutable;
    int parallelProcessCount = 1;
};

struct FcProjectLoadResult
{
    std::unique_ptr<FcProject> project;
    FcProjectRuntimeSettings runtimeSettings;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return project != nullptr && errorMessage.isEmpty(); }
};

class FcProjectSerializer
{
public:
    static constexpr int CurrentFormatVersion = 5;

    static bool save(const FcProject& project,
                     const QString& filePath,
                     QString* errorMessage = nullptr);
    static bool save(const FcProject& project,
                     const QString& filePath,
                     const FcProjectRuntimeSettings& runtimeSettings,
                     QString* errorMessage = nullptr);
    static FcProjectLoadResult load(const QString& filePath);
};
