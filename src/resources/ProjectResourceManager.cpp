#include "resources/ProjectResourceManager.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "results/FcResultCase.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include <functional>

namespace
{
constexpr quint32 kMaximumArchiveEntries = 10000;
constexpr qint64 kMaximumSingleEntryBytes = 1024LL * 1024LL * 1024LL;
const QByteArray kPackageMagic("FIRECAE_PACKAGE_V1");

ProjectResourceKind kindForPath(const QString& path, bool result)
{
    if (result) return ProjectResourceKind::Result;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{QStringLiteral("ifc"), QStringLiteral("stl"), QStringLiteral("obj"),
         QStringLiteral("glb"), QStringLiteral("gltf"), QStringLiteral("step"),
         QStringLiteral("stp"), QStringLiteral("iges"), QStringLiteral("igs"),
         QStringLiteral("dxf"), QStringLiteral("dwg"), QStringLiteral("fbx"),
         QStringLiteral("dae")}.contains(suffix))
        return ProjectResourceKind::IfcOrCad;
    if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
         QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"),
         QStringLiteral("webp")}.contains(suffix))
        return ProjectResourceKind::TextureOrImage;
    return ProjectResourceKind::Other;
}

void appendReference(QList<ProjectResourceReference>& result,
                     const QString& objectId,
                     const QString& key,
                     const QString& storedPath,
                     const QString& projectFilePath,
                     bool resultResource)
{
    if (storedPath.trimmed().isEmpty()) return;
    ProjectResourceReference reference;
    reference.objectId = objectId;
    reference.propertyKey = key;
    reference.storedPath = QDir::toNativeSeparators(storedPath);
    reference.resolvedPath = ProjectResourceManager::resolvePath(storedPath,
                                                                 projectFilePath);
    reference.kind = kindForPath(storedPath, resultResource);
    reference.exists = QFileInfo::exists(reference.resolvedPath);
    reference.relative = QDir::isRelativePath(storedPath);
    reference.packagedByDefault = !resultResource;
    result.append(reference);
}

void collectObjectResources(const FcObject::Ptr& object,
                            const QString& projectFilePath,
                            QList<ProjectResourceReference>& result)
{
    if (!object) return;
    if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
        appendReference(result, ifc->id(), QStringLiteral("ifcSource"),
                        ifc->sourceFile(), projectFilePath, false);
    }
    if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        const QVariantMap parameters = geometry->geometryParameters();
        for (const QString& key : {QStringLiteral("sourceFile"),
                                   QStringLiteral("resourcePath"),
                                   QStringLiteral("texturePath")}) {
            appendReference(result, geometry->id(), key, parameters.value(key).toString(),
                            projectFilePath, false);
        }
    }
    if (const auto resultCase = std::dynamic_pointer_cast<FcResultCase>(object)) {
        appendReference(result, resultCase->id(), QStringLiteral("resultDirectory"),
                        resultCase->resultDirectory(), projectFilePath, true);
        appendReference(result, resultCase->id(), QStringLiteral("smvFilePath"),
                        resultCase->smvFilePath(), projectFilePath, true);
        appendReference(result, resultCase->id(), QStringLiteral("fdsInputFilePath"),
                        resultCase->fdsInputFilePath(), projectFilePath, true);
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectObjectResources(child, projectFilePath, result);
    }
}

bool safeArchivePath(const QString& path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return !clean.isEmpty() && clean != QStringLiteral(".") &&
           !QDir::isAbsolutePath(clean) && clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

bool writeExactFile(const QString& path, const QByteArray& data)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
           file.commit();
}
}

QList<ProjectResourceReference> ProjectResourceManager::scan(
    const FcProject& project,
    const QString& projectFilePath)
{
    QList<ProjectResourceReference> result;
    if (!project.document()) return result;
    for (const FcObject::Ptr& group : project.document()->groups()) {
        collectObjectResources(group, projectFilePath, result);
    }
    return result;
}

QString ProjectResourceManager::resolvePath(const QString& storedPath,
                                            const QString& projectFilePath)
{
    if (storedPath.trimmed().isEmpty()) return {};
    if (QDir::isAbsolutePath(storedPath)) {
        return QDir::cleanPath(QFileInfo(storedPath).absoluteFilePath());
    }
    const QString base = QFileInfo(projectFilePath).absolutePath();
    return QDir::cleanPath(QDir(base).absoluteFilePath(storedPath));
}

