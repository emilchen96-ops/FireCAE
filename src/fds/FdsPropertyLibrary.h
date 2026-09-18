#pragma once

#include "fds/FcFdsModel.h"

#include <QString>
#include <QVector>

struct FdsLibraryEntry
{
    QString libraryId;
    QString name;
    QString category;
    QString keyword;
    QString fdsId;
    QString fdsVersion;
    std::vector<FcFdsParameter> parameters;
    bool builtIn = false;
};

enum class FdsLibraryConflictPolicy
{
    Skip,
    Replace,
    Rename
};

class FdsPropertyLibrary
{
public:
    explicit FdsPropertyLibrary(QString userFilePath = {});

    const QVector<FdsLibraryEntry>& entries() const;
    const QString& userFilePath() const;
    bool reload(QString* errorMessage = nullptr);
    bool save(QString* errorMessage = nullptr) const;
    bool importFile(const QString& filePath,
                    FdsLibraryConflictPolicy policy,
                    QString* errorMessage = nullptr);
    bool exportFile(const QString& filePath,
                    const QStringList& libraryIds,
                    QString* errorMessage = nullptr) const;
    QString duplicateAsUser(const QString& libraryId,
                            QString* errorMessage = nullptr);
    bool removeUserEntry(const QString& libraryId,
                         QString* errorMessage = nullptr);
    const FdsLibraryEntry* find(const QString& libraryId) const;

    static QVector<FdsLibraryEntry> builtInEntries();

private:
    bool readEntries(const QString& filePath,
                     QVector<FdsLibraryEntry>& entries,
                     QString* errorMessage) const;
    bool writeEntries(const QString& filePath,
                      const QVector<FdsLibraryEntry>& entries,
                      QString* errorMessage) const;
    QString uniqueId(QString preferred) const;

    QString m_userFilePath;
    QVector<FdsLibraryEntry> m_entries;
};
