#include "core/FcDocument.h"
#include "core/FcFloorObject.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "comparison/FdsInputComparator.h"
#include "comparison/ThreeWayComparisonEvidence.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsImporter.h"
#include "fds/FcProjectSerializer.h"
#include "import/GeometryImportService.h"
#include "import/IfcImportService.h"
#include "modeling/SnapManager.h"
#include "modeling/BuildingGeometryService.h"
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS_Compound.hxx>
#include "reliability/ProjectRecoveryManager.h"
#include "settings/ApplicationSettings.h"
#include "results/FcResultCase.h"
#include "results/FcResultFile.h"
#include "results/FdsCsvData.h"
#include "results/FdsResultComparator.h"
#include "results/FdsResultScanner.h"
#include "results/FdsSliceReader.h"
#include "results/FdsSliceImageRenderer.h"
#include "results/SmokeviewLauncher.h"
#include "results/SmokeviewFrameRenderer.h"
#include "resources/ProjectResourceManager.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>

#include <QCoreApplication>
#include <QImage>
#include <QRegularExpression>
#include <QSettings>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>

namespace
{
int failureCount = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failureCount;
    }
}

int ifcShapeCount(const std::shared_ptr<FcIfcObject>& object)
{
    if (!object) {
        return 0;
    }
    int count = object->hasShape() ? 1 : 0;
    for (const FcObject::Ptr& child : object->children()) {
        count += ifcShapeCount(std::dynamic_pointer_cast<FcIfcObject>(child));
    }
    return count;
}

bool writeTextFile(const QString& filePath, const QString& contents)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream stream(&file);
    stream << contents;
    return stream.status() == QTextStream::Ok;
}