bool ProjectResourceManager::relink(FcProject& project,
                                    const QString& objectId,
                                    const QString& propertyKey,
                                    const QString& replacementPath)
{
    if (!project.document() || objectId.isEmpty() || replacementPath.isEmpty()) return false;
    const FcObject::Ptr object = project.document()->findObject(objectId);
    if (!object) return false;
    const QString normalized = QDir::cleanPath(replacementPath);
    if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
        if (propertyKey != QStringLiteral("ifcSource")) return false;
        ifc->setSourceFile(normalized);
        project.setModified(true);
        return true;
    }
    if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        if (!QStringList{QStringLiteral("sourceFile"), QStringLiteral("resourcePath"),
                         QStringLiteral("texturePath")}.contains(propertyKey))
            return false;
        QVariantMap parameters = geometry->geometryParameters();
        parameters.insert(propertyKey, normalized);
        geometry->setGeometryParameters(parameters);
        project.setModified(true);
        return true;
    }
    if (const auto resultCase = std::dynamic_pointer_cast<FcResultCase>(object)) {
        QString directory = resultCase->resultDirectory();
        QString smv = resultCase->smvFilePath();
        QString fds = resultCase->fdsInputFilePath();
        if (propertyKey == QStringLiteral("resultDirectory")) directory = normalized;
        else if (propertyKey == QStringLiteral("smvFilePath")) smv = normalized;
        else if (propertyKey == QStringLiteral("fdsInputFilePath")) fds = normalized;
        else return false;
        resultCase->updateMetadata(directory, smv, fds, resultCase->status(),
                                   resultCase->resultFileCount(), resultCase->startTime(),
                                   resultCase->endTime(), resultCase->lastScanTime(),
                                   resultCase->warningCount());
        project.setModified(true);
        return true;
    }
    return false;
}

int ProjectResourceManager::makePathsRelative(FcProject& project,
                                              const QString& projectFilePath)
{
    const QString projectDirectory = QFileInfo(projectFilePath).absolutePath();
    int changed = 0;
    for (const ProjectResourceReference& reference : scan(project, projectFilePath)) {
        if (reference.relative || reference.resolvedPath.isEmpty()) continue;
        const QString relative = QDir(projectDirectory).relativeFilePath(reference.resolvedPath);
        if (relink(project, reference.objectId, reference.propertyKey, relative)) ++changed;
    }
    return changed;
}

bool ProjectResourceManager::packageProject(
    const FcProject& project,
    const FcProjectRuntimeSettings& runtimeSettings,
    const QString& formalProjectPath,
    const QString& packageFilePath,
    QString* errorMessage)
{
    QTemporaryDir staging;
    if (!staging.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create package staging directory.");
        return false;
    }
    const QString projectSnapshot = staging.filePath(QStringLiteral("project.firecae"));
    QString saveError;
    if (!FcProjectSerializer::save(project, projectSnapshot, runtimeSettings, &saveError)) {
        if (errorMessage) *errorMessage = saveError;
        return false;
    }
    QFile projectFile(projectSnapshot);
    if (!projectFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not read package project snapshot.");
        return false;
    }

    QList<QPair<QString, QByteArray>> archiveEntries;
    archiveEntries.append({QStringLiteral("project.firecae"), projectFile.readAll()});
    QJsonArray manifestResources;
    QHash<QString, QString> archivedBySource;
    int resourceIndex = 0;
    for (const ProjectResourceReference& reference : scan(project, formalProjectPath)) {
        if (!reference.packagedByDefault || !reference.exists ||
            !QFileInfo(reference.resolvedPath).isFile())
            continue;
        const QString canonical = QFileInfo(reference.resolvedPath).canonicalFilePath();
        QString archivePath = archivedBySource.value(canonical);
        if (archivePath.isEmpty()) {
            archivePath = QStringLiteral("resources/%1_%2")
                              .arg(++resourceIndex, 4, 10, QLatin1Char('0'))
                              .arg(QFileInfo(reference.resolvedPath).fileName());
            QFile resource(reference.resolvedPath);
            if (!resource.open(QIODevice::ReadOnly) ||
                resource.size() > kMaximumSingleEntryBytes) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Resource cannot be packaged: %1")
                                        .arg(reference.resolvedPath);
                }
                return false;
            }
            archiveEntries.append({archivePath, resource.readAll()});
            archivedBySource.insert(canonical, archivePath);
        }
        QJsonObject resourceJson;
        resourceJson.insert(QStringLiteral("objectId"), reference.objectId);
        resourceJson.insert(QStringLiteral("propertyKey"), reference.propertyKey);
        resourceJson.insert(QStringLiteral("originalPath"),
                            QDir::fromNativeSeparators(reference.storedPath));
        resourceJson.insert(QStringLiteral("archivePath"), archivePath);
        manifestResources.append(resourceJson);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("FireCAEPackage"));
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("formalProjectName"),
                    QFileInfo(formalProjectPath).fileName());
    manifest.insert(QStringLiteral("resultsEmbedded"), false);
    manifest.insert(QStringLiteral("resources"), manifestResources);
    archiveEntries.append({QStringLiteral("manifest.json"),
                           QJsonDocument(manifest).toJson(QJsonDocument::Indented)});

    QSaveFile output(packageFilePath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create project package.");
        return false;
    }
    QDataStream stream(&output);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kPackageMagic << quint32(archiveEntries.size());
    for (const auto& entry : archiveEntries) stream << entry.first << entry.second;
    if (stream.status() != QDataStream::Ok || !output.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not finalize project package.");
        return false;
    }
    return true;
}

