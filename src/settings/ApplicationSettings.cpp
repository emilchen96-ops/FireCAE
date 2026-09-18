#include "settings/ApplicationSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <memory>

namespace
{
std::unique_ptr<QSettings> openSettings(const QString& iniFilePath)
{
    if (!iniFilePath.trimmed().isEmpty()) {
        return std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat);
    }
    return std::make_unique<QSettings>();
}

QString absoluteCleanPath(const QString& path)
{
    return path.trimmed().isEmpty()
               ? QString{}
               : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
}

ApplicationSettingsStore::ApplicationSettingsStore(const QString& iniFilePath)
    : m_iniFilePath(iniFilePath)
{
}

ApplicationSettings ApplicationSettingsStore::load() const
{
    const std::unique_ptr<QSettings> store = openSettings(m_iniFilePath);
    ApplicationSettings value;
    value.formatVersion = store->value(QStringLiteral("Settings/FormatVersion"),
                                       ApplicationSettings::CurrentFormatVersion).toInt();
    value.language = store->value(QStringLiteral("Preferences/Language"),
                                  value.language).toString();
    value.defaultUnit = store->value(QStringLiteral("Preferences/DefaultUnit"),
                                     value.defaultUnit).toString();
    value.theme = store->value(QStringLiteral("Preferences/Theme"),
                               value.theme).toString();
    value.backgroundColor = store->value(QStringLiteral("Preferences/BackgroundColor"),
                                         value.backgroundColor).toString();
    value.autoSaveEnabled = store->value(QStringLiteral("Recovery/Enabled"),
                                         value.autoSaveEnabled).toBool();
    value.autoSaveIntervalMinutes = qBound(
        1, store->value(QStringLiteral("Recovery/IntervalMinutes"),
                        value.autoSaveIntervalMinutes).toInt(), 120);
    value.autoSaveMaximumFiles = qBound(
        1, store->value(QStringLiteral("Recovery/MaximumFiles"),
                        value.autoSaveMaximumFiles).toInt(), 100);
    value.fdsExecutable = QDir::toNativeSeparators(
        store->value(QStringLiteral("applications/fdsExecutable")).toString());
    value.mpiExecutable = QDir::toNativeSeparators(
        store->value(QStringLiteral("applications/mpiExecutable")).toString());
    value.smokeviewExecutable = QDir::toNativeSeparators(
        store->value(QStringLiteral("applications/smokeviewExecutable")).toString());
    value.mpiProcessCount = qBound(
        1, store->value(QStringLiteral("Preferences/MpiProcessCount"), 1).toInt(), 1024);
    value.autoOpenResults = store->value(QStringLiteral("Preferences/AutoOpenResults"),
                                         true).toBool();
    value.backupBeforeOpen = store->value(
        QStringLiteral("Recovery/BackupBeforeOpen"), true).toBool();
    value.saveBeforeRun = store->value(
        QStringLiteral("Preferences/SaveBeforeRun"), true).toBool();
    value.defaultWorkingDirectory = QDir::toNativeSeparators(
        store->value(QStringLiteral("Preferences/DefaultWorkingDirectory")).toString());
    value.defaultMeshCellSize = qBound(
        0.001, store->value(QStringLiteral("Defaults/MeshCellSize"), 0.20).toDouble(),
        1000.0);
    value.defaultMaterial = store->value(
        QStringLiteral("Defaults/Material"), value.defaultMaterial).toString();
    value.defaultColorScheme = store->value(
        QStringLiteral("Defaults/ColorScheme"), value.defaultColorScheme).toString();
    value.highDpiEnabled = store->value(
        QStringLiteral("Preferences/HighDpiEnabled"), true).toBool();
    value.largeFileWarningMegabytes = qBound(
        10, store->value(QStringLiteral("Preferences/LargeFileWarningMegabytes"), 250).toInt(),
        102400);
    value.logLevel = store->value(QStringLiteral("Preferences/LogLevel"),
                                  value.logLevel).toString();
    value.renderQuality = store->value(QStringLiteral("Preferences/RenderQuality"),
                                       value.renderQuality).toString();
    value.formatVersion = ApplicationSettings::CurrentFormatVersion;
    return value;
}

