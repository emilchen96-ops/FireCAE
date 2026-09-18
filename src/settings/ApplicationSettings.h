#pragma once

#include <QString>
#include <QStringList>

struct ApplicationSettings
{
    static constexpr int CurrentFormatVersion = 2;

    int formatVersion = CurrentFormatVersion;
    QString language = QStringLiteral("system");
    QString defaultUnit = QStringLiteral("m");
    QString theme = QStringLiteral("system");
    QString backgroundColor = QStringLiteral("#687181");
    bool autoSaveEnabled = true;
    int autoSaveIntervalMinutes = 5;
    int autoSaveMaximumFiles = 10;
    QString fdsExecutable;
    QString mpiExecutable;
    QString smokeviewExecutable;
    int mpiProcessCount = 1;
    bool autoOpenResults = true;
    bool backupBeforeOpen = true;
    bool saveBeforeRun = true;
    QString defaultWorkingDirectory;
    double defaultMeshCellSize = 0.20;
    QString defaultMaterial = QStringLiteral("CONCRETE");
    QString defaultColorScheme = QStringLiteral("rainbow");
    bool highDpiEnabled = true;
    int largeFileWarningMegabytes = 250;
    QString logLevel = QStringLiteral("info");
    QString renderQuality = QStringLiteral("balanced");
};

class ApplicationSettingsStore final
{
public:
    explicit ApplicationSettingsStore(const QString& iniFilePath = {});

    ApplicationSettings load() const;
    bool save(const ApplicationSettings& settings, QString* errorMessage = nullptr) const;
    QStringList recentProjects() const;
    void addRecentProject(const QString& filePath, int maximumCount = 12) const;
    void removeRecentProject(const QString& filePath) const;

private:
    QString m_iniFilePath;
};