bool ProjectResourceManager::unpackProject(const QString& packageFilePath,
                                           const QString& destinationDirectory,
                                           QString* extractedProjectPath,
                                           QString* errorMessage)
{
    QFile input(packageFilePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not open project package.");
        return false;
    }
    QDataStream stream(&input);
    stream.setVersion(QDataStream::Qt_6_0);
    QByteArray magic;
    quint32 count = 0;
    stream >> magic >> count;
    if (magic != kPackageMagic || count == 0 || count > kMaximumArchiveEntries) {
        if (errorMessage) *errorMessage = QStringLiteral("The project package header is invalid.");
        return false;
    }
    if (!QDir().mkpath(destinationDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create unpack destination.");
        return false;
    }
    for (quint32 index = 0; index < count; ++index) {
        QString archivePath;
        QByteArray data;
        stream >> archivePath >> data;
        if (stream.status() != QDataStream::Ok || !safeArchivePath(archivePath) ||
            data.size() > kMaximumSingleEntryBytes ||
            !writeExactFile(QDir(destinationDirectory).filePath(archivePath), data)) {
            if (errorMessage) *errorMessage = QStringLiteral("The project package contains an invalid entry.");
            return false;
        }
    }
    const QString projectPath =
        QDir(destinationDirectory).filePath(QStringLiteral("project.firecae"));
    const QString manifestPath =
        QDir(destinationDirectory).filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("The project package manifest is missing.");
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    FcProjectLoadResult loaded = FcProjectSerializer::load(projectPath);
    const QString manifestFormat = manifest.value(QStringLiteral("format")).toString();
    if (manifestFormat != QStringLiteral("FireCAEPackage") || !loaded.success()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The packaged project is invalid (manifest='%1', project='%2').")
                                .arg(manifestFormat,
                                     loaded.errorMessage.isEmpty()
                                         ? QStringLiteral("ok")
                                         : loaded.errorMessage);
        }
        return false;
    }
    for (const QJsonValue& value : manifest.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        const QString archivePath = resource.value(QStringLiteral("archivePath")).toString();
        if (!safeArchivePath(archivePath)) continue;
        relink(*loaded.project,
               resource.value(QStringLiteral("objectId")).toString(),
               resource.value(QStringLiteral("propertyKey")).toString(),
               QDir(destinationDirectory).filePath(archivePath));
    }
    QString saveError;
    if (!FcProjectSerializer::save(*loaded.project, projectPath,
                                   loaded.runtimeSettings, &saveError)) {
        if (errorMessage) *errorMessage = saveError;
        return false;
    }
    if (extractedProjectPath) *extractedProjectPath = projectPath;
    return true;
}

bool ProjectResourceManager::copyProjectToDirectory(
    const FcProject& project,
    const FcProjectRuntimeSettings& runtimeSettings,
    const QString& formalProjectPath,
    const QString& destinationDirectory,
    QString* copiedProjectPath,
    QString* errorMessage)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create copy staging directory.");
        return false;
    }
    const QString package = temporary.filePath(QStringLiteral("copy.firecaepkg"));
    return packageProject(project, runtimeSettings, formalProjectPath, package, errorMessage) &&
           unpackProject(package, destinationDirectory, copiedProjectPath, errorMessage);
}

QStringList ProjectResourceManager::unusedResultFiles(const FcProject& project,
                                                      const QString& resultDirectory)
{
    QSet<QString> referenced;
    for (const ProjectResourceReference& reference : scan(project, {})) {
        if (reference.kind == ProjectResourceKind::Result && !reference.resolvedPath.isEmpty())
            referenced.insert(QFileInfo(reference.resolvedPath).canonicalFilePath());
    }
    QStringList unused;
    const QFileInfoList files = QDir(resultDirectory).entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& file : files) {
        if (!referenced.contains(file.canonicalFilePath())) unused.append(file.absoluteFilePath());
    }
    return unused;
}

int ProjectResourceManager::removeFiles(const QStringList& exactFilePaths,
                                        const QString& allowedRootDirectory,
                                        QStringList* failures)
{
    const QString root = QFileInfo(allowedRootDirectory).canonicalFilePath();
    if (root.isEmpty()) return 0;
    int removed = 0;
    for (const QString& path : exactFilePaths) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        const QString relative = QDir(root).relativeFilePath(canonical);
        if (canonical.isEmpty() || QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") ||
            relative.startsWith(QStringLiteral("../")) || relative.startsWith(QStringLiteral("..\\"))) {
            if (failures) failures->append(path);
            continue;
        }
        if (QFile::remove(canonical)) ++removed;
        else if (failures) failures->append(path);
    }
    return removed;
}
