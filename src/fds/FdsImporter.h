#pragma once

#include <QString>
#include <QStringList>

#include <memory>

class FcProject;

struct FdsImportResult
{
    std::unique_ptr<FcProject> project;
    QString sourceFilePath;
    int objectCount = 0;
    int unsupportedRecordCount = 0;
    int unsupportedParameterCount = 0;
    int resolvedReferenceCount = 0;
    int unresolvedReferenceCount = 0;
    QStringList unsupportedRecords;
    QStringList unsupportedParameters;
    QStringList unresolvedReferences;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return project != nullptr && errorMessage.isEmpty(); }
};

class FdsImporter
{
public:
    FdsImportResult importFile(const QString& filePath) const;
    FdsImportResult importText(const QString& text,
                               const QString& sourceName = {}) const;
};