bool writeSliceFixture(const QString& filePath, QDataStream::ByteOrder byteOrder)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const auto record = [&](const QByteArray& payload) {
        QDataStream stream(&file);
        stream.setByteOrder(byteOrder);
        stream << static_cast<quint32>(payload.size());
        if (stream.status() != QDataStream::Ok ||
            file.write(payload) != payload.size()) return false;
        stream << static_cast<quint32>(payload.size());
        return stream.status() == QDataStream::Ok;
    };
    const auto fixedLabel = [](const QByteArray& value) {
        return value.leftJustified(30, ' ', true);
    };
    if (!record(fixedLabel("TEMPERATURE")) ||
        !record(fixedLabel("temp")) || !record(fixedLabel("C"))) return false;
    QByteArray bounds;
    {
        QDataStream stream(&bounds, QIODevice::WriteOnly);
        stream.setByteOrder(byteOrder);
        for (qint32 value : {0, 1, 0, 2, 4, 4}) stream << value;
    }
    if (!record(bounds)) return false;
    for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
        QByteArray time;
        {
            QDataStream stream(&time, QIODevice::WriteOnly);
            stream.setByteOrder(byteOrder);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            stream << static_cast<float>(frameIndex) * 0.5F;
        }
        QByteArray values;
        {
            QDataStream stream(&values, QIODevice::WriteOnly);
            stream.setByteOrder(byteOrder);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            for (int value = 0; value < 6; ++value) {
                stream << static_cast<float>(frameIndex * 10 + value);
            }
        }
        if (!record(time) || !record(values)) return false;
    }
    return true;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    check(!application.inherits("QGuiApplication"),
          "Headless HTML report regression runs with a real QCoreApplication");
    FdsCsvQuantityComparison headlessQuantity;
    headlessQuantity.quantity = QStringLiteral("Temperature & probe");
    headlessQuantity.unit = QStringLiteral("C");
    headlessQuantity.timeUnit = QStringLiteral("s");
    headlessQuantity.sampleCount = 3;
    headlessQuantity.sampleTimes = {2.0, 4.0, 6.0};
    headlessQuantity.referenceSamples = {-10.0, 0.0, 10.0};
    headlessQuantity.candidateSamples = {10.0, 0.0, -10.0};
    FdsResultComparison headlessComparison;
    headlessComparison.quantities.push_back(headlessQuantity);
    const QString headlessHtml = headlessComparison.htmlReport();
    const QRegularExpression headlessPngExpression(
        QStringLiteral("data:image/png;base64,([^']+)"));
    const auto headlessPngMatch = headlessPngExpression.match(headlessHtml);
    const QImage headlessImage = QImage::fromData(
        QByteArray::fromBase64(headlessPngMatch.captured(1).toLatin1()), "PNG");
    check(!headlessImage.isNull() && headlessImage.width() > 100 &&
              headlessImage.height() > 50,
          "QCore HTML report contains a decodable time-series PNG");
    bool headlessReferenceVisible = false;
    bool headlessCandidateVisible = false;
    for (int y = 0; y < headlessImage.height(); ++y) {
        for (int x = 0; x < headlessImage.width(); ++x) {
            const QRgb pixel = headlessImage.pixel(x, y);
            headlessReferenceVisible |= pixel == qRgb(0x15, 0x65, 0xc0);
            headlessCandidateVisible |= pixel == qRgb(0xef, 0x6c, 0x00);
        }
    }
    check(headlessReferenceVisible && headlessCandidateVisible,
          "QCore PNG retains both reference and candidate curves");
    check(headlessHtml.contains(QStringLiteral(
              "Horizontal axis: Time (s); left: 2; middle: 4; right: 6.")) &&
              headlessHtml.contains(QStringLiteral(
              "Vertical axis: Temperature &amp; probe (C); bottom: -10; middle: 0; top: 10.")),
          "QCore HTML labels both axes, units and all three tick positions");
    check(headlessHtml.contains(QStringLiteral(
              "Time (s): 2 .. 6; Temperature &amp; probe (C): -10 .. 10")) &&
              headlessHtml.contains(QStringLiteral("Reference</span>")) &&
              headlessHtml.contains(QStringLiteral("Candidate</span>")),
          "QCore report preserves complete ranges, escaped labels and series legend");
    headlessQuantity.quantity = QStringLiteral("Zero");
    headlessQuantity.unit.clear();
    headlessQuantity.timeUnit.clear();
    headlessQuantity.referenceSamples.fill(0.0);
    headlessQuantity.candidateSamples.fill(0.0);
    headlessComparison.quantities.front() = headlessQuantity;
    const QString zeroHeadlessHtml = headlessComparison.htmlReport();
    check(zeroHeadlessHtml.contains(QStringLiteral(
              "Horizontal axis: Time (unit not supplied); left: 2; middle: 4; right: 6.")) &&
              zeroHeadlessHtml.contains(QStringLiteral(
              "Vertical axis: Zero (unit not supplied); constant value: 0 (single tick).")) &&
              headlessPngExpression.match(zeroHeadlessHtml).hasMatch(),
          "QCore zero series retains a PNG and one honest tick without invented units");
    QTemporaryDir isolatedPreferences;
    if (!isolatedPreferences.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolatedPreferences.path());

    QTemporaryDir settingsDirectory;
    const QString settingsPath =
        QDir(settingsDirectory.path()).filePath(QStringLiteral("settings.ini"));
    ApplicationSettingsStore settingsStore(settingsPath);
    ApplicationSettings settingsValue;
    settingsValue.language = QStringLiteral("zh_CN");
    settingsValue.defaultUnit = QStringLiteral("mm");
    settingsValue.theme = QStringLiteral("dark");
    settingsValue.autoSaveIntervalMinutes = 7;
    settingsValue.autoSaveMaximumFiles = 4;
    settingsValue.mpiProcessCount = 6;
    settingsValue.autoOpenResults = false;
    settingsValue.renderQuality = QStringLiteral("high");
    QString settingsError;
    check(settingsDirectory.isValid() && settingsStore.save(settingsValue, &settingsError),
          "A15 versioned application settings can be saved");
    const ApplicationSettings restoredSettings = settingsStore.load();
    check(restoredSettings.formatVersion == ApplicationSettings::CurrentFormatVersion &&
              restoredSettings.language == QStringLiteral("zh_CN") &&
              restoredSettings.defaultUnit == QStringLiteral("mm") &&
              restoredSettings.autoSaveIntervalMinutes == 7 &&
              restoredSettings.autoSaveMaximumFiles == 4 &&
              restoredSettings.mpiProcessCount == 6 &&
              !restoredSettings.autoOpenResults &&
              restoredSettings.renderQuality == QStringLiteral("high"),
          "A15 versioned settings round-trip all reliability and runtime preferences");
    const QString recentOne =
        QDir(settingsDirectory.path()).filePath(QStringLiteral("one.firecae"));
    const QString recentTwo =
        QDir(settingsDirectory.path()).filePath(QStringLiteral("two.firecae"));
    settingsStore.addRecentProject(recentOne, 2);
    settingsStore.addRecentProject(recentTwo, 2);
    settingsStore.addRecentProject(recentOne, 2);
    check(settingsStore.recentProjects().size() == 2 &&
              settingsStore.recentProjects().constFirst().endsWith(
                  QStringLiteral("one.firecae")),
          "A15 recent projects are de-duplicated and ordered by latest use");

    QTemporaryDir recoveryDirectory;
    ProjectRecoveryManager recoveryManager(recoveryDirectory.path());
    FcProject recoveryProject(QStringLiteral("Recovery lifecycle"));
    recoveryProject.setChid(QStringLiteral("recovery_lifecycle"));
    FcProjectRuntimeSettings recoveryRuntime;
    recoveryRuntime.resultDirectory = QStringLiteral("D:/results/not-embedded");
    recoveryRuntime.solverExecutable = QStringLiteral("D:/runtime/fds.exe");
    recoveryRuntime.parallelProcessCount = 4;
    const QString formalProjectPath =
        QDir(recoveryDirectory.path()).filePath(QStringLiteral("formal.firecae"));
    QString recoverySnapshot;
    QString recoveryError;
    check(recoveryDirectory.isValid() &&
              recoveryManager.writeSnapshot(recoveryProject,
                                            recoveryRuntime,
                                            formalProjectPath,
                                            QStringLiteral("Core recovery test"),
                                            &recoverySnapshot,
                                            &recoveryError) &&
              QFileInfo::exists(recoverySnapshot),
          "A15 recovery manager writes a non-destructive project snapshot");
    const QList<ProjectRecoveryEntry> initialRecoveryEntries = recoveryManager.entries();
    check(initialRecoveryEntries.size() == 1 &&
              initialRecoveryEntries.constFirst().originalProjectPath ==
                  QDir::toNativeSeparators(QFileInfo(formalProjectPath).absoluteFilePath()) &&
              initialRecoveryEntries.constFirst().reason ==
                  QStringLiteral("Core recovery test"),
          "A15 recovery metadata preserves source path and recovery reason");
    const FcProjectLoadResult recoveredProject =
        FcProjectSerializer::load(recoverySnapshot);
    check(recoveredProject.success() &&
              recoveredProject.project->chid() == QStringLiteral("recovery_lifecycle") &&
              recoveredProject.runtimeSettings.parallelProcessCount == 4,
          "A15 recovery snapshot reopens with project and runtime settings intact");
    QString secondRecovery;
    QString thirdRecovery;
    check(recoveryManager.writeSnapshot(recoveryProject,
                                        recoveryRuntime,
                                        formalProjectPath,
                                        QStringLiteral("Second"),
                                        &secondRecovery,
                                        &recoveryError) &&
              recoveryManager.writeSnapshot(recoveryProject,
                                            recoveryRuntime,
                                            formalProjectPath,
                                            QStringLiteral("Third"),
                                            &thirdRecovery,
                                            &recoveryError) &&
              recoveryManager.entries().size() == 3,
          "A15 recovery manager can retain multiple recovery generations");
    check(recoveryManager.prune(2) == 1 && recoveryManager.entries().size() == 2,
          "A15 recovery retention prunes only snapshots beyond the configured limit");
    const QString retainedSnapshot = recoveryManager.entries().constFirst().snapshotPath;
    check(recoveryManager.acknowledge(retainedSnapshot, &recoveryError) &&
              ProjectRecoveryManager(recoveryDirectory.path()).entries().constFirst().acknowledged &&
              FcProjectSerializer::load(retainedSnapshot).success(),
          "recovery acknowledgement survives restart and preserves a readable snapshot");
    check(!recoveryManager.acknowledge(recoveryDirectory.path() + QStringLiteral("/../outside.firecae"), &recoveryError),
          "recovery acknowledgement rejects paths outside its directory");
    check(recoveryManager.discard(retainedSnapshot) &&
              !QFileInfo::exists(retainedSnapshot) &&
              recoveryManager.entries().size() == 1,
          "A15 recovery discard removes the selected snapshot and metadata only");

    QTemporaryDir resourceSourceDirectory;
    QTemporaryDir resourceUnpackDirectory;
    QTemporaryDir resourceCopyDirectory;
    const QString sourceResourcePath =
        QDir(resourceSourceDirectory.path()).filePath(QStringLiteral("building.obj"));
    const QString formalResourceProjectPath =
        QDir(resourceSourceDirectory.path()).filePath(QStringLiteral("resource.firecae"));
    const QString packagePath =
        QDir(resourceSourceDirectory.path()).filePath(QStringLiteral("resource.firecaepkg"));
    FcProject resourceProject(QStringLiteral("Resource project"));
    auto resourceGeometry = std::make_shared<FcGeometryObject>(
        QStringLiteral("Linked building"));
    resourceGeometry->setGeometryParameters(
        {{QStringLiteral("sourceFile"), sourceResourcePath}});
    const QString resourceObjectId = resourceGeometry->id();
    check(writeTextFile(sourceResourcePath, QStringLiteral("o packaged_resource\n")) &&
              resourceProject.document()->geometryGroup()->addChild(resourceGeometry),
          "A15 resource fixture is attached to a UUID-backed geometry object");
    const QList<ProjectResourceReference> initialResources =
        ProjectResourceManager::scan(resourceProject, formalResourceProjectPath);
    check(initialResources.size() == 1 && initialResources.constFirst().exists &&
              initialResources.constFirst().kind == ProjectResourceKind::IfcOrCad,
          "A15 resource scanner identifies an existing CAD dependency");
    check(ProjectResourceManager::makePathsRelative(resourceProject,
                                                     formalResourceProjectPath) == 1 &&
              QDir::isRelativePath(resourceGeometry->geometryParameters()
                                       .value(QStringLiteral("sourceFile")).toString()),
          "A15 project resources can be converted to portable relative paths");
    QString packageError;
    FcProjectRuntimeSettings packageRuntime;
    packageRuntime.parallelProcessCount = 3;
    check(ProjectResourceManager::packageProject(resourceProject,
                                                  packageRuntime,
                                                  formalResourceProjectPath,
                                                  packagePath,
                                                  &packageError) &&
              QFileInfo(packagePath).size() > 0,
          "A15 project package stores the project and redistributable resources");
    QString unpackedProjectPath;
    const bool unpackedPackage = ProjectResourceManager::unpackProject(
        packagePath, resourceUnpackDirectory.path(), &unpackedProjectPath, &packageError);
    if (!unpackedPackage) {
        std::cerr << "A15 unpack error: " << packageError.toStdString() << '\n';
    }
    check(unpackedPackage && QFileInfo::exists(unpackedProjectPath),
          "A15 project package unpacks through traversal-safe exact paths");
    const FcProjectLoadResult unpackedResourceProject =
        FcProjectSerializer::load(unpackedProjectPath);
    const auto unpackedGeometry = unpackedResourceProject.success()
        ? std::dynamic_pointer_cast<FcGeometryObject>(
              unpackedResourceProject.project->document()->findObject(resourceObjectId))
        : std::shared_ptr<FcGeometryObject>{};
    const QString unpackedResourcePath = unpackedGeometry
        ? unpackedGeometry->geometryParameters().value(QStringLiteral("sourceFile")).toString()
        : QString{};
    check(unpackedGeometry && QFileInfo::exists(unpackedResourcePath) &&
              QFileInfo(unpackedResourcePath).absoluteFilePath().startsWith(
                  QFileInfo(resourceUnpackDirectory.path()).absoluteFilePath(),
                  Qt::CaseInsensitive) &&
              unpackedResourceProject.runtimeSettings.parallelProcessCount == 3,
          "A15 unpack relinks resource UUID properties without losing runtime settings");
    QString copiedProjectPath;
    const bool copiedResourceProject = ProjectResourceManager::copyProjectToDirectory(
        resourceProject, packageRuntime, formalResourceProjectPath,
        resourceCopyDirectory.path(), &copiedProjectPath, &packageError);
    if (!copiedResourceProject) {
        std::cerr << "A15 copy error: " << packageError.toStdString() << '\n';
    }
    check(copiedResourceProject && QFileInfo::exists(copiedProjectPath),
          "A15 Copy Project creates a self-contained project directory");

    check(GeometryImportService::detectFormat(QStringLiteral("building.STEP")) ==
              GeometryImportFormat::Step &&
              GeometryImportService::detectFormat(QStringLiteral("scene.glb")) ==
                  GeometryImportFormat::Glb,
          "A14 geometry importer detects CAD/mesh extensions case-insensitively");
    check(GeometryImportService::isSupported(GeometryImportFormat::Stl) &&
              GeometryImportService::isSupported(GeometryImportFormat::Obj) &&
              GeometryImportService::isSupported(GeometryImportFormat::Dxf) &&
              !GeometryImportService::isSupported(GeometryImportFormat::Dwg) &&
              GeometryImportService::unavailableReason(GeometryImportFormat::Dwg)
                  .contains(QStringLiteral("licensed"), Qt::CaseInsensitive),
          "A14 exposes real import capabilities and does not fake DWG support");
    QTemporaryDir importDirectory;
    const QString stlPath = importDirectory.filePath(QStringLiteral("tetra_mm.stl"));
    const QString stlText = QStringLiteral(
        "solid tetra\n"
        "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 1000 0 0\nvertex 0 1000 0\nendloop\nendfacet\n"
        "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 0 0 1000\nvertex 1000 0 0\nendloop\nendfacet\n"
        "facet normal -1 0 0\nouter loop\nvertex 0 0 0\nvertex 0 1000 0\nvertex 0 0 1000\nendloop\nendfacet\n"
        "facet normal 1 1 1\nouter loop\nvertex 1000 0 0\nvertex 0 0 1000\nvertex 0 1000 0\nendloop\nendfacet\nendsolid tetra\n");
    check(importDirectory.isValid() && writeTextFile(stlPath, stlText),
          "A14 STL fixture is writable");
    GeometryImportOptions stlOptions;
    stlOptions.sourceUnit = QStringLiteral("mm");
    stlOptions.originX = 2.0;
    const GeometryImportResult stlImport =
        GeometryImportService().importFile(stlPath, stlOptions);
    std::cout << "A14 STL import completed: triangles=" << stlImport.quality.triangles
              << ", x=[" << stlImport.quality.minimumX << ','
              << stlImport.quality.maximumX << "]\n" << std::flush;
    check(stlImport.success() && stlImport.object->hasShape() &&
              stlImport.quality.triangles == 4 &&
              std::abs(stlImport.quality.minimumX - 2.0) < 1.0e-6 &&
              std::abs(stlImport.quality.maximumX - 3.0) < 1.0e-6 &&
              stlImport.quality.closed && stlImport.quality.boundaryEdges == 0 &&
              stlImport.quality.nonManifoldEdges == 0,
          "A14 imports real STL triangles and applies units/origin in metres");
    check(stlImport.object->geometryParameters()
                  .value(QStringLiteral("sourceFile")).toString() == stlPath &&
              stlImport.object->geometryParameters()
                  .value(QStringLiteral("importFormat")).toString() == QStringLiteral("STL"),
          "A14 imported geometry preserves its source link and format metadata");

    const QString objPath = importDirectory.filePath(QStringLiteral("pyramid.obj"));
    const QString objText = QStringLiteral(
        "o pyramid\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 0.5 1\n"
        "f 1 2 3 4\nf 1 5 2\nf 2 5 3\nf 3 5 4\nf 4 5 1\n");
    check(writeTextFile(objPath, objText), "A14 OBJ fixture is writable");
    const GeometryImportResult objImport = GeometryImportService().importFile(objPath);
    std::cout << "A14 OBJ import completed.\n" << std::flush;
    check(objImport.success() && objImport.object->hasShape() &&
              objImport.quality.triangles >= 6,
          "A14 imports a real OBJ mesh through OpenCascade");

    const QString dxfPath = importDirectory.filePath(QStringLiteral("room_faces.dxf"));
    const QString dxfText = QStringLiteral(
        "0\nSECTION\n2\nENTITIES\n"
        "0\n3DFACE\n8\nWALLS\n10\n0\n20\n0\n30\n0\n"
        "11\n4\n21\n0\n31\n0\n12\n4\n22\n0\n32\n3\n"
        "13\n0\n23\n0\n33\n3\n"
        "0\nLINE\n8\nGUIDES\n10\n0\n20\n0\n30\n0\n"
        "11\n4\n21\n3\n31\n0\n0\nENDSEC\n0\nEOF\n");
    check(writeTextFile(dxfPath, dxfText), "A14 ASCII DXF fixture is writable");
    const GeometryImportResult dxfImport = GeometryImportService().importFile(dxfPath);
    check(dxfImport.success() && dxfImport.object->hasShape() &&
              dxfImport.quality.faces == 1 && dxfImport.quality.triangles == 2 &&
              dxfImport.quality.boundaryEdges == 4 && !dxfImport.quality.closed,
          "A14 imports ASCII DXF 3DFACE and LINE entities without commercial SDKs");

    const QString stepPath = importDirectory.filePath(QStringLiteral("equipment.step"));
    STEPControl_Writer stepWriter;
    const TopoDS_Shape stepSource = BRepPrimAPI_MakeBox(2.0, 1.0, 0.5).Shape();
    const bool stepWritten =
        stepWriter.Transfer(stepSource, STEPControl_AsIs) == IFSelect_RetDone &&
        stepWriter.Write(QFile::encodeName(stepPath).constData()) == IFSelect_RetDone;
    check(stepWritten, "A14 STEP fixture is generated by OpenCascade");
    const GeometryImportResult stepImport = GeometryImportService().importFile(stepPath);
    check(stepImport.success() && stepImport.object->hasShape() &&
              stepImport.quality.solids == 1 && stepImport.quality.faces == 6,
          "A14 imports a real STEP solid through OpenCascade");

    const QString igesPath = importDirectory.filePath(QStringLiteral("equipment.iges"));
    IGESControl_Writer igesWriter("M", 1);
    const bool igesWritten = igesWriter.AddShape(stepSource) &&
                             igesWriter.Write(QFile::encodeName(igesPath).constData());
    check(igesWritten, "A14 IGES fixture is generated by OpenCascade");
    const GeometryImportResult igesImport = GeometryImportService().importFile(igesPath);
    check(igesImport.success() && igesImport.object->hasShape() &&
              igesImport.quality.faces == 6,
          "A14 imports a real IGES BRep through OpenCascade");

    const FdsCsvData demoHrr = FdsCsvReader::read(
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-results/demo_hrr.csv")));
    check(demoHrr.success(), "FDS CSV reader loads a two-header-row time series");
    check(demoHrr.times.size() == 3 && demoHrr.series.size() == 1,
          "FDS CSV reader preserves frame and quantity counts");
    check(demoHrr.timeUnit == QStringLiteral("s") &&
              demoHrr.series.constFirst().name == QStringLiteral("HRR") &&
              demoHrr.series.constFirst().unit == QStringLiteral("kW"),
          "FDS CSV reader preserves semantic names and units");
    bool interpolated = false;
    check(std::abs(demoHrr.interpolatedValue(0, 1.5, &interpolated) - 15.0) < 1.0e-9 &&
              interpolated,
          "FDS CSV time interpolation is linear");
    const FdsSeriesStatistics demoStats = demoHrr.statistics(0);
    check(demoStats.sampleCount == 3 && std::abs(demoStats.minimum) < 1.0e-9 &&
              std::abs(demoStats.maximum - 20.0) < 1.0e-9 &&
              std::abs(demoStats.mean - 10.0) < 1.0e-9,
          "FDS CSV statistics report minimum, maximum, and mean");
    const FdsResultScanResult descriptorScan = FdsResultScanner().scanSmvFile(
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-results/demo.smv")));
    const auto descriptor = std::find_if(
        descriptorScan.files.cbegin(), descriptorScan.files.cend(),
        [](const FdsResultFileInfo& file) { return file.type == FcResultFileType::Slice; });
    check(descriptor != descriptorScan.files.cend() &&
              descriptor->quantity == QStringLiteral("TEMPERATURE") &&
              descriptor->unit == QStringLiteral("C"),
          "SMV scanner attaches quantity and unit metadata to binary field files");
    SmokeviewFrameRenderRequest renderRequest;
    renderRequest.outputDirectory = QStringLiteral("C:/FireCAE Frame Cache");
    renderRequest.fieldType = FcResultFileType::Smoke3D;
    renderRequest.quantity = QStringLiteral("SOOT DENSITY");
    renderRequest.times = {0.0, 2.0};
    renderRequest.filePrefix = QStringLiteral("smoke test");
    const QString renderScript = SmokeviewFrameRenderer::scriptText(renderRequest);
    check(renderScript.contains(QStringLiteral("RENDERDIR")) &&
              renderScript.contains(QStringLiteral("LOAD3DSMOKE")) &&
              renderScript.contains(QStringLiteral("SOOT DENSITY")) &&
              renderScript.count(QStringLiteral("RENDERONCE")) == 2,
          "Smokeview frame-cache script preserves output path, field, and requested times");
    SmokeviewFrameRenderRequest sliceRequest = renderRequest;
    sliceRequest.fieldType = FcResultFileType::Slice;
    sliceRequest.quantity = QStringLiteral("TEMPERATURE");
    sliceRequest.slicePlaneKeyword = QStringLiteral("PBX");
    sliceRequest.slicePlaneValue = 2.5;
    const QString renderSliceScript = SmokeviewFrameRenderer::scriptText(sliceRequest);
    check(renderSliceScript.contains(QStringLiteral("QUANTITY='TEMPERATURE' PBX=2.5")),
          "Smokeview slice render script includes the associated plane coordinate");

    FcProject project(QStringLiteral("Test"));
    FcDocument* document = project.document();

    check(document != nullptr, "Project owns a document");
    check(document->groups().size() == FcDocument::StandardGroupCount,
          "Document creates ten standard groups");
    check(document->groups().size() == 15, "Standard group count is fifteen");
    check(!project.isModified(), "A new project is not modified");

    std::set<QString> ids;
    for (const auto& group : document->groups()) {
        check(group != nullptr, "Every standard group exists");
        check(group->type() == FcObjectType::Group, "Standard objects are groups");
        check(ids.insert(group->id()).second, "Every standard group has a unique ID");
    }

    const auto geometry = document->geometryGroup();
    const auto testObject =
        std::make_shared<FcObject>(QStringLiteral("TestObject"), FcObjectType::Geometry);
    const QString testId = testObject->id();

    check(geometry->addChild(testObject), "A test object can be added to Geometry");
    check(testObject->parent() == geometry.get(), "Child stores a non-owning parent pointer");
    check(document->findObject(testId) == testObject, "Document finds an object recursively by ID");
    check(!geometry->addChild(testObject), "The same child cannot be added twice");

    check(geometry->removeChild(testId), "A child can be removed by ID");
    check(testObject->parent() == nullptr, "Removing a child clears its parent pointer");
    check(!document->findObject(testId), "Removed object is no longer found in the document");

    const auto orderingParent =
        std::make_shared<FcObject>(QStringLiteral("Ordering"), FcObjectType::Group);
    const auto firstChild =
        std::make_shared<FcObject>(QStringLiteral("First"), FcObjectType::Mesh);
    const auto secondChild =
        std::make_shared<FcObject>(QStringLiteral("Second"), FcObjectType::Mesh);
    const auto insertedChild =
        std::make_shared<FcObject>(QStringLiteral("Inserted"), FcObjectType::Mesh);
    check(orderingParent->addChild(firstChild) && orderingParent->addChild(secondChild),
          "Ordering fixture accepts ordinary children");
    check(orderingParent->insertChild(insertedChild, 1),
          "A child can be inserted at an explicit tree position");
    check(orderingParent->children().at(1) == insertedChild &&
              insertedChild->parent() == orderingParent.get(),
          "Inserted child preserves order and its non-owning parent pointer");
    check(orderingParent->moveChild(insertedChild->id(), 2),
          "A child can be moved to a new sibling position");
    check(orderingParent->children().at(2) == insertedChild,
          "Moved child occupies the requested sibling position");

    const TopoDS_Shape boxShape = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
    const auto box = std::make_shared<FcGeometryObject>(QStringLiteral("Box_001"), boxShape);
    check(box->type() == FcObjectType::Geometry, "Geometry object has Geometry type");
    check(box->hasShape(), "Geometry object stores a TopoDS_Shape");
    check(geometry->addChild(box), "Geometry object enters the document object tree");
    check(document->findObject(box->id()) == box,
          "Document resolves a geometry object by UUID");
    box->setLocked(true);
    box->setFloorName(QStringLiteral("Level 01"));
    box->setTags({QStringLiteral("structure"), QStringLiteral("fire-zone-a")});
    box->setGeometryKind(FcGeometryKind::Box);
    BuildingGeometryRequest boxRequest;
    boxRequest.kind = FcGeometryKind::Box;
    boxRequest.width = boxRequest.depth = boxRequest.height = 1.0;
    box->setGeometryParameters(
        BuildingGeometryService::requestToParameters(boxRequest));

    BuildingGeometryRequest wallRequest;
    wallRequest.kind = FcGeometryKind::Wall;
    wallRequest.x = 0.0; wallRequest.y = 0.0; wallRequest.z = 0.0;
    wallRequest.endX = 5.0; wallRequest.endY = 0.0;
    wallRequest.height = 3.0; wallRequest.thickness = 0.2;
    wallRequest.baseline = FcWallBaseline::Center;
    QString geometryError;
    const TopoDS_Shape wallShape =
        BuildingGeometryService::createShape(wallRequest, &geometryError);
    check(!wallShape.IsNull() && geometryError.isEmpty(),
          "A09 wall service creates valid oriented geometry");
    const auto wall = std::make_shared<FcGeometryObject>(
        QStringLiteral("Wall_001"), wallShape);
    wall->setGeometryKind(FcGeometryKind::Wall);
    wall->setGeometryParameters(
        BuildingGeometryService::requestToParameters(wallRequest));
    wall->setDefaultSurfaceId(QStringLiteral("{a70714bc-aa00-4ad9-9b7f-754c61ca35f4}"));
    check(geometry->addChild(wall), "A09 semantic wall enters the UUID object tree");

    BuildingGeometryRequest doorRequest;
    doorRequest.kind = FcGeometryKind::Door;
    doorRequest.x = 1.0; doorRequest.y = -0.1; doorRequest.z = 0.0;
    doorRequest.width = 1.0; doorRequest.depth = 0.2; doorRequest.height = 2.1;
    const TopoDS_Shape doorShape =
        BuildingGeometryService::createShape(doorRequest, &geometryError);
    const auto door = std::make_shared<FcGeometryObject>(
        QStringLiteral("Door_001"), doorShape);
    door->setGeometryKind(FcGeometryKind::Door);
    door->setGeometryParameters(
        BuildingGeometryService::requestToParameters(doorRequest));
    door->setHostObjectId(wall->id());
    check(geometry->addChild(door) && door->hostObjectId() == wall->id(),
          "A09 opening stores its host by UUID");

    const GeometryValidationResult wallValidation =
        BuildingGeometryService::validate(wallShape);
    check(wallValidation.valid && wallValidation.closed,
          "A09 geometry validation recognizes a closed wall solid");
    check(wallValidation.faceCount == 6 && wallValidation.vertexCount == 8 &&
              !wallValidation.hasDuplicateFaces && !wallValidation.hasDuplicateVertices,
          "R01 geometry quality reports face/vertex counts and duplicate faces");
    BRep_Builder duplicateBuilder;
    TopoDS_Compound duplicateVertices;
    duplicateBuilder.MakeCompound(duplicateVertices);
    duplicateBuilder.Add(duplicateVertices, BRepBuilderAPI_MakeVertex(gp_Pnt()).Shape());
    duplicateBuilder.Add(duplicateVertices, BRepBuilderAPI_MakeVertex(gp_Pnt()).Shape());
    const auto coincidentValidation = BuildingGeometryService::validate(duplicateVertices);
    check(coincidentValidation.vertexCount == 2 && coincidentValidation.duplicateVertexCount == 1,
          "Distinct coincident vertices are still diagnosed, unlike shared incident vertex occurrences");
    const QVector<GeometryFaceInfo> wallFaces =
        BuildingGeometryService::faceInfos(wallShape);
    QSet<QString> wallFaceKeys;
    for (const GeometryFaceInfo& face : wallFaces) wallFaceKeys.insert(face.key);
    check(wallFaces.size() == 6 && wallFaceKeys.size() == 6 &&
              wallFaces.constFirst().key.startsWith(QStringLiteral("TopoFace:")),
          "R01 assigns stable identities to real OpenCascade faces");
    const FdsGeomTriangulation wallTriangles =
        BuildingGeometryService::triangulateForFds(wallShape);
    check(wallTriangles.valid() && wallTriangles.vertices.size() == 24 &&
              wallTriangles.triangles.size() == 12,
          "R01 triangulates a closed wall to deduplicated FDS GEOM vertices/faces");

    const BuildingGeometryRequest rightBaseline =
        BuildingGeometryService::rebaseWall(
            wallRequest, FcWallBaseline::Right, true);
    const TopoDS_Shape rightBaselineShape =
        BuildingGeometryService::createShape(rightBaseline, &geometryError);
    FcGeometryObject rightBaselineObject(QStringLiteral("right"), rightBaselineShape);
    FcGeometryObject centerBaselineObject(QStringLiteral("center"), wallShape);
    const FcFdsBounds centerBounds =
        FdsBlockConversionService::boundsForShape(centerBaselineObject);
    const FcFdsBounds rightBounds =
        FdsBlockConversionService::boundsForShape(rightBaselineObject);
    check(std::abs(centerBounds.xMin - rightBounds.xMin) < 1.0e-8 &&
              std::abs(centerBounds.xMax - rightBounds.xMax) < 1.0e-8 &&
              std::abs(centerBounds.yMin - rightBounds.yMin) < 1.0e-8 &&
              std::abs(centerBounds.yMax - rightBounds.yMax) < 1.0e-8,
          "R01 baseline switching preserves the physical wall footprint");

    QString connectedWallId;
    const QPointF connected = BuildingGeometryService::snapWallEndpoint(
        QPointF(5.04, 0.02), {wall}, 0.1, &connectedWallId);
    check(connected == QPointF(5.0, 0.0) && connectedWallId == wall->id(),
          "R01 endpoint auto-connection snaps to an existing wall UUID");
    const QPointF teeConnection = BuildingGeometryService::snapWallEndpoint(
        QPointF(2.5, 0.04), {wall}, 0.1, &connectedWallId);
    check(std::abs(teeConnection.x() - 2.5) < 1.0e-9 &&
              std::abs(teeConnection.y()) < 1.0e-9 &&
              connectedWallId == wall->id(),
          "R01 wall endpoint snaps to an existing wall segment for a UUID-backed T junction");

    const BuildingGeometryRequest attachedDoor =
        BuildingGeometryService::attachOpeningToWall(doorRequest, wallRequest);
    BuildingGeometryRequest movedWall = wallRequest;
    movedWall.x = 2.0; movedWall.y = 1.0;
    movedWall.endX = 2.0; movedWall.endY = 6.0;
    const BuildingGeometryRequest followedDoor =
        BuildingGeometryService::followEditedHostWall(
            attachedDoor, wallRequest, movedWall);
    check(std::abs(followedDoor.x - 2.1) < 1.0e-8 &&
              std::abs(followedDoor.y - 2.0) < 1.0e-8 &&
              std::abs(followedDoor.rotationDegrees - 90.0) < 1.0e-8,
          "R01 hosted openings follow wall translation and rotation");
    const TopoDS_Shape cutShape = BuildingGeometryService::booleanOperation(
        wallShape, doorShape, FcBooleanOperation::Difference, &geometryError);
    check(!cutShape.IsNull() && geometryError.isEmpty(),
          "A09 guarded boolean difference creates a valid door opening");

    const auto conversionMesh = std::make_shared<FcFdsMesh>(
        QStringLiteral("A09 Mesh"), QStringLiteral("A09_MESH"),
        std::array<int, 3>{50, 40, 30},
        FcFdsBounds{-1.0, 9.0, -1.0, 7.0, 0.0, 6.0});
    const FcFdsBounds thinRequested{4.9, 5.1, 1.0, 2.0, 0.0, 3.0};
    const FcFdsBounds thinActual =
        FdsBlockConversionService::snapBounds(thinRequested, *conversionMesh);
    check(thinActual.xMin <= thinRequested.xMin &&
              thinActual.xMax >= thinRequested.xMax &&
              thinActual.xMax > thinActual.xMin,
          "R01 mesh snapping encloses thin solids instead of collapsing them");
    const FcFdsBounds planarRequested{5.0, 5.0, 1.0, 2.0, 0.0, 2.0};
    const FcFdsBounds planarActual =
        FdsBlockConversionService::snapBounds(planarRequested, *conversionMesh);
    check(std::abs(planarActual.xMax - planarActual.xMin) < 1.0e-12,
          "R01 mesh snapping preserves planar VENT thickness");
    const FdsBlockConversionResult blockConversion =
        FdsBlockConversionService::convert({wall, door}, {conversionMesh});
    check(blockConversion.success() && blockConversion.previews.size() == 2 &&
              blockConversion.fdsObjects.size() == 2,
          "A09 conversion previews source geometry without changing it");
    check(blockConversion.previews[0].target == FdsBlockTarget::Obstruction &&
              blockConversion.previews[1].target == FdsBlockTarget::Hole,
          "A09 conversion maps walls to OBST and doors to HOLE");
    check(wall->geometryKind() == FcGeometryKind::Wall && wall->shape().IsSame(wallShape),
          "A09 mesh snapping leaves original CAD geometry unchanged");
    const auto referenceWall = std::make_shared<FcGeometryObject>(
        QStringLiteral("Drawing Reference"), wallShape);
    referenceWall->setGeometryKind(FcGeometryKind::Wall);
    QVariantMap referenceParameters = wall->geometryParameters();
    referenceParameters.insert(QStringLiteral("fdsConversionRoute"),
                               QStringLiteral("REFERENCE"));
    referenceWall->setGeometryParameters(referenceParameters);
    const FdsBlockConversionResult referenceConversion =
        FdsBlockConversionService::convert({referenceWall}, {conversionMesh});
    check(referenceConversion.success() && referenceConversion.fdsObjects.isEmpty() &&
              std::any_of(referenceConversion.warnings.cbegin(),
                          referenceConversion.warnings.cend(),
                          [](const QString& warning) {
                              return warning.contains(QStringLiteral("REFERENCE"));
                          }),
          "P19 reference-only geometry is preserved but omitted from FDS blocks");
    BuildingGeometryRequest geomWallRequest = wallRequest;
    geomWallRequest.fdsConversionRoute = QStringLiteral("GEOM");
    const auto geomWall = std::make_shared<FcGeometryObject>(
        QStringLiteral("GEOM Wall"), wallShape);
    geomWall->setGeometryKind(FcGeometryKind::Wall);
    geomWall->setGeometryParameters(
        BuildingGeometryService::requestToParameters(geomWallRequest));
    geomWall->setDefaultSurfaceId(wall->defaultSurfaceId());
    if (!wallFaces.isEmpty()) {
        geomWall->setFaceSurfaceIds({{wallFaces.constFirst().key,
                                      wall->defaultSurfaceId()}});
    }
    const FdsBlockConversionResult geomConversion =
        FdsBlockConversionService::convert({geomWall}, {conversionMesh});
    bool hasVerts = false;
    bool hasFaces = false;
    bool hasBounds = false;
    const auto geomNamelist = geomConversion.fdsObjects.isEmpty()
                                  ? std::shared_ptr<FcFdsNamelist>{}
                                  : std::dynamic_pointer_cast<FcFdsNamelist>(
                                        geomConversion.fdsObjects.constFirst());
    if (geomNamelist) {
        for (const FcFdsParameter& parameter : geomNamelist->parameters()) {
            hasVerts = hasVerts || parameter.key == QStringLiteral("VERTS");
            hasFaces = hasFaces || parameter.key == QStringLiteral("FACES");
            hasBounds = hasBounds || parameter.key == QStringLiteral("XB");
        }
    }
    check(geomConversion.success() && geomConversion.previews.size() == 1 &&
              geomConversion.previews.constFirst().target == FdsBlockTarget::Geom &&
              geomNamelist && hasVerts && hasFaces && !hasBounds &&
              std::abs(geomConversion.previews.constFirst().volumeErrorPercent) < 1.0e-12,
          "R01 native GEOM route writes real VERTS/FACES without rasterizing or mutating CAD");
    project.setDisplayUnit(FcDisplayUnit::Millimeters);
    if (!wallFaces.isEmpty()) {
        wall->setFaceSurfaceIds({{wallFaces.constFirst().key,
                                  wall->defaultSurfaceId()}});
    }
    check(project.metersToDisplay(1.0) == 1000.0 &&
              project.displayToMeters(2500.0) == 2.5,
          "Display units convert input and output while preserving SI geometry");
    auto floor = std::make_shared<FcFloorObject>(QStringLiteral("Level 02"));
    floor->setBaseElevation(3.6);
    floor->setDefaultStoreyHeight(3.4);
    floor->setDefaultSlabThickness(0.18);
    floor->setDefaultWallHeight(3.1);
    floor->setBackgroundImagePath(QStringLiteral("plans/level-02.png"));
    floor->setClippingEnabled(true);
    floor->setClippingRange({-10.0, 30.0, -5.0, 20.0, 3.6, 7.0});
    project.document()->geometryGroup()->addChild(floor);
    QTemporaryDir projectRoundTripDirectory;
    const QString projectRoundTripPath =
        QDir(projectRoundTripDirectory.path()).filePath(QStringLiteral("a08.firecae"));
    QString projectSaveError;
    check(projectRoundTripDirectory.isValid() &&
              FcProjectSerializer::save(project, projectRoundTripPath, &projectSaveError),
          "A08 project with native geometry can be saved");
    FcProjectLoadResult projectRoundTrip =
        FcProjectSerializer::load(projectRoundTripPath);
    check(projectRoundTrip.success(), "A08 project can be reopened");
    if (projectRoundTrip.success()) {
        const auto restored = std::dynamic_pointer_cast<FcGeometryObject>(
            projectRoundTrip.project->document()->findObject(box->id()));
        check(restored && restored->hasShape(),
              "Native OpenCascade BREP geometry survives project reopen");
        check(restored && restored->id() == box->id() && restored->isLocked() &&
                  restored->floorName() == QStringLiteral("Level 01") &&
                  restored->tags().contains(QStringLiteral("fire-zone-a")),
              "UUID, lock, floor and tags survive project reopen");
        const auto restoredWall = std::dynamic_pointer_cast<FcGeometryObject>(
            projectRoundTrip.project->document()->findObject(wall->id()));
        const auto restoredDoor = std::dynamic_pointer_cast<FcGeometryObject>(
            projectRoundTrip.project->document()->findObject(door->id()));
        check(restoredWall && restoredWall->geometryKind() == FcGeometryKind::Wall &&
                  restoredWall->geometryParameters().value(QStringLiteral("thickness")).toDouble() == 0.2,
              "A09 semantic geometry kind and parameters survive project reopen");
        check(restoredWall && !restoredWall->faceSurfaceIds().isEmpty() &&
                  restoredWall->faceSurfaceIds().cbegin().key().startsWith(
                      QStringLiteral("TopoFace:")),
              "R01 topological face surface UUID assignments survive save/reopen");
        check(restoredDoor && restoredDoor->geometryKind() == FcGeometryKind::Door &&
                  restoredDoor->hostObjectId() == restoredWall->id(),
              "A09 opening host UUID survives project reopen");
        check(projectRoundTrip.project->displayUnit() == FcDisplayUnit::Millimeters,
              "Project display unit survives project reopen");
        const auto restoredFloor = std::dynamic_pointer_cast<FcFloorObject>(
            projectRoundTrip.project->document()->findObject(floor->id()));
        check(restoredFloor && restoredFloor->id() == floor->id() &&
                  std::abs(restoredFloor->baseElevation() - 3.6) < 1.0e-12 &&
                  std::abs(restoredFloor->defaultStoreyHeight() - 3.4) < 1.0e-12 &&
                  std::abs(restoredFloor->defaultSlabThickness() - 0.18) < 1.0e-12 &&
                  std::abs(restoredFloor->defaultWallHeight() - 3.1) < 1.0e-12 &&
                  restoredFloor->backgroundImagePath() ==
                      QStringLiteral("plans\\level-02.png") &&
                  restoredFloor->clippingEnabled() &&
                  std::abs(restoredFloor->clippingRange().zMax - 7.0) < 1.0e-12,
              "P19 typed floor properties and UUID survive project reopen");
    }

    const QString ifcPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                .filePath(QStringLiteral("tessellated-item.ifc"));
    QTemporaryDir glbFixtureDirectory;
    const QString glbPath =
        glbFixtureDirectory.filePath(QStringLiteral("ifc_material.glb"));
    QProcess ifcConverter;
    ifcConverter.setProgram(IfcImportService().converterPath());
    ifcConverter.setArguments({QStringLiteral("--no-progress"),
                               QStringLiteral("--use-element-guids"),
                               ifcPath, glbPath});
    ifcConverter.start();
    const bool glbWritten = ifcConverter.waitForStarted(10000) &&
                            ifcConverter.waitForFinished(120000) &&
                            ifcConverter.exitStatus() == QProcess::NormalExit &&
                            ifcConverter.exitCode() == 0 && QFileInfo::exists(glbPath);
    check(glbWritten, "A14 GLB fixture is generated through the isolated IFC worker");
    const GeometryImportResult glbImport = GeometryImportService().importFile(glbPath);
    std::cout << "A14 GLB materials=" << glbImport.quality.materialCount
              << ", textures=" << glbImport.quality.textureCount << "\n" << std::flush;
    check(glbImport.success() && glbImport.object->hasShape() &&
              glbImport.quality.materialCount > 0 &&
              glbImport.object->geometryParameters().contains(
                  QStringLiteral("displayColorRed")),
          "A14 imports GLB material metadata and preserves a visible base color");
    const IfcImportResult importResult = IfcImportService().importFile(ifcPath);
    check(importResult.success(), "IfcOpenShell imports the IFC fixture");
    if (importResult.rootObject) {
        const auto& ifcRoot = importResult.rootObject;
        check(ifcRoot->type() == FcObjectType::IfcModel,
              "IFC decomposition root has IFC Model type");
        check(ifcRoot->ifcClass() == QStringLiteral("IfcProject"),
              "IFC project class is preserved");
        check(ifcRoot->globalId() == QStringLiteral("0xScRe4drECQ4DMSqUjd6d"),
              "IFC GlobalId is preserved separately from the FireCAE UUID");
        check(ifcRoot->id() != ifcRoot->globalId(),
              "FireCAE UUID is not replaced by IFC GlobalId");
        check(ifcRoot->schema() == QStringLiteral("IFC4"),
              "IFC schema is preserved");
        check(!ifcRoot->hasShape(),
              "IFC model root is organizational and has no aggregate shape");
        check(ifcRoot->children().size() == 1,
              "IFC decomposition hierarchy is preserved");
        const auto building =
            std::dynamic_pointer_cast<FcIfcObject>(ifcRoot->children().front());
        const auto product = building && !building->children().empty()
                                 ? std::dynamic_pointer_cast<FcIfcObject>(
                                       building->children().front())
                                 : std::shared_ptr<FcIfcObject>{};
        check(product && product->hasShape(),
              "IFC product receives its own OpenCascade shape");
        check(product && product->id() != product->globalId(),
              "IFC product display identity remains the FireCAE UUID");
        check(product &&
                  product->tags().contains(
                      QStringLiteral("ifc-class:%1").arg(product->ifcClass())) &&
                  product->tags().contains(
                      QStringLiteral("ifc-globalid:%1").arg(product->globalId())),
              "A14 IFC class and GlobalId are indexed for model-tree property filtering");
        check(importResult.geometryObjectCount == 1 && ifcShapeCount(ifcRoot) == 1,
              "Fixture maps exactly one GLB product to one semantic object");
        if (product) {
            auto ifcProxy = std::make_shared<FcGeometryObject>(product->name(),
                                                               product->shape());
            ifcProxy->restorePersistentId(product->id());
            QVariantMap ifcParameters;
            ifcParameters.insert(QStringLiteral("sourceIfcGlobalId"), product->globalId());
            ifcParameters.insert(QStringLiteral("sourceIfcClass"), product->ifcClass());
            ifcProxy->setGeometryParameters(ifcParameters);
            const FdsBlockConversionResult ifcConversion =
                FdsBlockConversionService::convert({ifcProxy}, {conversionMesh});
            check(ifcConversion.success() && ifcConversion.fdsObjects.size() == 1 &&
                      ifcConversion.previews.constFirst().sourceObjectId == product->id() &&
                      ifcConversion.fdsObjects.constFirst()->tags().contains(
                          QStringLiteral("source-ifc-globalid:%1").arg(product->globalId())),
                  "A14 reviewed BIM-to-FDS conversion preserves IFC UUID and GlobalId linkage");
        }
        check(document->geometryGroup()->addChild(ifcRoot),
              "IFC model enters the document object tree");
        check(document->findObject(ifcRoot->children().front()->id()) ==
                  ifcRoot->children().front(),
              "IFC semantic nodes resolve recursively by FireCAE UUID");
        QTemporaryDir ifcRoundTripDirectory;
        const QString ifcProjectPath =
            QDir(ifcRoundTripDirectory.path()).filePath(QStringLiteral("a14-ifc.firecae"));
        QString ifcSaveError;
        check(ifcRoundTripDirectory.isValid() &&
                  FcProjectSerializer::save(project, ifcProjectPath, &ifcSaveError),
              "A14 IFC project saves without flattening the semantic tree");
        const FcProjectLoadResult restoredIfcProject =
            FcProjectSerializer::load(ifcProjectPath);
        const auto restoredIfcRoot = restoredIfcProject.success()
            ? std::dynamic_pointer_cast<FcIfcObject>(
                  restoredIfcProject.project->document()->findObject(ifcRoot->id()))
            : std::shared_ptr<FcIfcObject>{};
        const auto restoredIfcProduct = restoredIfcProject.success() && product
            ? std::dynamic_pointer_cast<FcIfcObject>(
                  restoredIfcProject.project->document()->findObject(product->id()))
            : std::shared_ptr<FcIfcObject>{};
        check(restoredIfcRoot && restoredIfcRoot->ifcClass() == QStringLiteral("IfcProject") &&
                  restoredIfcRoot->globalId() == ifcRoot->globalId() &&
                  restoredIfcProduct && restoredIfcProduct->hasShape() &&
                  restoredIfcProduct->globalId() == product->globalId(),
              "A14 IFC UUID, GlobalId, hierarchy and component BREP survive save/reopen");
    }

    const QString resultDataDirectory =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(QStringLiteral("fds-results"));
    const FdsResultScanner resultScanner;
    const QStringList smvFiles = resultScanner.findSmvFiles(resultDataDirectory);
    check(smvFiles.size() == 2, "Result scanner discovers SMV files in a directory");
    const FdsResultScanResult resultScan = resultScanner.scanSmvFile(
        QDir(resultDataDirectory).filePath(QStringLiteral("demo.smv")));
    check(resultScan.success(), "Result scanner reads a valid SMV case");
    check(resultScan.caseName == QStringLiteral("demo"),
          "Result scanner derives the case name from SMV");
    check(resultScan.status == FcResultStatus::Ready,
          "Complete result fixture has Ready status");
    const std::vector<std::pair<QString, FcResultFileType>> expectedDemoFiles{
        {QStringLiteral("demo.fds"), FcResultFileType::FdsInput},
        {QStringLiteral("demo.smv"), FcResultFileType::Smokeview},
        {QStringLiteral("demo_hrr.csv"), FcResultFileType::HeatReleaseRate},
        {QStringLiteral("demo_devc.csv"), FcResultFileType::Devices},
        {QStringLiteral("demo_0001.sf"), FcResultFileType::Slice}};
    check(resultScan.files.size() == expectedDemoFiles.size() &&
              std::all_of(expectedDemoFiles.begin(), expectedDemoFiles.end(),
                  [&](const auto& expected) {
                      return std::any_of(resultScan.files.begin(), resultScan.files.end(),
                          [&](const FdsResultFileInfo& file) {
                              return file.name == expected.first && file.type == expected.second;
                          });
                  }),
          "Result scanner classifies the five demo files and excludes neighboring missing.smv");
    check(resultScan.startTime == QStringLiteral("0.0") &&
              resultScan.endTime == QStringLiteral("2.0"),
          "Result scanner extracts the CSV time range");

    QTemporaryDir sliceDirectory;
    const QString littleSlice =
        sliceDirectory.filePath(QStringLiteral("little.sf"));
    const QString bigSlice =
        sliceDirectory.filePath(QStringLiteral("big.sf"));
    check(sliceDirectory.isValid() &&
              writeSliceFixture(littleSlice, QDataStream::LittleEndian) &&
              writeSliceFixture(bigSlice, QDataStream::BigEndian),
          "P31 classic FDS slice fixtures can be written in both byte orders");
    const FdsSliceData littleSliceData = FdsSliceReader::read(littleSlice);
    const FdsSliceData bigSliceData = FdsSliceReader::read(bigSlice);
    check(littleSliceData.success() && littleSliceData.littleEndian &&
              littleSliceData.longLabel == QStringLiteral("TEMPERATURE") &&
              littleSliceData.shortLabel == QStringLiteral("temp") &&
              littleSliceData.unit == QStringLiteral("C") &&
              littleSliceData.nx == 2 && littleSliceData.ny == 3 &&
              littleSliceData.nz == 1 && littleSliceData.isPlanar() &&
              littleSliceData.frames.size() == 2 &&
              std::abs(littleSliceData.frames.at(1).time - 0.5F) < 1.0e-6F &&
              std::abs(littleSliceData.value(1, 1, 2, 0) - 15.0F) < 1.0e-6F,
          "P31 decodes classic little-endian FDS SLCF labels, bounds, time, and values");
    check(bigSliceData.success() && !bigSliceData.littleEndian &&
              bigSliceData.frames.size() == 2 &&
              std::abs(bigSliceData.value(0, 1, 2, 0) - 5.0F) < 1.0e-6F,
          "P31 decodes classic big-endian Fortran slice records");
    FdsSliceImageOptions sliceImageOptions;
    sliceImageOptions.frameIndex = 1;
    const FdsSliceImageResult sliceImage =
        FdsSliceImageRenderer::render(littleSliceData, sliceImageOptions);
    check(sliceImage.success() && sliceImage.image.size() == QSize(2, 3) &&
              sliceImage.horizontalAxis == QStringLiteral("X") &&
              sliceImage.verticalAxis == QStringLiteral("Y") &&
              sliceImage.minimum == 0.0F && sliceImage.maximum == 15.0F &&
              sliceImage.image.pixelColor(0, 0) !=
                  sliceImage.image.pixelColor(1, 2),
          "P31 renders a decoded planar SLCF frame with global color bounds and axis metadata");
    FdsSliceReader::Limits strictSliceLimits;
    strictSliceLimits.maximumValuesPerFrame = 5;
    const FdsSliceData rejectedLargeSlice =
        FdsSliceReader::read(littleSlice, strictSliceLimits);
    check(!rejectedLargeSlice.success() &&
              rejectedLargeSlice.errorMessage.contains(
                  QStringLiteral("safety limit"), Qt::CaseInsensitive),
          "P31 rejects a slice whose dimensions exceed configured memory limits");
    const QString truncatedSlice =
        sliceDirectory.filePath(QStringLiteral("truncated.sf"));
    check(writeTextFile(truncatedSlice, QStringLiteral("not-a-slice")) &&
              !FdsSliceReader::read(truncatedSlice).success(),
          "P31 rejects truncated or unsupported slice files without exposing partial frames");
    QTemporaryDir timelineDirectory;
    const QString timelineSmv = timelineDirectory.filePath(QStringLiteral("demo.smv"));
    const bool timelineFixture = timelineDirectory.isValid() &&
        QFile::copy(QDir(resultDataDirectory).filePath(QStringLiteral("demo.smv")),
                    timelineSmv) &&
        QFile::copy(QDir(resultDataDirectory).filePath(QStringLiteral("demo.fds")),
                    timelineDirectory.filePath(QStringLiteral("demo.fds"))) &&
        QFile::copy(QDir(resultDataDirectory).filePath(QStringLiteral("demo_hrr.csv")),
                    timelineDirectory.filePath(QStringLiteral("demo_hrr.csv"))) &&
        QFile::copy(QDir(resultDataDirectory).filePath(QStringLiteral("demo_devc.csv")),
                    timelineDirectory.filePath(QStringLiteral("demo_devc.csv"))) &&
        writeTextFile(timelineDirectory.filePath(QStringLiteral("demo_steps.csv")),
                      QStringLiteral(",,s,s,s\n"
                                     "Time Step,Wall Time,Step Size,Simulation Time,CPU Time\n"
                                     "1,2026-01-01T00:00:00,0.1,0.1,0.1\n"
                                     "99,2026-01-01T00:00:01,0.1,2.0,1.0\n"));
    const FdsResultScanResult timelineScan =
        timelineFixture ? resultScanner.scanSmvFile(timelineSmv)
                        : FdsResultScanResult{};
    check(timelineFixture && timelineScan.success() &&
              timelineScan.endTime == QStringLiteral("2.0"),
          "A16 physical end time ignores Time Step and CPU summary axes");

    const FdsResultComparison identicalComparison =
        FdsResultComparator::compareSmvFiles(resultScan.smvFilePath,
                                              resultScan.smvFilePath);
    if (!identicalComparison.success() || !identicalComparison.passed()) {
        std::cerr << "Identical comparison error: "
                  << identicalComparison.errorMessage.toStdString() << '\n'
                  << "matched files=" << identicalComparison.matchedFileCount
                  << " quantities=" << identicalComparison.quantities.size()
                  << " missing files="
                  << identicalComparison.missingCandidateFiles.join(',').toStdString()
                  << " missing quantities="
                  << identicalComparison.missingCandidateQuantities.join(',').toStdString()
                  << " warnings="
                  << identicalComparison.warnings.join('|').toStdString() << '\n';
    }
    check(identicalComparison.success() && identicalComparison.passed(),
          "Result comparator accepts identical FDS physical CSV output");
    check(!identicalComparison.quantities.empty() &&
              identicalComparison.csvReport().contains(QStringLiteral("PASS")),
          "Result comparator exposes quantity metrics and a CSV report");
    check(!identicalComparison.referenceProvenance.fdsInputSha256.isEmpty() &&
              identicalComparison.referenceProvenance.fdsInputSha256 ==
                  identicalComparison.candidateProvenance.fdsInputSha256 &&
              identicalComparison.csvReport().contains(
                  QStringLiteral("fds_input_sha256")),
          "Result comparison records input provenance and matching SHA-256 values");
    check(!identicalComparison.quantities.empty() &&
              identicalComparison.quantities.front().sampleTimes.size() ==
                  identicalComparison.quantities.front().sampleCount &&
              identicalComparison.csvReport().contains(
                  QStringLiteral("reference_peak_time")) &&
              identicalComparison.htmlReport().contains(
                  QStringLiteral("data:image/png;base64")),
          "A16 result comparison reports mean/peak timing and time-series plots");

    QTemporaryDir inputComparisonDirectory;
    const QString referenceInput = inputComparisonDirectory.filePath(
        QStringLiteral("reference.fds"));
    const QString reorderedInput = inputComparisonDirectory.filePath(
        QStringLiteral("reordered.fds"));
    const QString changedInput = inputComparisonDirectory.filePath(
        QStringLiteral("changed.fds"));
    check(inputComparisonDirectory.isValid() &&
              writeTextFile(referenceInput,
                            QStringLiteral(
                                "&HEAD CHID='reference', TITLE='Reference case' /\n"
                                "&MESH ID='M1', IJK=10,20,5, XB=0,10,0,20,0,5 /\n"
                                "&OBST XB=0,1,0,1,0,1, SURF_ID='INERT' /\n"
                                "&OBST XB=2,3,0,1,0,1, SURF_ID='INERT' /\n"
                                "&TAIL /\n")) &&
              writeTextFile(reorderedInput,
                            QStringLiteral(
                                "&HEAD TITLE='Candidate case', CHID='candidate' /\n"
                                "&OBST SURF_ID='INERT', XB=2.0,3.0,0.,1.,0.,1. /\n"
                                "&MESH XB=0.,10.,0.,20.,0.,5., IJK=10.,20.,5., ID='M1' /\n"
                                "&OBST SURF_ID='INERT', XB=0.,1.,0.,1.,0.,1. /\n"
                                "&TAIL /\n")) &&
              writeTextFile(changedInput,
                            QStringLiteral(
                                "&HEAD CHID='candidate', TITLE='Candidate case' /\n"
                                "&MESH ID='M1', IJK=11,20,5, XB=0,10,0,20,0,5 /\n"
                                "&OBST XB=0,1,0,1,0,1, SURF_ID='INERT' /\n"
                                "&OBST XB=2,3,0,1,0,1, SURF_ID='INERT' /\n"
                                "&TAIL /\n")),
          "A16 FDS input comparison fixtures can be written");
    const FdsInputComparison reorderedComparison =
        FdsInputComparator::compareFiles(referenceInput, reorderedInput);
    if (!reorderedComparison.semanticallyEquivalent()) {
        for (const FdsInputDifference& difference : reorderedComparison.differences) {
            std::cerr << "A16 diff " << difference.recordKey.toStdString() << "/"
                      << difference.parameter.toStdString() << " ref="
                      << difference.referenceValue.toStdString() << " cand="
                      << difference.candidateValue.toStdString() << " acceptable="
                      << difference.acceptable << "\n";
        }
    }
    check(reorderedComparison.success() &&
              reorderedComparison.semanticallyEquivalent() &&
              reorderedComparison.unacceptableDifferenceCount() == 0 &&
              reorderedComparison.referenceCellCount == 1000 &&
              reorderedComparison.candidateCellCount == 1000,
          "A16 semantic input comparison ignores record order and numeric formatting");
    check(!reorderedComparison.differences.isEmpty() &&
              std::all_of(reorderedComparison.differences.cbegin(),
                          reorderedComparison.differences.cend(),
                          [](const FdsInputDifference& difference) {
                              return difference.acceptable;
                          }) &&
              reorderedComparison.rawTextDiff.contains(QStringLiteral("--- reference")),
          "A16 semantic input comparison explains acceptable CHID/TITLE and raw text differences");
    const FdsInputComparison changedComparison =
        FdsInputComparator::compareFiles(referenceInput, changedInput);
    check(changedComparison.success() && !changedComparison.semanticallyEquivalent() &&
              changedComparison.candidateCellCount == 1100 &&
              changedComparison.unacceptableDifferenceCount() > 0,
          "A16 semantic input comparison rejects a changed mesh and reports cell statistics");
    const FdsImportResult mappingProject = FdsImporter().importFile(referenceInput);
    check(mappingProject.success() &&
              !FdsInputComparator::uuidToFdsIdMappings(*mappingProject.project).isEmpty(),
          "A16 exposes FireCAE UUID-to-FDS-ID mappings from the business object tree");

    QTemporaryDir comparisonDirectory;
    check(comparisonDirectory.isValid(),
          "Temporary result comparison directory can be created");
    const QString candidateSmv =
        QDir(comparisonDirectory.path()).filePath(QStringLiteral("candidate.smv"));
    const QString candidateHrr =
        QDir(comparisonDirectory.path()).filePath(QStringLiteral("candidate_hrr.csv"));
    const QString candidateDevc =
        QDir(comparisonDirectory.path()).filePath(QStringLiteral("candidate_devc.csv"));
    check(writeTextFile(candidateSmv,
                        QStringLiteral("candidate_hrr.csv\ncandidate_devc.csv\n")) &&
              writeTextFile(candidateHrr,
                            QStringLiteral("\"s\",\"kW\"\n"
                                           "\"Time\",\"HRR\"\n"
                                           "0.0,0.0\n1.0,11.0\n2.0,22.0\n")) &&
              QFile::copy(QDir(resultDataDirectory).filePath(
                              QStringLiteral("demo_devc.csv")),
                          candidateDevc),
          "Perturbed comparison fixture can be prepared");
    const FdsResultComparison perturbedComparison =
        FdsResultComparator::compareSmvFiles(resultScan.smvFilePath,
                                              candidateSmv,
                                              0.05,
                                              1.0e-9);
    if (!perturbedComparison.success()) {
        std::cerr << "Perturbed comparison error: "
                  << perturbedComparison.errorMessage.toStdString()
                  << " warnings="
                  << perturbedComparison.warnings.join('|').toStdString() << '\n';
    }
    check(perturbedComparison.success() && !perturbedComparison.passed(),
          "Result comparator rejects a physical quantity outside tolerance");
    check(std::any_of(perturbedComparison.quantities.cbegin(),
                      perturbedComparison.quantities.cend(),
                      [](const FdsCsvQuantityComparison& quantity) {
                          return quantity.quantity == QStringLiteral("HRR") &&
                                 !quantity.withinTolerance;
                      }),
          "Result comparator reports the failing HRR quantity");

    const FdsResultScanResult missingScan = resultScanner.scanSmvFile(
        QDir(resultDataDirectory).filePath(QStringLiteral("missing.smv")));
    check(missingScan.success() && missingScan.status == FcResultStatus::MissingFiles,
          "Missing referenced results produce a warning status instead of failure");
    check(!missingScan.warnings.isEmpty(),
          "Missing referenced results provide readable warnings");

    QTemporaryDir threeWayDirectory;
    const auto copyResultFixture = [&](const QString& sourceName,
                                       const QString& projectSuffix) {
        const QString sourceDirectory =
            QDir(threeWayDirectory.path()).filePath(sourceName);
        QDir().mkpath(sourceDirectory);
        const QStringList fixtureFiles = {
            QStringLiteral("demo.smv"), QStringLiteral("demo.fds"),
            QStringLiteral("demo_hrr.csv"), QStringLiteral("demo_devc.csv")};
        bool copied = true;
        for (const QString& fixtureFile : fixtureFiles) {
            copied = QFile::copy(QDir(resultDataDirectory).filePath(fixtureFile),
                                 QDir(sourceDirectory).filePath(fixtureFile)) && copied;
        }
        copied = writeTextFile(
                     QDir(sourceDirectory).filePath(QStringLiteral("demo.out")),
                     QStringLiteral("Revision : FDS-6.11.1-0-g0000000\n"
                                    "Total Time: 1.0 s\n"
                                    "STOP: FDS completed successfully\n")) && copied;
        if (!projectSuffix.isEmpty()) {
            copied = writeTextFile(
                         QDir(sourceDirectory).filePath(
                             QStringLiteral("demo.") + projectSuffix),
                         QStringLiteral("independent project evidence\n")) && copied;
        }
        return copied;
    };
    const bool threeWayFixtures = threeWayDirectory.isValid() &&
        copyResultFixture(QStringLiteral("native"), QString()) &&
        copyResultFixture(QStringLiteral("firecae"), QStringLiteral("firecae")) &&
        copyResultFixture(QStringLiteral("pyrosim"), QStringLiteral("psm"));
    const QString readyManifest =
        threeWayDirectory.filePath(QStringLiteral("ready-manifest.json"));
    const QString pendingManifest =
        threeWayDirectory.filePath(QStringLiteral("pending-manifest.json"));
    const QString sharedManifest =
        threeWayDirectory.filePath(QStringLiteral("shared-manifest.json"));
    const QString readyManifestText = QStringLiteral(R"JSON({
  "schemaVersion": 1,
  "caseId": "demo",
  "sources": {
    "nativeFds": {
      "producer": "NIST FDS 6.11.1",
      "smv": "native/demo.smv",
      "fds": "native/demo.fds"
    },
    "fireCae": {
      "producer": "FireCAE 0.3.0",
      "smv": "firecae/demo.smv",
      "fds": "firecae/demo.fds",
      "project": "firecae/demo.firecae"
    },
    "pyroSim": {
      "producer": "PyroSim 2023.3",
      "smv": "pyrosim/demo.smv",
      "fds": "pyrosim/demo.fds",
      "project": "pyrosim/demo.psm",
      "exportedAt": "2026-09-03T10:00:00+08:00"
    }
  }
})JSON");
    QString pendingManifestText = readyManifestText;
    pendingManifestText.replace(QStringLiteral("pyrosim/demo.psm"),
                                QStringLiteral("pyrosim/missing.psm"));
    QString sharedManifestText = readyManifestText;
    sharedManifestText.replace(QStringLiteral("firecae/demo.smv"),
                               QStringLiteral("native/demo.smv"));
    check(threeWayFixtures && writeTextFile(readyManifest, readyManifestText) &&
              writeTextFile(pendingManifest, pendingManifestText) &&
              writeTextFile(sharedManifest, sharedManifestText),
          "P30 independent three-source evidence fixtures can be prepared");
    const FdsThreeWayComparisonResult readyThreeWay =
        ThreeWayComparisonEvidence::loadAndCompare(readyManifest);
    if (readyThreeWay.statusCode() != QStringLiteral("PASS")) {
        std::cerr << "P30 ready three-way status="
                  << readyThreeWay.statusCode().toStdString()
                  << " error=" << readyThreeWay.errorMessage.toStdString() << '\n';
        for (const FdsEvidenceValidation& item : readyThreeWay.evidence) {
            std::cerr << ThreeWayComparisonEvidence::sourceDisplayName(item.source.kind)
                             .toStdString()
                      << " pending=" << item.pendingIssues.join('|').toStdString()
                      << " invalid=" << item.invalidIssues.join('|').toStdString()
                      << '\n';
        }
    }
    check(readyThreeWay.evidenceReady() && readyThreeWay.comparisonsPassed() &&
              readyThreeWay.statusCode() == QStringLiteral("PASS") &&
              readyThreeWay.pairs.size() == 3,
          "P30 validates independent Native FDS, FireCAE and PyroSim evidence and all pairs");
    check(readyThreeWay.markdownReport().contains(
              QStringLiteral("A source label alone is not accepted")) &&
              readyThreeWay.jsonReport().contains(
                  QStringLiteral("\"status\": \"PASS\"")),
          "P30 emits auditable Markdown and JSON evidence reports");
    const FdsThreeWayComparisonResult pendingThreeWay =
        ThreeWayComparisonEvidence::loadAndCompare(pendingManifest);
    check(pendingThreeWay.statusCode() == QStringLiteral("PENDING_EVIDENCE") &&
              pendingThreeWay.pairs.isEmpty(),
          "P30 never reports PASS when independent PyroSim project evidence is missing");
    const FdsThreeWayComparisonResult sharedThreeWay =
        ThreeWayComparisonEvidence::loadAndCompare(sharedManifest);
    check(sharedThreeWay.statusCode() == QStringLiteral("INVALID_EVIDENCE") &&
              sharedThreeWay.pairs.isEmpty(),
          "P30 rejects one physical result file masquerading as multiple sources");
    const FdsThreeWayReportArtifacts threeWayArtifacts =
        ThreeWayComparisonEvidence::writeReports(
            pendingThreeWay, threeWayDirectory.filePath(QStringLiteral("reports")));
    check(threeWayArtifacts.success() &&
              QFileInfo::exists(threeWayArtifacts.markdownFile) &&
              QFileInfo::exists(threeWayArtifacts.jsonFile),
          "P30 writes a persistent pending-evidence report without claiming completion");

    auto resultCase = std::make_shared<FcResultCase>(resultScan.caseName);
    resultCase->updateMetadata(resultScan.resultDirectory,
                               resultScan.smvFilePath,
                               resultScan.fdsInputFilePath,
                               resultScan.status,
                               static_cast<int>(resultScan.files.size()),
                               resultScan.startTime,
                               resultScan.endTime,
                               resultScan.scanTime,
                               resultScan.warnings.size());
    const FdsResultFileInfo& firstResultFile = resultScan.files.front();
    auto resultFile = std::make_shared<FcResultFile>(firstResultFile.name,
                                                     firstResultFile.type,
                                                     firstResultFile.filePath,
                                                     firstResultFile.fileSize,
                                                     firstResultFile.exists,
                                                     firstResultFile.lastModified);
    check(resultCase->id() != resultFile->id(),
          "Result case and file use independent FireCAE UUIDs");
    check(resultCase->addChild(resultFile), "Result file enters the result object tree");
    check(document->resultsGroup()->addChild(resultCase),
          "Result case enters the document Results group");
    check(document->findObject(resultFile->id()) == resultFile,
          "Document resolves a result file recursively by FireCAE UUID");
    check(!SmokeviewLauncher::validateExecutable(
               QStringLiteral("Z:/missing/smokeview.exe"))
               .isEmpty(),
          "Smokeview launcher reports a missing executable without starting a process");
    QString scriptError;
    const QString sliceScript = SmokeviewLauncher::createAnimationScript(
        resultScan.smvFilePath, SmokeviewLaunchMode::Slice, &scriptError);
    check(!sliceScript.isEmpty() && QFileInfo::exists(sliceScript) && scriptError.isEmpty(),
          "Smokeview launcher creates a slice animation script");
    check(SmokeviewLauncher::createAnimationScript(
              resultScan.smvFilePath, SmokeviewLaunchMode::SmokeAndFire, &scriptError)
              .isEmpty() && !scriptError.isEmpty(),
          "Smokeview launcher reports unavailable animation data clearly");

    SnapManager snapManager;
    SnapSettings snapSettings;
    snapSettings.fdsGrid = false;
    snapSettings.vertex = false;
    snapSettings.edgeMidpoint = false;
    snapSettings.edge = false;
    snapSettings.face = false;
    snapSettings.objectCenter = false;
    snapSettings.worldGridStep = 0.25;
    snapManager.setSettings(snapSettings);
    const SnapResult gridSnap = snapManager.snapPoint(gp_Pnt(0.37, 0.62, 0.11));
    check(gridSnap.snapped && gridSnap.target == SnapTarget::WorldGrid &&
              gridSnap.point.Distance(gp_Pnt(0.25, 0.5, 0.0)) < 1.0e-9,
          "World-grid snapping returns a stable SI coordinate");
    check(snapManager.constrainTranslation(gp_Vec(1.0, 4.0, 2.0))
                  .IsEqual(gp_Vec(0.0, 4.0, 0.0), 1.0e-9, 1.0e-9),
          "Orthogonal snapping constrains translation to its dominant axis");
    check(std::abs(snapManager.snapAngle(17.0) - 15.0) < 1.0e-9,
          "Angle snapping uses the configured engineering step");

    snapSettings.worldGrid = false;
    snapSettings.vertex = true;
    snapManager.setSettings(snapSettings);
    const TopoDS_Shape snapBox = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
    const SnapResult vertexSnap = snapManager.snapPoint(
        gp_Pnt(0.02, 0.01, 0.03), QVector<TopoDS_Shape>{snapBox}, 0.1);
    check(vertexSnap.snapped && vertexSnap.target == SnapTarget::Vertex &&
              vertexSnap.point.Distance(gp_Pnt(0.0, 0.0, 0.0)) < 1.0e-9,
          "Vertex snapping locates topology from an OpenCascade shape");
    snapManager.setTemporarilyDisabled(true);
    const gp_Pnt unsnappedPoint(0.37, 0.62, 0.11);
    const SnapResult disabledSnap = snapManager.snapPoint(unsnappedPoint);
    check(!disabledSnap.snapped && disabledSnap.point.Distance(unsnappedPoint) < 1.0e-9,
          "Temporary snap suppression preserves the raw coordinate");
    snapManager.setTemporarilyDisabled(false);

    snapSettings.worldGrid = false;
    snapSettings.fdsGrid = false;
    snapSettings.vertex = false;
    snapSettings.edgeMidpoint = false;
    snapSettings.intersection = true;
    snapSettings.edge = false;
    snapSettings.face = false;
    snapSettings.objectCenter = false;
    snapManager.setSettings(snapSettings);
    const TopoDS_Shape crossingBox = BRepPrimAPI_MakeBox(
        gp_Pnt(0.5, -0.5, -0.5), 1.0, 1.0, 1.0).Shape();
    const SnapResult intersectionSnap = snapManager.snapPoint(
        gp_Pnt(0.51, 0.0, 0.01), QVector<TopoDS_Shape>{snapBox, crossingBox}, 0.1);
    check(intersectionSnap.snapped &&
              intersectionSnap.target == SnapTarget::Intersection,
          "Intersection snapping resolves crossing OpenCascade topology");


    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        const QString additionalPath = QString::fromLocal8Bit(argv[argumentIndex]);
        const IfcImportResult additionalResult =
            IfcImportService().importFile(additionalPath);
        check(additionalResult.success(), "Additional IFC model imports successfully");
        if (additionalResult.rootObject) {
            const int shapeCount = ifcShapeCount(additionalResult.rootObject);
            check(shapeCount > 0,
                  "Additional IFC model contains component-level geometry");
            check(shapeCount == additionalResult.geometryObjectCount,
                  "Reported geometry count matches component shapes");
            std::cout << "Validated component mapping: "
                      << additionalPath.toStdString() << " ("
                      << shapeCount << " geometry objects)\n";
        }
    }

    if (failureCount == 0) {
        std::cout << "FireCAE core tests passed.\n";
        return 0;
    }
    return 1;
}
