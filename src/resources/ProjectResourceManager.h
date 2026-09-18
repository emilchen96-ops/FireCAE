#pragma once

#include "fds/FcProjectSerializer.h"

#include <QList>
#include <QString>
#include <QStringList>

class FcProject;

enum class ProjectResourceKind
{
    IfcOrCad,
    TextureOrImage,
    Result,
    Other
};

struct ProjectResourceReference
{
    QString objectId;
    QString propertyKey;
    QString storedPath;
    QString resolvedPath;
    ProjectResourceKind kind = ProjectResourceKind::Other;
    bool exists = false;
    bool relative = false;
    bool packagedByDefault = true;
};

class ProjectResourceManager final
{
public:
    static QList<ProjectResourceReference> scan(const FcProject& project,
                                                const QString& projectFilePath);
    static QString resolvePath(const QString& storedPath,
                               const QString& projectFilePath);
    static bool relink(FcProject& project,
                       const QString& objectId,
                       const QString& propertyKey,
                       const QString& replacementPath);
    static int makePathsRelative(FcProject& project,
                                 const QString& projectFilePath);

    static bool packageProject(const FcProject& project,
                               const FcProjectRuntimeSettings& runtimeSettings,
                               const QString& formalProjectPath,
                               const QString& packageFilePath,
                               QString* errorMessage = nullptr);
    static bool unpackProject(const QString& packageFilePath,
                              const QString& destinationDirectory,
                              QString* extractedProjectPath = nullptr,
                              QString* errorMessage = nullptr);
    static bool copyProjectToDirectory(const FcProject& project,
                                       const FcProjectRuntimeSettings& runtimeSettings,
                                       const QString& formalProjectPath,
                                       const QString& destinationDirectory,
                                       QString* copiedProjectPath = nullptr,
                                       QString* errorMessage = nullptr);

    static QStringList unusedResultFiles(const FcProject& project,
                                         const QString& resultDirectory);
    static int removeFiles(const QStringList& exactFilePaths,
                           const QString& allowedRootDirectory,
                           QStringList* failures = nullptr);
};