bool ApplicationSettingsStore::save(const ApplicationSettings& settings,
                                    QString* errorMessage) const
{
    const std::unique_ptr<QSettings> store = openSettings(m_iniFilePath);
    store->setValue(QStringLiteral("Settings/FormatVersion"),
                    ApplicationSettings::CurrentFormatVersion);
    store->setValue(QStringLiteral("Preferences/Language"), settings.language);
    store->setValue(QStringLiteral("Preferences/DefaultUnit"), settings.defaultUnit);
    store->setValue(QStringLiteral("Preferences/Theme"), settings.theme);
    store->setValue(QStringLiteral("Preferences/BackgroundColor"),
                    settings.backgroundColor);
    store->setValue(QStringLiteral("Recovery/Enabled"), settings.autoSaveEnabled);
    store->setValue(QStringLiteral("Recovery/IntervalMinutes"),
                    qBound(1, settings.autoSaveIntervalMinutes, 120));
    store->setValue(QStringLiteral("Recovery/MaximumFiles"),
                    qBound(1, settings.autoSaveMaximumFiles, 100));
    store->setValue(QStringLiteral("applications/fdsExecutable"),
                    QDir::fromNativeSeparators(settings.fdsExecutable));
    store->setValue(QStringLiteral("applications/mpiExecutable"),
                    QDir::fromNativeSeparators(settings.mpiExecutable));
    store->setValue(QStringLiteral("applications/smokeviewExecutable"),
                    QDir::fromNativeSeparators(settings.smokeviewExecutable));
    store->setValue(QStringLiteral("Preferences/MpiProcessCount"),
                    qBound(1, settings.mpiProcessCount, 1024));
    store->setValue(QStringLiteral("Preferences/AutoOpenResults"),
                    settings.autoOpenResults);
    store->setValue(QStringLiteral("Recovery/BackupBeforeOpen"),
                    settings.backupBeforeOpen);
    store->setValue(QStringLiteral("Preferences/SaveBeforeRun"),
                    settings.saveBeforeRun);
    store->setValue(QStringLiteral("Preferences/DefaultWorkingDirectory"),
                    QDir::fromNativeSeparators(settings.defaultWorkingDirectory));
    store->setValue(QStringLiteral("Defaults/MeshCellSize"),
                    qBound(0.001, settings.defaultMeshCellSize, 1000.0));
    store->setValue(QStringLiteral("Defaults/Material"), settings.defaultMaterial);
    store->setValue(QStringLiteral("Defaults/ColorScheme"), settings.defaultColorScheme);
    store->setValue(QStringLiteral("Preferences/HighDpiEnabled"), settings.highDpiEnabled);
    store->setValue(QStringLiteral("Preferences/LargeFileWarningMegabytes"),
                    qBound(10, settings.largeFileWarningMegabytes, 102400));
    store->setValue(QStringLiteral("Preferences/LogLevel"), settings.logLevel);
    store->setValue(QStringLiteral("Preferences/RenderQuality"),
                    settings.renderQuality);
    store->sync();
    if (store->status() == QSettings::NoError) return true;
    if (errorMessage) {
        *errorMessage = QStringLiteral("The versioned application settings could not be written.");
    }
    return false;
}

QStringList ApplicationSettingsStore::recentProjects() const
{
    const std::unique_ptr<QSettings> store = openSettings(m_iniFilePath);
    QStringList result;
    for (const QString& entry :
         store->value(QStringLiteral("RecentProjects/Files")).toStringList()) {
        const QString normalized = absoluteCleanPath(entry);
        if (!normalized.isEmpty() && !result.contains(normalized, Qt::CaseInsensitive)) {
            result.append(normalized);
        }
    }
    return result;
}

void ApplicationSettingsStore::addRecentProject(const QString& filePath,
                                                 int maximumCount) const
{
    const QString normalized = absoluteCleanPath(filePath);
    if (normalized.isEmpty()) return;
    QStringList recent = recentProjects();
    for (auto iterator = recent.begin(); iterator != recent.end();) {
        if (iterator->compare(normalized, Qt::CaseInsensitive) == 0) {
            iterator = recent.erase(iterator);
        } else {
            ++iterator;
        }
    }
    recent.prepend(normalized);
    const int keep = qBound(1, maximumCount, 100);
    while (recent.size() > keep) recent.removeLast();
    const std::unique_ptr<QSettings> store = openSettings(m_iniFilePath);
    store->setValue(QStringLiteral("RecentProjects/Files"), recent);
    store->sync();
}

void ApplicationSettingsStore::removeRecentProject(const QString& filePath) const
{
    const QString normalized = absoluteCleanPath(filePath);
    QStringList recent = recentProjects();
    for (auto iterator = recent.begin(); iterator != recent.end();) {
        if (iterator->compare(normalized, Qt::CaseInsensitive) == 0) {
            iterator = recent.erase(iterator);
        } else {
            ++iterator;
        }
    }
    const std::unique_ptr<QSettings> store = openSettings(m_iniFilePath);
    store->setValue(QStringLiteral("RecentProjects/Files"), recent);
    store->sync();
}
