#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsExamples.h"
#include "fds/FdsImporter.h"
#include "fds/FdsPropertyLibrary.h"
#include "fds/FdsSchema.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <algorithm>
#include <memory>
#include <set>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void collectNamelists(const FcObject::Ptr& object,
                      std::vector<std::shared_ptr<FcFdsNamelist>>& result)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        result.push_back(namelist);
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectNamelists(child, result);
    }
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    std::unique_ptr<FcProject> project = FdsExamples::createSimpleTestProject();
    check(project != nullptr, "Simple test project is created");
    check(project->chid() == QStringLiteral("simple_test"), "CHID is preserved");
    check(project->endTime() == 60.0, "End time is preserved");
    check(project->fdsVersion() == QStringLiteral("6.11.1"),
          "Project selects a versioned FDS schema");
    check(FdsSchemaRegistry::supportedVersions().contains(QStringLiteral("6.11.1")) &&
              FdsSchemaRegistry::keywords(QStringLiteral("6.11.1")).contains(
                  QStringLiteral("GEOM")) &&
              FdsSchemaRegistry::keywords(QStringLiteral("6.11.1")).size() >= 29,
          "Versioned schema exposes all A10 namelist families");
    check(FdsSchemaRegistry::versionFromRevision(
              QStringLiteral("FDS-6.11.1-0-gff928db-release")) ==
              QStringLiteral("6.11.1") &&
              FdsSchemaRegistry::compatibleVersionForRevision(
                  QStringLiteral("Revision : FDS-6.10.1-0-g123-release")) ==
                  QStringLiteral("6.10") &&
              FdsSchemaRegistry::compatibleVersionForRevision(
                  QStringLiteral("FDS-6.12.0")).isEmpty(),
          "Solver revisions map only to an explicitly compatible schema line");
    check(!FdsSchemaRegistry::parameter(QStringLiteral("RAMP"),
                                        QStringLiteral("CYCLING"),
                                        QStringLiteral("6.10")) &&
              FdsSchemaRegistry::parameter(QStringLiteral("RAMP"),
                                           QStringLiteral("CYCLING"),
                                           QStringLiteral("6.11.1")) &&
              FdsSchemaRegistry::parameter(QStringLiteral("SURF"),
                                           QStringLiteral("MOISTURE_CONTENT"),
                                           QStringLiteral("6.11.1")) &&
              FdsSchemaRegistry::parameter(QStringLiteral("PROP"),
                                           QStringLiteral("PROBE_DIAMETER"),
                                           QStringLiteral("6.11.1")) &&
              FdsSchemaRegistry::parameter(QStringLiteral("SLCF"),
                                           QStringLiteral("DRY"),
                                           QStringLiteral("6.11.1")) &&
              FdsSchemaRegistry::parameter(QStringLiteral("COMB"),
                                           QStringLiteral("CVODE_ORDER"),
                                           QStringLiteral("6.11.1")),
          "FDS 6.11 additions are exposed without leaking into the 6.10 schema");
    const FdsNamelistSchema* meshSchema = FdsSchemaRegistry::namelist(
        QStringLiteral("MESH"), project->fdsVersion());
    check(meshSchema && FdsSchemaRegistry::parameter(
                            QStringLiteral("MESH"), QStringLiteral("IJK"),
                            project->fdsVersion()) &&
              FdsSchemaRegistry::parameter(
                  QStringLiteral("MESH"), QStringLiteral("IJK"),
                  project->fdsVersion())->arrayLength == 3,
          "MESH schema describes typed arrays, units and validation metadata");
    const QStringList invalidSchema = FdsSchemaRegistry::validate(
        QStringLiteral("MESH"), QString{},
        {{QStringLiteral("IJK"), FcFdsParameterKind::Raw,
          QStringLiteral("20,-1"), {}},
         {QStringLiteral("XB"), FcFdsParameterKind::Raw,
          QStringLiteral("0,1,0,1,0,1"), {}},
         {QStringLiteral("FUTURE_PARAMETER"), FcFdsParameterKind::Raw,
          QStringLiteral("17"), {}}},
        project->fdsVersion());
    check(!invalidSchema.isEmpty() &&
              invalidSchema.join(QLatin1Char('\n')).contains(QStringLiteral("IJK")) &&
              !invalidSchema.join(QLatin1Char('\n')).contains(
                  QStringLiteral("FUTURE_PARAMETER")),
          "Schema rejects typed range/array errors while accepting unknown future parameters");
    check(project->document()->groups().size() == 15, "Fifteen document groups exist");

    QTemporaryDir libraryDirectory;
    const QString userLibraryPath = libraryDirectory.filePath(
        QStringLiteral("user-library.json"));
    FdsPropertyLibrary propertyLibrary(userLibraryPath);
    check(propertyLibrary.entries().size() >= 9 &&
              propertyLibrary.find(QStringLiteral("builtin.material.concrete")),
          "Property library exposes versioned built-in engineering entries");
    QString libraryError;
    const QString userCopyId = propertyLibrary.duplicateAsUser(
        QStringLiteral("builtin.material.concrete"), &libraryError);
    check(!userCopyId.isEmpty() && QFileInfo::exists(userLibraryPath) &&
              propertyLibrary.find(userCopyId) &&
              !propertyLibrary.find(userCopyId)->builtIn,
          "Built-in library entries duplicate into an independent persistent user library");
    const QString exportedLibraryPath = libraryDirectory.filePath(
        QStringLiteral("exported-library.json"));
    check(propertyLibrary.exportFile(exportedLibraryPath, {userCopyId}, &libraryError),
          "Selected property library entries export to a versioned JSON file");
    const int libraryCountBeforeImport = propertyLibrary.entries().size();
    check(propertyLibrary.importFile(exportedLibraryPath,
                                     FdsLibraryConflictPolicy::Rename,
                                     &libraryError) &&
              propertyLibrary.entries().size() == libraryCountBeforeImport + 1,
          "Property library import resolves conflicts by renaming without overwriting");

    const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(
        project->document()->meshesGroup()->children().front());
    check(mesh != nullptr, "Mesh is a real business object");
    check(mesh && mesh->id() != mesh->fdsId(),
          "FireCAE UUID remains independent from the FDS ID");
    check(mesh && project->document()->findObject(mesh->id()) == mesh,
          "Mesh resolves through FcDocument by UUID");

    const FdsWriteResult rendered = FdsWriter::render(*project);
    check(rendered.success(), "Valid simple test project renders successfully");
    check(rendered.text.contains(QStringLiteral("&HEAD CHID='simple_test'")),
          "Writer emits HEAD");
    check(rendered.text.contains(QStringLiteral("&MESH ID='MESH_1', IJK=36,24,24")),
          "Writer emits mesh settings");
    check(rendered.text.contains(QStringLiteral("&SURF ID='BURNER', HRRPUA=1000")),
          "Writer emits burner surface");
    check(rendered.text.contains(QStringLiteral("&VENT ID='BURNER_VENT'")),
          "Writer emits burner vent");
    check(rendered.text.contains(QStringLiteral("&BNDF QUANTITY='GAUGE HEAT FLUX'")),
          "Writer emits boundary output");
    check(rendered.text.contains(QStringLiteral("&SLCF PBY=1.2, QUANTITY='TEMPERATURE'")),
          "Writer emits vector temperature slice");
    check(rendered.text.endsWith(QStringLiteral("&TAIL /\n")), "Writer emits TAIL");
    auto mixedProject = FdsExamples::createSimpleTestProject();
    auto mixedSpecies = std::make_shared<FcFdsNamelist>(
        QStringLiteral("Water vapor"), FcObjectType::Species,
        QStringLiteral("SPEC"), QStringLiteral("WATER_VAPOR"), 100);
    mixedProject->document()->speciesGroup()->addChild(mixedSpecies);
    const FdsWriteResult mixedRendered = FdsWriter::render(*mixedProject);
    check(mixedRendered.success() &&
              mixedRendered.text.contains(
                  QStringLiteral("&MESH ID='MESH_1', IJK=36,24,24")) &&
              mixedRendered.text.contains(
                  QStringLiteral("&SURF ID='BURNER', HRRPUA=1000")) &&
              mixedRendered.text.contains(
                  QStringLiteral("&SPEC ID='WATER_VAPOR'")),
          "Mixed typed and generic projects preserve both object families");
    const auto ijkSource = std::find_if(
        rendered.sourceMap.cbegin(), rendered.sourceMap.cend(),
        [&mesh](const FdsSourceMapEntry& source) {
            return source.objectId == mesh->id() &&
                   source.parameterKey == QStringLiteral("IJK");
        });
    const QStringList renderedLines = rendered.text.split(QLatin1Char('\n'));
    check(ijkSource != rendered.sourceMap.cend() && ijkSource->line > 0 &&
              ijkSource->line <= renderedLines.size() &&
              ijkSource->columnStart >= 0 &&
              ijkSource->columnEnd > ijkSource->columnStart &&
              renderedLines.at(ijkSource->line - 1)
                  .mid(ijkSource->columnStart,
                       ijkSource->columnEnd - ijkSource->columnStart)
                  .startsWith(QStringLiteral("IJK=")),
          "Writer source map resolves an IJK text range to the mesh UUID");

    FdsImporter unknownRecordImporter;
    const FdsImportResult unknownRecordImport = unknownRecordImporter.importText(
        QStringLiteral("&HEAD CHID='unknown_record_case' /\n"
                       "&MESH ID='MESH_A', IJK=10,10,10, XB=0,1,0,1,0,1 /\n"
                       "&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /\n"
                       "&TIME T_END=5 /\n&TAIL /\n"),
        QStringLiteral("unknown-record.fds"));
    FcObject::Ptr additionalRecords;
    if (unknownRecordImport.success()) {
        for (const FcObject::Ptr& child :
             unknownRecordImport.project->document()->configurationGroup()->children()) {
            if (child && child->name() == QStringLiteral("Additional Records")) {
                additionalRecords = child;
                break;
            }
        }
    }
    const FdsWriteResult unknownRecordRender =
        unknownRecordImport.success()
            ? FdsWriter::render(*unknownRecordImport.project)
            : FdsWriteResult{};
    check(unknownRecordImport.success() && additionalRecords &&
              additionalRecords->children().size() == 1 &&
              unknownRecordRender.success() &&
              unknownRecordRender.text.contains(
                  QStringLiteral("&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /")),
          "Unknown imported namelists are isolated under Additional Records and re-exported");
    QTemporaryDir unknownRecordDirectory;
    const QString unknownProjectPath = unknownRecordDirectory.filePath(
        QStringLiteral("unknown-record.firecae"));
    QString unknownSaveError;
    const bool unknownSaved = unknownRecordImport.success() &&
        FcProjectSerializer::save(*unknownRecordImport.project,
                                  unknownProjectPath, &unknownSaveError);
    const FcProjectLoadResult reopenedUnknown =
        unknownSaved ? FcProjectSerializer::load(unknownProjectPath)
                     : FcProjectLoadResult{};
    check(unknownSaved && reopenedUnknown.success() &&
              FdsWriter::render(*reopenedUnknown.project).text.contains(
                  QStringLiteral("&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /")),
          "Unknown namelists survive project save/reopen without silent data loss");

    QTemporaryDir temporaryDirectory;
    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("simple_test.fds"));
    QString error;
    check(FdsWriter::writeFile(*project, outputPath, &error),
          "Rendered project writes atomically to disk");
    QFile outputFile(outputPath);
    check(outputFile.open(QIODevice::ReadOnly | QIODevice::Text),
          "Generated FDS file can be reopened");
    check(QString::fromUtf8(outputFile.readAll()) == rendered.text,
          "File contents match deterministic render output");

    auto badProject = FdsExamples::createSimpleTestProject();
    const auto badVent = std::dynamic_pointer_cast<FcFdsVent>(
        badProject->document()->ventsGroup()->children().front());
    badVent->setSurfaceId(QStringLiteral("MISSING_SURFACE"));
    const FdsWriteResult invalid = FdsWriter::render(*badProject);
    check(!invalid.success() && invalid.errors.join(QLatin1Char('\n')).contains(
                                  QStringLiteral("unknown surface")),
          "Unknown surface references are rejected before solver launch");

    FdsImporter detachedFireImporter;
    const FdsImportResult detachedFire = detachedFireImporter.importText(
        QStringLiteral("&HEAD CHID='detached_fire' /\n"
                       "&MESH IJK=10,10,10, XB=0,1,0,1,0,1 /\n"
                       "&SURF ID='FIRE', HRRPUA=500. /\n"
                       "&TIME T_END=10. /\n&TAIL /\n"),
        QStringLiteral("detached_fire.fds"));
    check(detachedFire.success() &&
              !FdsWriter::render(*detachedFire.project).success() &&
              FdsWriter::render(*detachedFire.project).errors.join(QLatin1Char('\n'))
                  .contains(QStringLiteral("fire surface is not attached")),
          "Detached fire surfaces are rejected before solver launch");

    const QString importedInput = QStringLiteral(
        "&HEAD CHID='object_case', TITLE='Object Case' /\n"
        "&MESH ID='Mesh A', IJK=10,10,10, XB=0,1,0,1,0,1 /\n"
        "&SPEC ID='WATER VAPOR' /\n"
        "&MATL ID='GYPSUM', DENSITY=800.\n"
        " CONDUCTIVITY=0.2, SPECIFIC_HEAT=1.0 /\n"
        "&SURF ID='WALL', MATL_ID='GYPSUM', THICKNESS=0.012 /\n"
        "&PART ID='DROPLETS', SPEC_ID='WATER VAPOR' /\n"
        "&PROP ID='SPRINKLER', PART_ID='DROPLETS' /\n"
        "&DEVC ID='LINK', PROP_ID='SPRINKLER', XYZ=0.5,0.5,0.8 /\n"
        "&CTRL ID='ENABLE', FUNCTION_TYPE='ALL', INPUT_ID='LINK' /\n"
        "&HVAC ID='DUCT 1', TYPE_ID='DUCT', NODE_ID='N1','N2' /\n"
        "&INIT ID='HOT ZONE', TEMPERATURE=100. /\n"
        "&SLCF QUANTITY='TEMPERATURE', PBZ=0.5 /\n"
        "&TIME T_END=12., DT=0.02 /\n&TAIL /\n");
    FdsImporter importer;
    FdsImportResult imported = importer.importText(importedInput,
                                                    QStringLiteral("object_case.fds"));
    check(imported.success(), "Structured FDS input imports successfully");
    check(imported.objectCount == 12,
          "Every non-project FDS namelist becomes a business object");
    check(imported.project && imported.project->chid() == QStringLiteral("object_case"),
          "HEAD fields populate the project object");
    check(imported.project && imported.project->endTime() == 12.0,
          "TIME fields populate the project object");
    check(imported.project && imported.project->document()->speciesGroup()->children().size() == 1,
          "SPEC objects enter the Species group");
    check(imported.project && imported.project->document()->particlesGroup()->children().size() == 2,
          "PART and PROP objects enter the Particles group");
    check(imported.project && imported.project->document()->controlsGroup()->children().size() == 1,
          "CTRL objects enter the Controls group");
    if (imported.project) {
        const auto surface = std::dynamic_pointer_cast<FcFdsNamelist>(
            imported.project->document()->surfacesGroup()->children().front());
        check(surface && imported.project->document()->findObject(surface->id()) == surface,
              "Imported namelist resolves through FcDocument by UUID");
        check(surface && !surface->parameters().empty() &&
                  surface->parameters().front().kind == FcFdsParameterKind::ObjectReferences,
              "Imported FDS references are converted to UUID references");
        const FdsWriteResult roundTrip = FdsWriter::render(*imported.project);
        if (!roundTrip.success()) {
            std::cerr << "Schema round-trip errors:\n"
                      << roundTrip.errors.join(QLatin1Char('\n')).toStdString() << '\n';
        }
        check(roundTrip.success(), "Imported object tree writes back to FDS");
        check(roundTrip.text.contains(QStringLiteral("MATL_ID='GYPSUM'")),
              "UUID reference writes back as the target FDS ID");
        check(roundTrip.text.contains(QStringLiteral("&TIME T_END=12, DT=0.02")),
              "TIME project data and imported parameters are retained");
    }

    std::unique_ptr<FcProject> activate = FdsExamples::createActivateVentsProject();
    check(activate != nullptr, "activate_vents object factory creates a project");
    std::vector<std::shared_ptr<FcFdsNamelist>> activateObjects;
    std::set<QString> activateUuids;
    if (activate) {
        for (const auto& group : activate->document()->groups()) {
            collectNamelists(group, activateObjects);
        }
        for (const auto& object : activateObjects) {
            activateUuids.insert(object->id());
        }
    }
    check(activateObjects.size() == 44,
          "activate_vents contains all 44 editable FDS business objects");
    check(activateUuids.size() == activateObjects.size(),
          "activate_vents business object UUIDs are unique");
    bool allReferencesUseExistingUuids = true;
    for (const auto& object : activateObjects) {
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
            for (const QString& uuid : parameter.targetObjectIds) {
                allReferencesUseExistingUuids =
                    allReferencesUseExistingUuids && activateUuids.count(uuid) == 1;
            }
        }
    }
    check(allReferencesUseExistingUuids,
          "activate_vents references use existing object UUIDs");
    const FdsWriteResult activateRendered = FdsWriter::render(*activate);
    if (!activateRendered.success()) {
        std::cerr << "Schema activate errors:\n"
                  << activateRendered.errors.join(QLatin1Char('\n')).toStdString() << '\n';
    }
    check(activateRendered.success(), "activate_vents object tree renders successfully");
    check(activateRendered.text.contains(
              QStringLiteral("&CTRL ID='controller 4', FUNCTION_TYPE='ALL', "
                             "INPUT_ID='controller 1','controller 3'")),
          "Multi-object CTRL references render as FDS IDs");
    check(activateRendered.text.count(QStringLiteral("&RAMP ID='ramp 1'")) == 7,
          "Repeated RAMP records with one FDS ID are preserved");

    std::unique_ptr<FcProject> wrongReferenceType =
        FdsExamples::createActivateVentsProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> wrongTypeObjects;
    for (const auto& group : wrongReferenceType->document()->groups()) {
        collectNamelists(group, wrongTypeObjects);
    }
    const auto wrongTypeMesh = std::find_if(
        wrongTypeObjects.cbegin(), wrongTypeObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("MESH");
        });
    const auto wrongTypeVent = std::find_if(
        wrongTypeObjects.cbegin(), wrongTypeObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("VENT") &&
                   object->parameterValue(QStringLiteral("SURF_ID")).isEmpty();
        });
    if (wrongTypeMesh != wrongTypeObjects.cend() &&
        wrongTypeVent != wrongTypeObjects.cend()) {
        (*wrongTypeVent)->setReferenceTargets(
            QStringLiteral("SURF_ID"), {(*wrongTypeMesh)->id()});
        const FdsWriteResult wrongTypeResult = FdsWriter::render(*wrongReferenceType);
        const QString wrongTypeErrors = wrongTypeResult.errors.join(QLatin1Char('\n'));
        check(!wrongTypeResult.success() &&
                  wrongTypeErrors.contains((*wrongTypeVent)->id()) &&
                  wrongTypeErrors.contains(QStringLiteral("SURF_ID")) &&
                  wrongTypeErrors.contains(QStringLiteral("expected SURF")),
              "Wrong UUID reference types are rejected with object UUID and parameter name");
    } else {
        check(false, "Wrong-reference-type validation fixture is complete");
    }

    std::unique_ptr<FcProject> cyclicControls =
        FdsExamples::createActivateVentsProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> cyclicObjects;
    for (const auto& group : cyclicControls->document()->groups()) {
        collectNamelists(group, cyclicObjects);
    }
    const auto controller1 = std::find_if(
        cyclicObjects.cbegin(), cyclicObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("CTRL") &&
                   object->fdsId() == QStringLiteral("controller 1");
        });
    const auto controller4 = std::find_if(
        cyclicObjects.cbegin(), cyclicObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("CTRL") &&
                   object->fdsId() == QStringLiteral("controller 4");
        });
    if (controller1 != cyclicObjects.cend() && controller4 != cyclicObjects.cend()) {
        (*controller1)->setReferenceTargets(
            QStringLiteral("INPUT_ID"), {(*controller4)->id()});
        const FdsWriteResult cycleResult = FdsWriter::render(*cyclicControls);
        const QString cycleErrors = cycleResult.errors.join(QLatin1Char('\n'));
        check(!cycleResult.success() &&
                  cycleErrors.contains(QStringLiteral("CTRL INPUT_ID reference cycle")) &&
                  cycleErrors.contains((*controller1)->id()),
              "CTRL UUID reference cycles are rejected before solver launch");
    } else {
        check(false, "CTRL-cycle validation fixture is complete");
    }

    QTemporaryDir projectDirectory;
    const QString projectPath =
        projectDirectory.filePath(QStringLiteral("activate_vents.firecae"));
    QString projectSaveError;
    FcProjectRuntimeSettings activateRuntimeSettings;
    activateRuntimeSettings.resultDirectory =
        QStringLiteral("D:/FireCAE/tests/data/gui-generated/activate_vents");
    activateRuntimeSettings.solverExecutable =
        QStringLiteral("C:/Program Files/firemodels/FDS6/bin/fds.exe");
    activateRuntimeSettings.parallelProcessCount = 7;
    check(FcProjectSerializer::save(*activate, projectPath, activateRuntimeSettings,
                                    &projectSaveError),
          "activate_vents project saves as a FireCAE project");
    const FcProjectLoadResult reopened = FcProjectSerializer::load(projectPath);
    check(reopened.success(), "Saved activate_vents project reopens");
    check(reopened.warnings.isEmpty() && reopened.project &&
              reopened.project->fdsVersion() == QStringLiteral("6.11.1"),
          "Current projects reopen on Schema 6.11.1 without migration warnings");
    check(QDir::fromNativeSeparators(reopened.runtimeSettings.resultDirectory) ==
              activateRuntimeSettings.resultDirectory &&
              QDir::fromNativeSeparators(reopened.runtimeSettings.solverExecutable) ==
                  activateRuntimeSettings.solverExecutable &&
              reopened.runtimeSettings.parallelProcessCount == 7,
          "Project reopen preserves result directory and solver configuration");
    if (reopened.project) {
        std::vector<std::shared_ptr<FcFdsNamelist>> reopenedObjects;
        std::set<QString> reopenedUuids;
        for (const auto& group : reopened.project->document()->groups()) {
            collectNamelists(group, reopenedObjects);
        }
        for (const auto& object : reopenedObjects) reopenedUuids.insert(object->id());
        check(reopenedUuids == activateUuids,
              "Project reopen preserves every business object UUID");
        check(FdsWriter::render(*reopened.project).text == activateRendered.text,
              "Project reopen preserves parameters, references, order, and FDS output");
    }

    QFile currentProjectFile(projectPath);
    const QString legacyProjectPath =
        projectDirectory.filePath(QStringLiteral("activate_vents_legacy.firecae"));
    bool legacyFixtureWritten = false;
    if (currentProjectFile.open(QIODevice::ReadOnly)) {
        QJsonDocument legacyDocument =
            QJsonDocument::fromJson(currentProjectFile.readAll());
        QJsonObject legacyRoot = legacyDocument.object();
        QJsonObject legacyProject = legacyRoot.value(QStringLiteral("project")).toObject();
        legacyProject.remove(QStringLiteral("fdsVersion"));
        legacyRoot.insert(QStringLiteral("project"), legacyProject);
        QFile legacyProjectFile(legacyProjectPath);
        legacyFixtureWritten = legacyProjectFile.open(QIODevice::WriteOnly) &&
            legacyProjectFile.write(QJsonDocument(legacyRoot).toJson()) > 0;
    }
    const FcProjectLoadResult reopenedLegacy = legacyFixtureWritten
        ? FcProjectSerializer::load(legacyProjectPath) : FcProjectLoadResult{};
    check(reopenedLegacy.success() && reopenedLegacy.project &&
              reopenedLegacy.project->fdsVersion() == QStringLiteral("6.10") &&
              !reopenedLegacy.warnings.isEmpty(),
          "Legacy projects without a Schema version retain 6.10 compatibility and warn");

    std::unique_ptr<FcProject> bucket = FdsExamples::createBucketTest2Project();
    check(bucket != nullptr, "bucket_test_2 object factory creates a project");
    std::vector<std::shared_ptr<FcFdsNamelist>> bucketObjects;
    std::set<QString> bucketUuids;
    if (bucket) {
        for (const auto& group : bucket->document()->groups()) {
            collectNamelists(group, bucketObjects);
        }
        for (const auto& object : bucketObjects) bucketUuids.insert(object->id());
    }
    check(bucketObjects.size() == 13,
          "bucket_test_2 contains all 13 editable FDS business objects");
    check(bucketUuids.size() == bucketObjects.size(),
          "bucket_test_2 business object UUIDs are unique");
    bool bucketReferencesAreValid = true;
    for (const auto& object : bucketObjects) {
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
            for (const QString& uuid : parameter.targetObjectIds) {
                bucketReferencesAreValid =
                    bucketReferencesAreValid && bucketUuids.count(uuid) == 1;
            }
        }
    }
    check(bucketReferencesAreValid,
          "bucket_test_2 references use existing object UUIDs");
    const FdsWriteResult bucketRendered = FdsWriter::render(*bucket);
    check(bucketRendered.success(),
          "bucket_test_2 object tree renders successfully");
    check(bucketRendered.text.count(QStringLiteral("&TABL ID='TABLE1'")) == 2,
          "Both repeated sprinkler spray TABL records are preserved");
    check(bucketRendered.text.contains(
              QStringLiteral("SPRAY_PATTERN_TABLE='TABLE1'")),
          "PROP spray table UUID reference renders as its FDS ID");

    const QString bucketReferencePath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(
            QStringLiteral("fds-tutorials/bucket_test_2/bucket_test_2.fds"));
    FdsImportResult bucketReference = importer.importFile(bucketReferencePath);
    check(bucketReference.success(),
          "Official bucket_test_2 reference imports for semantic comparison");
    if (bucketReference.project) {
        const FdsWriteResult canonicalReference =
            FdsWriter::render(*bucketReference.project);
        check(canonicalReference.success(),
              "Official bucket_test_2 canonical form renders");
        check(canonicalReference.text == bucketRendered.text,
              "FireCAE bucket_test_2 output is canonically equal to the reference");
    }

    QTemporaryDir bucketProjectDirectory;
    const QString bucketProjectPath = bucketProjectDirectory.filePath(
        QStringLiteral("bucket_test_2.firecae"));
    QString bucketSaveError;
    check(FcProjectSerializer::save(*bucket, bucketProjectPath, &bucketSaveError),
          "bucket_test_2 project saves as a FireCAE project");
    const FcProjectLoadResult reopenedBucket =
        FcProjectSerializer::load(bucketProjectPath);
    check(reopenedBucket.success(), "Saved bucket_test_2 project reopens");
    if (reopenedBucket.project) {
        std::vector<std::shared_ptr<FcFdsNamelist>> reopenedBucketObjects;
        std::set<QString> reopenedBucketUuids;
        for (const auto& group : reopenedBucket.project->document()->groups()) {
            collectNamelists(group, reopenedBucketObjects);
        }
        for (const auto& object : reopenedBucketObjects) {
            reopenedBucketUuids.insert(object->id());
        }
        check(reopenedBucketUuids == bucketUuids,
              "bucket_test_2 reopen preserves every object UUID");
        check(FdsWriter::render(*reopenedBucket.project).text == bucketRendered.text,
              "bucket_test_2 reopen preserves its FDS output");
    }

    std::unique_ptr<FcProject> couch = FdsExamples::createCouchProject();
    check(couch != nullptr, "couch object factory creates a project");
    std::vector<std::shared_ptr<FcFdsNamelist>> couchObjects;
    std::set<QString> couchUuids;
    if (couch) {
        for (const auto& group : couch->document()->groups()) {
            collectNamelists(group, couchObjects);
        }
        for (const auto& object : couchObjects) couchUuids.insert(object->id());
    }
    check(couchObjects.size() == 26,
          "couch contains all 26 editable FDS business objects");
    check(couchUuids.size() == couchObjects.size(),
          "couch business object UUIDs are unique");
    bool couchReferencesAreValid = true;
    int couchReferenceCount = 0;
    int upholsteryMaterialReferenceCount = 0;
    for (const auto& object : couchObjects) {
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
            couchReferenceCount += parameter.targetObjectIds.size();
            if (object->fdsId() == QStringLiteral("UPHOLSTERY") &&
                parameter.key == QStringLiteral("MATL_ID(1:2,1)")) {
                upholsteryMaterialReferenceCount = parameter.targetObjectIds.size();
            }
            for (const QString& uuid : parameter.targetObjectIds) {
                couchReferencesAreValid =
                    couchReferencesAreValid && couchUuids.count(uuid) == 1;
            }
        }
    }
    check(couchReferencesAreValid && couchReferenceCount >= 13,
          "couch cross-object links use existing UUIDs");
    check(upholsteryMaterialReferenceCount == 2,
          "Upholstery surface keeps both material layer UUID references");
    const FdsWriteResult couchRendered = FdsWriter::render(*couch);
    if (!couchRendered.success()) {
        for (const QString& error : couchRendered.errors) {
            std::cerr << "couch render error: " << error.toStdString() << '\n';
        }
    }
    check(couchRendered.success(), "couch object tree renders successfully");
    check(couchRendered.text.contains(
              QStringLiteral("MATL_ID(1:2,1)='FABRIC','FOAM'")),
          "Layered material UUID references render as ordered FDS IDs");
    check(couchRendered.text.contains(
              QStringLiteral("&REAC FUEL='POLYURETHANE'")),
          "Reaction fuel UUID reference renders as the species FDS ID");

    const QString couchReferencePath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(
            QStringLiteral("fds-tutorials/couch/couch.fds"));
    FdsImportResult couchReference = importer.importFile(couchReferencePath);
    check(couchReference.success(),
          "Official couch reference imports for semantic comparison");
    if (couchReference.project) {
        const FdsWriteResult canonicalReference =
            FdsWriter::render(*couchReference.project);
        check(canonicalReference.success(), "Official couch canonical form renders");
        check(canonicalReference.text == couchRendered.text,
              "FireCAE couch output is canonically equal to the reference");
    }

    QTemporaryDir couchProjectDirectory;
    const QString couchProjectPath = couchProjectDirectory.filePath(
        QStringLiteral("couch.firecae"));
    QString couchSaveError;
    check(FcProjectSerializer::save(*couch, couchProjectPath, &couchSaveError),
          "couch project saves as a FireCAE project");
    const FcProjectLoadResult reopenedCouch =
        FcProjectSerializer::load(couchProjectPath);
    check(reopenedCouch.success(), "Saved couch project reopens");
    if (reopenedCouch.project) {
        std::vector<std::shared_ptr<FcFdsNamelist>> reopenedCouchObjects;
        std::set<QString> reopenedCouchUuids;
        for (const auto& group : reopenedCouch.project->document()->groups()) {
            collectNamelists(group, reopenedCouchObjects);
        }
        for (const auto& object : reopenedCouchObjects) {
            reopenedCouchUuids.insert(object->id());
        }
        check(reopenedCouchUuids == couchUuids,
              "couch reopen preserves every object UUID");
        check(FdsWriter::render(*reopenedCouch.project).text == couchRendered.text,
              "couch reopen preserves its FDS output");
    }

    std::unique_ptr<FcProject> couchSmoke =
        FdsExamples::createCouchSmoke12sProject();
    check(couchSmoke != nullptr,
          "couch_smoke_12s object factory creates a project");
    std::vector<std::shared_ptr<FcFdsNamelist>> couchSmokeObjects;
    std::set<QString> couchSmokeUuids;
    if (couchSmoke) {
        for (const auto& group : couchSmoke->document()->groups()) {
            collectNamelists(group, couchSmokeObjects);
        }
        for (const auto& object : couchSmokeObjects) {
            couchSmokeUuids.insert(object->id());
        }
    }
    check(couchSmokeObjects.size() == 26,
          "couch_smoke_12s contains all 26 editable business objects");
    const FdsWriteResult couchSmokeRendered = FdsWriter::render(*couchSmoke);
    if (!couchSmokeRendered.success()) {
        for (const QString& error : couchSmokeRendered.errors) {
            std::cerr << "couch_smoke render error: " << error.toStdString() << '\n';
        }
    }
    check(couchSmokeRendered.success(),
          "couch_smoke_12s object tree renders successfully");
    check(couchSmokeRendered.text.contains(QStringLiteral("&DUMP NFRAMES=60")) &&
              couchSmokeRendered.text.contains(QStringLiteral("&TIME T_END=12")),
          "couch_smoke_12s keeps its shortened time and output cadence");
    const QString couchSmokeReferencePath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(QStringLiteral(
            "fds-tutorials/couch_smoke_12s/couch_smoke_12s.fds"));
    FdsImportResult couchSmokeReference = importer.importFile(couchSmokeReferencePath);
    check(couchSmokeReference.success(),
          "Official couch_smoke_12s reference imports for semantic comparison");
    if (couchSmokeReference.project) {
        const FdsWriteResult canonicalReference =
            FdsWriter::render(*couchSmokeReference.project);
        check(canonicalReference.success(),
              "Official couch_smoke_12s canonical form renders");
        check(canonicalReference.text == couchSmokeRendered.text,
              "FireCAE couch_smoke_12s output is canonically equal to the reference");
    }
    QTemporaryDir couchSmokeProjectDirectory;
    const QString couchSmokeProjectPath = couchSmokeProjectDirectory.filePath(
        QStringLiteral("couch_smoke_12s.firecae"));
    QString couchSmokeSaveError;
    check(FcProjectSerializer::save(*couchSmoke, couchSmokeProjectPath,
                                    &couchSmokeSaveError),
          "couch_smoke_12s project saves as a FireCAE project");
    const FcProjectLoadResult reopenedCouchSmoke =
        FcProjectSerializer::load(couchSmokeProjectPath);
    check(reopenedCouchSmoke.success(), "Saved couch_smoke_12s project reopens");
    if (reopenedCouchSmoke.project) {
        std::vector<std::shared_ptr<FcFdsNamelist>> reopenedObjects;
        std::set<QString> reopenedUuids;
        for (const auto& group : reopenedCouchSmoke.project->document()->groups()) {
            collectNamelists(group, reopenedObjects);
        }
        for (const auto& object : reopenedObjects) reopenedUuids.insert(object->id());
        check(reopenedUuids == couchSmokeUuids,
              "couch_smoke_12s reopen preserves every object UUID");
        check(FdsWriter::render(*reopenedCouchSmoke.project).text ==
                  couchSmokeRendered.text,
              "couch_smoke_12s reopen preserves its FDS output");
    }

    std::unique_ptr<FcProject> hvac = FdsExamples::createHvacAircoilProject();
    check(hvac != nullptr, "HVAC_aircoil object factory creates a project");
    std::vector<std::shared_ptr<FcFdsNamelist>> hvacObjects;
    std::set<QString> hvacUuids;
    if (hvac) {
        for (const auto& group : hvac->document()->groups()) {
            collectNamelists(group, hvacObjects);
        }
        for (const auto& object : hvacObjects) hvacUuids.insert(object->id());
    }
    check(hvacObjects.size() == 20,
          "HVAC_aircoil contains all 20 editable business objects");
    bool hvacReferencesAreValid = true;
    int hvacReferenceCount = 0;
    for (const auto& object : hvacObjects) {
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
            hvacReferenceCount += static_cast<int>(parameter.targetObjectIds.size());
            for (const QString& uuid : parameter.targetObjectIds) {
                hvacReferencesAreValid =
                    hvacReferencesAreValid && hvacUuids.count(uuid) == 1;
            }
        }
    }
    check(hvacReferencesAreValid && hvacReferenceCount == 9,
          "HVAC nodes, duct, aircoil, vents, and devices use UUID references");
    const FdsWriteResult hvacRendered = FdsWriter::render(*hvac);
    check(hvacRendered.success(), "HVAC_aircoil object tree renders successfully");
    check(hvacRendered.text.contains(
              QStringLiteral("NODE_ID='INLET','OUTLET'")) &&
              hvacRendered.text.contains(QStringLiteral("AIRCOIL_ID='AIRCOIL'")),
          "HVAC UUID references render as ordered FDS IDs");
    std::unique_ptr<FcProject> invalidHvac =
        FdsExamples::createHvacAircoilProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> invalidHvacObjects;
    for (const auto& group : invalidHvac->document()->groups()) {
        collectNamelists(group, invalidHvacObjects);
    }
    const auto invalidDuct = std::find_if(
        invalidHvacObjects.cbegin(), invalidHvacObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("HVAC") &&
                   object->parameterValue(QStringLiteral("TYPE_ID")).compare(
                       QStringLiteral("DUCT"), Qt::CaseInsensitive) == 0;
        });
    if (invalidDuct != invalidHvacObjects.cend()) {
        const FcFdsParameter* nodes = nullptr;
        for (const FcFdsParameter& parameter : (*invalidDuct)->parameters()) {
            if (parameter.key == QStringLiteral("NODE_ID")) nodes = &parameter;
        }
        if (nodes && !nodes->targetObjectIds.isEmpty()) {
            (*invalidDuct)->setReferenceTargets(
                QStringLiteral("NODE_ID"), {nodes->targetObjectIds.constFirst()});
        }
        const FdsWriteResult invalidHvacResult = FdsWriter::render(*invalidHvac);
        const QString invalidHvacErrors =
            invalidHvacResult.errors.join(QLatin1Char('\n'));
        check(!invalidHvacResult.success() &&
                  invalidHvacErrors.contains((*invalidDuct)->id()) &&
                  invalidHvacErrors.contains(QStringLiteral(
                      "NODE_ID must reference exactly two HVAC nodes")),
              "HVAC ducts with incomplete UUID topology are rejected");
    } else {
        check(false, "HVAC topology validation fixture is complete");
    }
    std::unique_ptr<FcProject> wrongHvacEndpoint =
        FdsExamples::createHvacAircoilProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> wrongEndpointObjects;
    for (const auto& group : wrongHvacEndpoint->document()->groups()) {
        collectNamelists(group, wrongEndpointObjects);
    }
    const auto wrongEndpointDuct = std::find_if(
        wrongEndpointObjects.cbegin(), wrongEndpointObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("HVAC") &&
                   object->parameterValue(QStringLiteral("TYPE_ID")) ==
                       QStringLiteral("DUCT");
        });
    const auto endpointAircoil = std::find_if(
        wrongEndpointObjects.cbegin(), wrongEndpointObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("HVAC") &&
                   object->parameterValue(QStringLiteral("TYPE_ID")) ==
                       QStringLiteral("AIRCOIL");
        });
    if (wrongEndpointDuct != wrongEndpointObjects.cend() &&
        endpointAircoil != wrongEndpointObjects.cend()) {
        QStringList nodeIds;
        for (const FcFdsParameter& parameter : (*wrongEndpointDuct)->parameters()) {
            if (parameter.key == QStringLiteral("NODE_ID")) {
                nodeIds = parameter.targetObjectIds;
            }
        }
        if (!nodeIds.isEmpty()) {
            (*wrongEndpointDuct)->setReferenceTargets(
                QStringLiteral("NODE_ID"),
                {nodeIds.constFirst(), (*endpointAircoil)->id()});
        }
        const FdsWriteResult wrongEndpointResult =
            FdsWriter::render(*wrongHvacEndpoint);
        const QString wrongEndpointErrors =
            wrongEndpointResult.errors.join(QLatin1Char('\n'));
        check(!wrongEndpointResult.success() &&
                  wrongEndpointErrors.contains((*wrongEndpointDuct)->id()) &&
                  wrongEndpointErrors.contains(
                      QStringLiteral("must target an HVAC NODE")),
              "HVAC duct endpoints reject HVAC components of the wrong subtype");
    } else {
        check(false, "HVAC wrong-endpoint validation fixture is complete");
    }

    std::unique_ptr<FcProject> cyclicHvac =
        FdsExamples::createHvacAircoilProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> cyclicHvacObjects;
    for (const auto& group : cyclicHvac->document()->groups()) {
        collectNamelists(group, cyclicHvacObjects);
    }
    std::vector<std::shared_ptr<FcFdsNamelist>> cyclicNodes;
    std::shared_ptr<FcFdsNamelist> cyclicBaseDuct;
    for (const auto& object : cyclicHvacObjects) {
        if (object->keyword() != QStringLiteral("HVAC")) continue;
        if (object->parameterValue(QStringLiteral("TYPE_ID")) == QStringLiteral("NODE")) {
            cyclicNodes.push_back(object);
        } else if (object->parameterValue(QStringLiteral("TYPE_ID")) ==
                   QStringLiteral("DUCT")) {
            cyclicBaseDuct = object;
        }
    }
    if (cyclicNodes.size() == 2 && cyclicBaseDuct) {
        auto thirdNode = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Cycle node"), FcObjectType::HVAC,
            QStringLiteral("HVAC"), QStringLiteral("CYCLE_NODE"), 20);
        thirdNode->addStringParameter(QStringLiteral("TYPE_ID"),
                                      QStringLiteral("NODE"));
        auto secondDuct = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Cycle duct 2"), FcObjectType::HVAC,
            QStringLiteral("HVAC"), QStringLiteral("CYCLE_DUCT_2"), 21);
        secondDuct->addStringParameter(QStringLiteral("TYPE_ID"),
                                       QStringLiteral("DUCT"));
        secondDuct->addReferenceParameter(
            QStringLiteral("NODE_ID"), {cyclicNodes[0]->id(), thirdNode->id()});
        secondDuct->addRawParameter(QStringLiteral("LENGTH"), QStringLiteral("1"));
        secondDuct->addRawParameter(QStringLiteral("AREA"), QStringLiteral("0.1"));
        auto thirdDuct = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Cycle duct 3"), FcObjectType::HVAC,
            QStringLiteral("HVAC"), QStringLiteral("CYCLE_DUCT_3"), 22);
        thirdDuct->addStringParameter(QStringLiteral("TYPE_ID"),
                                      QStringLiteral("DUCT"));
        thirdDuct->addReferenceParameter(
            QStringLiteral("NODE_ID"), {thirdNode->id(), cyclicNodes[1]->id()});
        thirdDuct->addRawParameter(QStringLiteral("LENGTH"), QStringLiteral("1"));
        thirdDuct->addRawParameter(QStringLiteral("AREA"), QStringLiteral("0.1"));
        cyclicNodes[0]->setReferenceTargets(
            QStringLiteral("DUCT_ID"), {cyclicBaseDuct->id(), secondDuct->id()});
        cyclicNodes[1]->setReferenceTargets(
            QStringLiteral("DUCT_ID"), {cyclicBaseDuct->id(), thirdDuct->id()});
        thirdNode->addReferenceParameter(
            QStringLiteral("DUCT_ID"), {secondDuct->id(), thirdDuct->id()});
        cyclicHvac->document()->hvacGroup()->addChild(thirdNode);
        cyclicHvac->document()->hvacGroup()->addChild(secondDuct);
        cyclicHvac->document()->hvacGroup()->addChild(thirdDuct);
        const FdsWriteResult cycleResult = FdsWriter::render(*cyclicHvac);
        check(!cycleResult.success() &&
                  cycleResult.errors.join(QLatin1Char('\n')).contains(
                      QStringLiteral("HVAC topology contains a duct cycle")),
              "HVAC duct cycles are rejected before solver launch");
    } else {
        check(false, "HVAC-cycle validation fixture is complete");
    }

    std::unique_ptr<FcProject> invalidCouchLayers =
        FdsExamples::createCouchProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> invalidLayerObjects;
    for (const auto& group : invalidCouchLayers->document()->groups()) {
        collectNamelists(group, invalidLayerObjects);
    }
    const auto upholstery = std::find_if(
        invalidLayerObjects.cbegin(), invalidLayerObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("SURF") &&
                   object->fdsId() == QStringLiteral("UPHOLSTERY");
        });
    if (upholstery != invalidLayerObjects.cend()) {
        std::vector<FcFdsParameter> parameters = (*upholstery)->parameters();
        for (FcFdsParameter& parameter : parameters) {
            if (parameter.key.startsWith(QStringLiteral("THICKNESS"))) {
                parameter.value = QStringLiteral("0.0005");
            }
        }
        (*upholstery)->setParameters(parameters);
        const FdsWriteResult invalidLayerResult =
            FdsWriter::render(*invalidCouchLayers);
        const QString invalidLayerErrors =
            invalidLayerResult.errors.join(QLatin1Char('\n'));
        check(!invalidLayerResult.success() &&
                  invalidLayerErrors.contains((*upholstery)->id()) &&
                  invalidLayerErrors.contains(
                      QStringLiteral("layer count must equal THICKNESS count")),
              "SURF material layer and thickness counts are validated");
    } else {
        check(false, "Couch layer validation fixture is complete");
    }
    const QString hvacReferencePath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(
            QStringLiteral("fds-tutorials/HVAC_aircoil/HVAC_aircoil.fds"));
    FdsImportResult hvacReference = importer.importFile(hvacReferencePath);
    check(hvacReference.success(),
          "Official HVAC_aircoil reference imports for semantic comparison");
    if (hvacReference.project) {
        const FdsWriteResult canonicalReference =
            FdsWriter::render(*hvacReference.project);
        check(canonicalReference.success(),
              "Official HVAC_aircoil canonical form renders");
        check(canonicalReference.text == hvacRendered.text,
              "FireCAE HVAC_aircoil output is canonically equal to the reference");
    }
    QTemporaryDir hvacProjectDirectory;
    const QString hvacProjectPath = hvacProjectDirectory.filePath(
        QStringLiteral("HVAC_aircoil.firecae"));
    QString hvacSaveError;
    check(FcProjectSerializer::save(*hvac, hvacProjectPath, &hvacSaveError),
          "HVAC_aircoil project saves as a FireCAE project");
    const FcProjectLoadResult reopenedHvac = FcProjectSerializer::load(hvacProjectPath);
    check(reopenedHvac.success(), "Saved HVAC_aircoil project reopens");
    if (reopenedHvac.project) {
        std::vector<std::shared_ptr<FcFdsNamelist>> reopenedObjects;
        std::set<QString> reopenedUuids;
        for (const auto& group : reopenedHvac.project->document()->groups()) {
            collectNamelists(group, reopenedObjects);
        }
        for (const auto& object : reopenedObjects) reopenedUuids.insert(object->id());
        check(reopenedUuids == hvacUuids,
              "HVAC_aircoil reopen preserves every object UUID");
        check(FdsWriter::render(*reopenedHvac.project).text == hvacRendered.text,
              "HVAC_aircoil reopen preserves its FDS output");
    }

    const auto validateTunnelTutorial = [&importer](
        std::unique_ptr<FcProject> tunnel,
        const QString& referenceRelativePath,
        const char* createdMessage,
        const char* canonicalMessage,
        const char* reopenMessage) {
        check(tunnel != nullptr, createdMessage);
        if (!tunnel) return;
        std::vector<std::shared_ptr<FcFdsNamelist>> objects;
        std::set<QString> uuids;
        for (const auto& group : tunnel->document()->groups()) {
            collectNamelists(group, objects);
        }
        for (const auto& object : objects) uuids.insert(object->id());
        check(objects.size() == 11,
              "Tunnel tutorial contains all 11 editable business objects");
        bool referencesValid = true;
        int referenceCount = 0;
        for (const auto& object : objects) {
            for (const FcFdsParameter& parameter : object->parameters()) {
                if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
                referenceCount += static_cast<int>(parameter.targetObjectIds.size());
                for (const QString& uuid : parameter.targetObjectIds) {
                    referencesValid = referencesValid && uuids.count(uuid) == 1;
                }
            }
        }
        check(referencesValid && referenceCount == 2,
              "Tunnel mesh multiplier and fire surface links use UUIDs");
        const FdsWriteResult renderedTunnel = FdsWriter::render(*tunnel);
        if (!renderedTunnel.success()) {
            for (const QString& error : renderedTunnel.errors) {
                std::cerr << "tunnel render error: " << error.toStdString() << '\n';
            }
        }
        check(renderedTunnel.success(), "Tunnel tutorial object tree renders");
        check(renderedTunnel.text.contains(QStringLiteral("PBX=128.0")),
              "Plane-boundary VENT validates and renders");
        const QString referencePath =
            QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR)).filePath(referenceRelativePath);
        const FdsImportResult referenceProject = importer.importFile(referencePath);
        check(referenceProject.success(),
              "Tunnel reference imports for semantic comparison");
        if (referenceProject.project) {
            check(FdsWriter::render(*referenceProject.project).text ==
                      renderedTunnel.text,
                  canonicalMessage);
        }
        QTemporaryDir projectDirectory;
        const QString projectPath = projectDirectory.filePath(
            tunnel->chid() + QStringLiteral(".firecae"));
        QString saveError;
        check(FcProjectSerializer::save(*tunnel, projectPath, &saveError),
              "Tunnel tutorial saves as a FireCAE project");
        const FcProjectLoadResult reopenedTunnel =
            FcProjectSerializer::load(projectPath);
        check(reopenedTunnel.success(), "Saved tunnel tutorial reopens");
        if (reopenedTunnel.project) {
            std::vector<std::shared_ptr<FcFdsNamelist>> reopenedObjects;
            std::set<QString> reopenedUuids;
            for (const auto& group : reopenedTunnel.project->document()->groups()) {
                collectNamelists(group, reopenedObjects);
            }
            for (const auto& object : reopenedObjects) {
                reopenedUuids.insert(object->id());
            }
            check(reopenedUuids == uuids &&
                      FdsWriter::render(*reopenedTunnel.project).text ==
                          renderedTunnel.text,
                  reopenMessage);
        }
    };
    validateTunnelTutorial(
        FdsExamples::createTunnelDemoProject(),
        QStringLiteral("fds-tutorials/tunnel_demo/tunnel_demo.fds"),
        "tunnel_demo object factory creates a project",
        "FireCAE tunnel_demo output is canonically equal to the reference",
        "tunnel_demo reopen preserves UUIDs and FDS output");
    validateTunnelTutorial(
        FdsExamples::createTunnelSmoke10sProject(),
        QStringLiteral("fds-tutorials/tunnel_smoke_10s/tunnel_smoke_10s.fds"),
        "tunnel_smoke_10s object factory creates a project",
        "FireCAE tunnel_smoke_10s output is canonically equal to the reference",
        "tunnel_smoke_10s reopen preserves UUIDs and FDS output");
    const std::unique_ptr<FcProject> tunnelStatisticsProject =
        FdsExamples::createTunnelDemoProject();
    const FdsModelStatistics tunnelStatistics =
        FdsWriter::modelStatistics(*tunnelStatisticsProject);
    check(tunnelStatistics.meshes.size() == 1 &&
              tunnelStatistics.expandedMeshCount == 8 &&
              tunnelStatistics.totalCellCount == 256000 &&
              tunnelStatistics.meshes.front().instanceCount == 8,
          "Tunnel MULT expansion reports 8 meshes and 256000 cells for MPI planning");

    std::unique_ptr<FcProject> outsideSlice =
        FdsExamples::createTunnelSmoke10sProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> outsideSliceObjects;
    for (const auto& group : outsideSlice->document()->groups()) {
        collectNamelists(group, outsideSliceObjects);
    }
    const auto sliceOutsideMesh = std::find_if(
        outsideSliceObjects.cbegin(), outsideSliceObjects.cend(),
        [](const auto& object) {
            return object->keyword() == QStringLiteral("SLCF") &&
                   !object->parameterValue(QStringLiteral("PBY")).isEmpty();
        });
    if (sliceOutsideMesh != outsideSliceObjects.cend()) {
        std::vector<FcFdsParameter> parameters = (*sliceOutsideMesh)->parameters();
        for (FcFdsParameter& parameter : parameters) {
            if (parameter.key == QStringLiteral("PBY")) {
                parameter.value = QStringLiteral("999");
            }
        }
        (*sliceOutsideMesh)->setParameters(parameters);
        const FdsWriteResult outsideResult = FdsWriter::render(*outsideSlice);
        const QString outsideErrors = outsideResult.errors.join(QLatin1Char('\n'));
        check(!outsideResult.success() &&
                  outsideErrors.contains((*sliceOutsideMesh)->id()) &&
                  outsideErrors.contains(QStringLiteral(
                      "lies outside every expanded mesh extent")),
              "Slice planes outside MULT-expanded mesh extents are rejected");
    } else {
        check(false, "Outside-slice validation fixture is complete");
    }

    std::unique_ptr<FcProject> overlappingMeshes =
        FdsExamples::createTunnelSmoke10sProject();
    auto overlappingMesh = std::make_shared<FcFdsNamelist>(
        QStringLiteral("Overlapping mesh"), FcObjectType::Mesh,
        QStringLiteral("MESH"), QString{}, 11);
    overlappingMesh->addRawParameter(QStringLiteral("IJK"),
                                     QStringLiteral("80,20,20"));
    overlappingMesh->addRawParameter(QStringLiteral("XB"),
                                     QStringLiteral("0,16,-2,2,0,4"));
    overlappingMeshes->document()->meshesGroup()->addChild(overlappingMesh);
    const FdsWriteResult overlapResult = FdsWriter::render(*overlappingMeshes);
    const QString overlapErrors = overlapResult.errors.join(QLatin1Char('\n'));
    check(!overlapResult.success() &&
              overlapErrors.contains(overlappingMesh->id()) &&
              overlapErrors.contains(QStringLiteral("with a non-zero volume")),
          "Meshes with a non-zero overlap are rejected with object UUIDs");

    std::unique_ptr<FcProject> gappedMeshes =
        FdsExamples::createSimpleTestProject();
    auto gappedMesh = std::make_shared<FcFdsMesh>(
        QStringLiteral("Distant mesh"), QStringLiteral("MESH_GAP"),
        std::array<int, 3>{10, 10, 10},
        FcFdsBounds{10.0, 11.0, 0.0, 1.0, 0.0, 1.0});
    gappedMeshes->document()->meshesGroup()->addChild(gappedMesh);
    const FdsWriteResult gapResult = FdsWriter::render(*gappedMeshes);
    check(gapResult.success() &&
              gapResult.warnings.join(QLatin1Char('\n')).contains(gappedMesh->id()) &&
              gapResult.warnings.join(QLatin1Char('\n')).contains(
                  QStringLiteral("unintended mesh gap")),
          "Disconnected mesh groups produce a UUID-addressable gap warning");

    std::unique_ptr<FcProject> discontinuousMeshes =
        FdsExamples::createSimpleTestProject();
    const FcFdsBounds firstMeshBounds =
        std::dynamic_pointer_cast<FcFdsMesh>(
            discontinuousMeshes->document()->meshesGroup()->children().front())
            ->bounds();
    auto discontinuousMesh = std::make_shared<FcFdsMesh>(
        QStringLiteral("Discontinuous mesh"), QStringLiteral("MESH_DISCONTINUOUS"),
        std::array<int, 3>{10, 17, 24},
        FcFdsBounds{firstMeshBounds.xMax, firstMeshBounds.xMax + 1.0,
                    firstMeshBounds.yMin, firstMeshBounds.yMax,
                    firstMeshBounds.zMin, firstMeshBounds.zMax});
    discontinuousMeshes->document()->meshesGroup()->addChild(discontinuousMesh);
    const FdsWriteResult discontinuityResult =
        FdsWriter::render(*discontinuousMeshes);
    check(discontinuityResult.success() &&
              discontinuityResult.warnings.join(QLatin1Char('\n')).contains(
                  discontinuousMesh->id()) &&
              discontinuityResult.warnings.join(QLatin1Char('\n')).contains(
                  QStringLiteral("boundary cell lines")),
          "Adjacent meshes with discontinuous tangential grids produce a UUID warning");

    std::unique_ptr<FcProject> scenarioProject =
        FdsExamples::createActivateVentsProject();
    std::vector<std::shared_ptr<FcFdsNamelist>> scenarioObjects;
    for (const auto& group : scenarioProject->document()->groups()) {
        collectNamelists(group, scenarioObjects);
    }
    const auto scenarioMesh = std::find_if(
        scenarioObjects.cbegin(), scenarioObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("MESH");
        });
    const auto scenarioOutput = std::find_if(
        scenarioObjects.cbegin(), scenarioObjects.cend(), [](const auto& object) {
            return object->keyword() == QStringLiteral("SLCF");
        });
    const QString scenarioId = scenarioProject->addScenario(QStringLiteral("Door Open"));
    FcScenario scenario = *scenarioProject->scenario(scenarioId);
    scenario.chid = QStringLiteral("activate_vents_door_open");
    scenario.outputDirectory = QStringLiteral("runs/door_open");
    scenario.solverBackendId = QStringLiteral("fds.mpi.cpu");
    scenario.processCount = 2;
    if (scenarioOutput != scenarioObjects.cend()) {
        scenario.disabledObjectIds.insert((*scenarioOutput)->id());
    }
    if (scenarioMesh != scenarioObjects.cend()) {
        scenario.parameterOverrides.append(
            {(*scenarioMesh)->id(), QStringLiteral("IJK"),
             QStringLiteral("24,12,12"), {}, false});
    }
    check(scenarioProject->updateScenario(scenario) &&
              scenarioProject->setActiveScenario(scenarioId),
          "A scenario stores differential solver settings, disabled UUIDs, and overrides");
    const FdsWriteResult scenarioRender = FdsWriter::render(*scenarioProject);
    check(scenarioRender.success() &&
              scenarioRender.text.contains(QStringLiteral("CHID='activate_vents_door_open'")) &&
              scenarioRender.text.contains(QStringLiteral("IJK=24,12,12")) &&
              (scenarioOutput == scenarioObjects.cend() ||
               !scenarioRender.text.contains(QStringLiteral("&SLCF"))),
          "Active scenario differences are applied without copying the base object tree");
    QTemporaryDir scenarioDirectory;
    const QString scenarioProjectPath = scenarioDirectory.filePath(
        QStringLiteral("scenario-roundtrip.firecae"));
    QString scenarioSaveError;
    check(FcProjectSerializer::save(*scenarioProject, scenarioProjectPath,
                                    &scenarioSaveError),
          "Scenario project is saved in the versioned project format");
    const FcProjectLoadResult reopenedScenario =
        FcProjectSerializer::load(scenarioProjectPath);
    check(reopenedScenario.success() &&
              reopenedScenario.project->scenarios().size() == 2 &&
              reopenedScenario.project->activeScenario() &&
              reopenedScenario.project->activeScenario()->chid ==
                  QStringLiteral("activate_vents_door_open") &&
              reopenedScenario.project->activeScenario()->parameterOverrides.size() == 1,
          "Scenario UUIDs, active/default state, disabled objects, and overrides survive reopen");

    if (failures == 0) {
        std::cout << "FireCAE FDS model tests passed.\n";
        return 0;
    }
    return 1;
}
