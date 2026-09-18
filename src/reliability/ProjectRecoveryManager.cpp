#include "reliability/ProjectRecoveryManager.h"

#include "core/FcProject.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace
{
QString metadataPathFor(const QString& snapshotPath)
{
    return snapshotPath + QStringLiteral(".recovery.json");
}

QString normalizedPath(const QString& path)
{
    return path.trimmed().isEmpty()
               ? QString{}
               : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
}

ProjectRecoveryManager::ProjectRecoveryManager(const QString& recoveryDirectory)
    : m_recoveryDirectory(recoveryDirectory.trimmed().isEmpty()
                              ? defaultRecoveryDirectory()
                              : QDir::cleanPath(recoveryDirectory))
{
}

const QString& ProjectRecoveryManager::recoveryDirectory() const
{
    return m_recoveryDirectory;
}

bool ProjectRecoveryManager::writeSnapshot(
    const FcProject& project,
    const FcProjectRuntimeSettings& runtimeSettings,
    const QString& originalProjectPath,
    const QString& reason,
    QString* snapshotPath,
    QString* errorMessage) const
{
    if (!QDir().mkpath(m_recoveryDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create recovery directory.");
        return false;
    }
    QString safeName = project.name().trimmed();
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                     QStringLiteral("_"));
    if (safeName.isEmpty()) safeName = QStringLiteral("Untitled");
    const QString fileName = QStringLiteral("%1_%2_%3.firecae")
        .arg(safeName,
             QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
             QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString path = QDir(m_recoveryDirectory).filePath(fileName);
    QString saveError;
    if (!FcProjectSerializer::save(project, path, runtimeSettings, &saveError)) {
        if (errorMessage) *errorMessage = saveError;
        return false;
    }
    QJsonObject metadata;
    metadata.insert(QStringLiteral("format"), QStringLiteral("FireCAERecovery"));
    metadata.insert(QStringLiteral("version"), 1);
    metadata.insert(QStringLiteral("snapshotPath"), QDir::fromNativeSeparators(path));
    metadata.insert(QStringLiteral("originalProjectPath"),
                    QDir::fromNativeSeparators(normalizedPath(originalProjectPath)));
    metadata.insert(QStringLiteral("projectName"), project.name());
    metadata.insert(QStringLiteral("reason"), reason);
    metadata.insert(QStringLiteral("createdAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QSaveFile metadataFile(metadataPathFor(path));
    if (!metadataFile.open(QIODevice::WriteOnly) ||
        metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented)) < 0 ||
        !metadataFile.commit()) {
        QFile::remove(path);
        if (errorMessage) *errorMessage = QStringLiteral("Could not write recovery metadata.");
        return false;
    }
    if (snapshotPath) *snapshotPath = path;
    return true;
}

QList<ProjectRecoveryEntry> ProjectRecoveryManager::entries() const
{
    QList<ProjectRecoveryEntry> result;
    const QFileInfoList files = QDir(m_recoveryDirectory).entryInfoList(
        {QStringLiteral("*.firecae")}, QDir::Files, QDir::Time);
    for (const QFileInfo& file : files) {
        ProjectRecoveryEntry entry;
        entry.snapshotPath = file.absoluteFilePath();
        entry.metadataPath = metadataPathFor(entry.snapshotPath);
        entry.projectName = file.completeBaseName();
        entry.createdAt = file.lastModified().toUTC();
        QFile metadataFile(entry.metadataPath);
        if (metadataFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
            const QJsonObject metadata = document.object();
            if (metadata.value(QStringLiteral("format")).toString() ==
                QStringLiteral("FireCAERecovery")) {
                entry.originalProjectPath = QDir::toNativeSeparators(
                    metadata.value(QStringLiteral("originalProjectPath")).toString());
                entry.projectName = metadata.value(QStringLiteral("projectName"))
                                        .toString(entry.projectName);
                entry.reason = metadata.value(QStringLiteral("reason")).toString();
                entry.acknowledged = metadata.value(QStringLiteral("acknowledged")).toBool();
                const QDateTime created = QDateTime::fromString(
                    metadata.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
                if (created.isValid()) entry.createdAt = created;
            }
        }
        result.append(entry);
    }
    std::sort(result.begin(), result.end(),
              [](const ProjectRecoveryEntry& left, const ProjectRecoveryEntry& right) {
                  return left.createdAt > right.createdAt;
              });
    return result;
}

bool ProjectRecoveryManager::acknowledge(const QString& snapshotPath, QString* errorMessage) const
{
    const QString absolute = normalizedPath(snapshotPath);
    const QString relative = QDir(normalizedPath(m_recoveryDirectory)).relativeFilePath(absolute);
    if (relative.isEmpty() || relative.startsWith(QStringLiteral("..")) ||
        QDir::isAbsolutePath(relative) || !QFileInfo(absolute).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid recovery path.");
        return false;
    }
    QFile input(metadataPathFor(absolute));
    QJsonObject metadata;
    if (input.open(QIODevice::ReadOnly)) metadata = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    metadata.insert(QStringLiteral("format"), QStringLiteral("FireCAERecovery"));
    metadata.insert(QStringLiteral("acknowledged"), true);
    QSaveFile output(metadataPathFor(absolute));
    const QByteArray bytes = QJsonDocument(metadata).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        if (errorMessage) *errorMessage = output.errorString();
        return false;
    }
    return true;
}

bool ProjectRecoveryManager::discard(const QString& snapshotPath,
                                     QString* errorMessage) const
{
    const QString absolute = normalizedPath(snapshotPath);
    const QString directory = normalizedPath(m_recoveryDirectory);
    const QString relative = QDir(directory).relativeFilePath(absolute);
    if (absolute.isEmpty() || directory.isEmpty() || relative.isEmpty() ||
        relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) ||
        relative.startsWith(QStringLiteral("..\\")) || QDir::isAbsolutePath(relative)) {
        if (errorMessage) *errorMessage = QStringLiteral("Recovery path is outside the recovery directory.");
        return false;
    }
    bool removed = true;
    if (QFileInfo::exists(absolute)) removed = QFile::remove(absolute);
    const QString metadata = metadataPathFor(absolute);
    if (QFileInfo::exists(metadata)) removed = QFile::remove(metadata) && removed;
    if (!removed && errorMessage) *errorMessage = QStringLiteral("Could not remove recovery files.");
    return removed;
}

int ProjectRecoveryManager::discardForProject(const QString& originalProjectPath) const
{
    const QString expected = normalizedPath(originalProjectPath);
    int count = 0;
    for (const ProjectRecoveryEntry& entry : entries()) {
        if (!expected.isEmpty() && normalizedPath(entry.originalProjectPath)
                                       .compare(expected, Qt::CaseInsensitive) == 0 &&
            discard(entry.snapshotPath)) {
            ++count;
        }
    }
    return count;
}

int ProjectRecoveryManager::prune(int maximumCount) const
{
    const QList<ProjectRecoveryEntry> available = entries();
    const int keep = std::max(0, maximumCount);
    int removed = 0;
    for (int index = keep; index < available.size(); ++index) {
        if (discard(available[index].snapshotPath)) ++removed;
    }
    return removed;
}

bool ProjectRecoveryManager::backupProjectFile(const QString& projectFilePath,
                                               int maximumBackups,
                                               QString* backupPath,
                                               QString* errorMessage) const
{
    const QFileInfo source(projectFilePath);
    if (!source.exists() || !source.isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project file does not exist.");
        return false;
    }
    const QString directoryPath = source.absoluteDir().filePath(
        QStringLiteral(".firecae-backups/%1").arg(source.completeBaseName()));
    if (!QDir().mkpath(directoryPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create the project backup directory.");
        return false;
    }
    QFile input(source.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = input.errorString();
        return false;
    }
    const QString target = QDir(directoryPath).filePath(
        QStringLiteral("%1_%2_%3.%4")
            .arg(source.completeBaseName(),
                 QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces).left(8),
                 source.suffix().isEmpty() ? QStringLiteral("firecae") : source.suffix()));
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(input.readAll()) < 0 ||
        !output.commit()) {
        if (errorMessage) *errorMessage = output.errorString();
        return false;
    }
    const QFileInfoList backups = QDir(directoryPath).entryInfoList(
        {QStringLiteral("*.firecae"), QStringLiteral("*.fcae")},
        QDir::Files, QDir::Time);
    const int keep = std::max(1, maximumBackups);
    for (int index = keep; index < backups.size(); ++index)
        QFile::remove(backups.at(index).absoluteFilePath());
    if (backupPath) *backupPath = target;
    return true;
}

QString ProjectRecoveryManager::defaultRecoveryDirectory()
{
    const QString overrideDirectory =
        qEnvironmentVariable("FIRECAE_RECOVERY_DIRECTORY").trimmed();
    if (!overrideDirectory.isEmpty()) return QDir::cleanPath(overrideDirectory);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("recovery"));
}
