#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsExamples.h"
#include "fds/FdsWriter.h"
#include "results/FcResultCase.h"
#include "results/FcResultFile.h"
#include "results/FdsResultComparator.h"
#include "results/FdsResultScanner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
    if (!condition) ++failures;
}

FcScenarioParameterOverride rawOverride(const QString& id, const QString& key, const QString& value)
{
    FcScenarioParameterOverride entry;
    entry.objectId = id;
    entry.parameterKey = key;
    entry.value = value;
    return entry;
}

void typedScenarioOverrides()
{
    auto project = FdsExamples::createSimpleTestProject();
    const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(
        project->document()->meshesGroup()->children().front());
    const auto surface = std::dynamic_pointer_cast<FcFdsSurface>(
        project->document()->surfacesGroup()->children().front());
    check(mesh && surface, "M01 built-in example contains typed mesh and surface");
    if (!mesh || !surface) return;
    const QString baseId = project->activeScenarioId();
    const QString scenarioId = project->addScenario(QStringLiteral("Coarse low fire"));
    project->setScenarioOverride(scenarioId,
        rawOverride(mesh->id(), QStringLiteral("IJK"), QStringLiteral("12,12,12")));
    project->setScenarioOverride(scenarioId,
        rawOverride(surface->id(), QStringLiteral("HRRPUA"), QStringLiteral("200")));
    project->setActiveScenario(scenarioId);
    const auto output = FdsWriter::render(*project);
    check(output.success(), "M01 valid typed scenario renders successfully");
    check(output.text.contains(QStringLiteral("IJK=12,12,12")),
          "M01 typed mesh override changes actual FDS input");
    check(output.text.contains(QStringLiteral("HRRPUA=200")),
          "M01 typed surface override changes actual FDS input");
    check(FdsWriter::modelStatistics(*project).totalCellCount == 1728,
          "M01 task statistics use effective scenario mesh cells");
    check(mesh->cells() == std::array<int, 3>{36, 24, 24} &&
              surface->heatReleaseRatePerArea() == 1000.0,
          "M01 rendering scenario does not mutate base typed objects");

    QTemporaryDir directory;
    QString error;
    const QString path = directory.filePath(QStringLiteral("typed-scenario.firecae"));
    check(directory.isValid() && FcProjectSerializer::save(*project, path, &error),
          "M01 typed scenario saves");
    auto reopened = FcProjectSerializer::load(path);
    check(reopened.success(), "M01 typed scenario reopens");
    if (reopened.success()) {
        const auto restoredOutput = FdsWriter::render(*reopened.project);
        check(restoredOutput.success() && restoredOutput.text == output.text,
              "M01 effective scenario input survives project round trip");
    }
    project->setActiveScenario(baseId);
    const auto baseOutput = FdsWriter::render(*project);
    check(baseOutput.success() && baseOutput.text.contains(QStringLiteral("IJK=36,24,24")) &&
              baseOutput.text.contains(QStringLiteral("HRRPUA=1000")),
          "M01 switching to base restores original solver input");
    project->setActiveScenario(scenarioId);
    project->setScenarioOverride(scenarioId,
        rawOverride(mesh->id(), QStringLiteral("IJK"), QStringLiteral("0,12,12")));
    check(!FdsWriter::render(*project).success(),
          "M01 invalid effective typed mesh is rejected before export");
}

void typedFieldsAndReferences()
{
    auto project = FdsExamples::createSimpleTestProject();
    auto* document = project->document();
    const auto reaction = std::dynamic_pointer_cast<FcFdsReaction>(document->reactionsGroup()->children().front());
    const auto surface = std::dynamic_pointer_cast<FcFdsSurface>(document->surfacesGroup()->children().front());
    const auto obstacle = std::dynamic_pointer_cast<FcFdsObstruction>(document->geometryGroup()->children().front());
    const auto vent = std::dynamic_pointer_cast<FcFdsVent>(document->ventsGroup()->children().front());
    const auto boundary = std::dynamic_pointer_cast<FcFdsOutput>(document->outputsGroup()->children().at(0));
    const auto slice = std::dynamic_pointer_cast<FcFdsOutput>(document->outputsGroup()->children().at(1));
    const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(document->meshesGroup()->children().front());
    check(reaction && surface && obstacle && vent && boundary && slice && mesh,
          "M01 typed field fixtures are available");
    if (!reaction || !surface || !obstacle || !vent || !boundary || !slice || !mesh) return;
    const QString scenario = project->addScenario(QStringLiteral("All typed fields"));
    project->setActiveScenario(scenario);
    for (const auto& entry : {
             rawOverride(reaction->id(), QStringLiteral("FUEL"), QStringLiteral("'METHANE'")),
             rawOverride(reaction->id(), QStringLiteral("SOOT_YIELD"), QStringLiteral("0.02")),
             rawOverride(surface->id(), QStringLiteral("COLOR"), QStringLiteral("'BLUE'")),
             rawOverride(obstacle->id(), QStringLiteral("XB"), QStringLiteral("0,0.6,1,1.4,0,0.2")),
             rawOverride(vent->id(), QStringLiteral("XB"), QStringLiteral("0,0.6,1,1.4,0.2,0.2")),
             rawOverride(boundary->id(), QStringLiteral("QUANTITY"), QStringLiteral("'WALL TEMPERATURE'")),
             rawOverride(slice->id(), QStringLiteral("PBX"), QStringLiteral("0.4")),
             rawOverride(slice->id(), QStringLiteral("VECTOR"), QStringLiteral(".FALSE."))}) {
        project->setScenarioOverride(scenario, entry);
    }
    FcScenarioParameterOverride reference;
    reference.objectId = obstacle->id();
    reference.parameterKey = QStringLiteral("SURF_ID");
    reference.reference = true;
    reference.targetObjectIds = {surface->id()};
    project->setScenarioOverride(scenario, reference);
    const auto output = FdsWriter::render(*project);
    check(output.success() && output.text.contains(QStringLiteral("FUEL='METHANE'")) &&
              output.text.contains(QStringLiteral("SOOT_YIELD=0.02")) &&
              output.text.contains(QStringLiteral("COLOR='BLUE'")) &&
              output.text.contains(QStringLiteral("XB=0,0.6,1,1.4,0,0.2")) &&
              output.text.contains(QStringLiteral("SURF_ID='BURNER'")) &&
              output.text.contains(QStringLiteral("QUANTITY='WALL TEMPERATURE'")) &&
              output.text.contains(QStringLiteral("PBX=0.4")) &&
              !output.text.contains(QStringLiteral("VECTOR=.TRUE.")),
          "M01 typed geometry, reaction, string, reference and output overrides reach FDS");
    check(reaction->fuel() == QStringLiteral("PROPANE") && surface->color() == QStringLiteral("RED") &&
              obstacle->surfaceId().isEmpty() && slice->planeAxis() == FcFdsPlaneAxis::Y && slice->vectorOutput(),
          "M01 applying multiple typed fields leaves original values intact");
    reference.targetObjectIds = {mesh->id()};
    project->setScenarioOverride(scenario, reference);
    check(!FdsWriter::render(*project).success(), "M01 typed surface override rejects wrong target type");
    reference.targetObjectIds = {surface->id()};
    project->setScenarioOverride(scenario, reference);
    project->setScenarioOverride(scenario,
        rawOverride(QStringLiteral("missing-object"), QStringLiteral("IJK"), QStringLiteral("2,2,2")));
    check(!FdsWriter::render(*project).success(), "M01 missing override owner cannot silently disappear");
}

void resultPersistence()
{
    auto project = FdsExamples::createSimpleTestProject();
    QTemporaryDir directory;
    check(directory.isValid(), "M03 temporary project directory exists");
    const QString resultDirectory = directory.filePath(QStringLiteral("unavailable-results"));
    const QString smv = QDir(resultDirectory).filePath(QStringLiteral("case.smv"));
    const QString fds = QDir(resultDirectory).filePath(QStringLiteral("case.fds"));
    const QString csv = QDir(resultDirectory).filePath(QStringLiteral("case_hrr.csv"));
    const QDateTime timestamp = QDateTime::fromString(
        QStringLiteral("2026-09-15T04:05:06.123Z"), Qt::ISODateWithMs);
    auto result = std::make_shared<FcResultCase>(QStringLiteral("Missing but retained result"));
    result->updateMetadata(resultDirectory, smv, fds, FcResultStatus::MissingFiles,
                           1, QStringLiteral("0"), QStringLiteral("60"), timestamp, 2);
    result->setLocked(true);
    auto file = std::make_shared<FcResultFile>(
        QStringLiteral("case_hrr.csv"), FcResultFileType::HeatReleaseRate, csv,
        qint64{6442450945}, false, timestamp, 3, qint64{4294967297},
        QStringLiteral("0"), QStringLiteral("60"),
        QStringList{QStringLiteral("Time"), QStringLiteral("HRR"), QStringLiteral("Q_RADI")});
    result->addChild(file);
    project->document()->resultsGroup()->addChild(result);
    const QString path = directory.filePath(QStringLiteral("results.firecae"));
    QString error;
    check(FcProjectSerializer::save(*project, path, &error), "M03 project with results saves");
    auto loaded = FcProjectSerializer::load(path);
    check(loaded.success(), "M03 project opens even when external results are absent");
    if (!loaded.success()) return;
    const auto restored = std::dynamic_pointer_cast<FcResultCase>(
        loaded.project->document()->findObject(result->id()));
    const auto restoredFile = std::dynamic_pointer_cast<FcResultFile>(
        loaded.project->document()->findObject(file->id()));
    check(restored != nullptr, "M03 result case retains its concrete type and UUID");
    check(restoredFile != nullptr, "M03 result file retains its concrete type and UUID");
    if (restored) {
        check(QDir::fromNativeSeparators(restored->resultDirectory()) == QDir::fromNativeSeparators(resultDirectory) &&
                  QDir::fromNativeSeparators(restored->smvFilePath()) == QDir::fromNativeSeparators(smv) &&
                  QDir::fromNativeSeparators(restored->fdsInputFilePath()) == QDir::fromNativeSeparators(fds) && restored->isLocked() &&
                  restored->status() == FcResultStatus::MissingFiles &&
                  restored->resultFileCount() == 1 && restored->warningCount() == 2 &&
                  restored->startTime() == QStringLiteral("0") &&
                  restored->endTime() == QStringLiteral("60") &&
                  restored->lastScanTime() == timestamp,
              "M03 result case preserves paths, status, timestamps and display metadata");
    }
    if (restoredFile) {
        check(QDir::fromNativeSeparators(restoredFile->filePath()) == QDir::fromNativeSeparators(csv) &&
                  restoredFile->fileType() == FcResultFileType::HeatReleaseRate &&
                  restoredFile->fileSize() == file->fileSize() &&
                  restoredFile->dataRowCount() == file->dataRowCount() &&
                  restoredFile->columnCount() == 3 && !restoredFile->exists() &&
                  restoredFile->lastModified() == timestamp &&
                  restoredFile->columnNames() == file->columnNames() &&
                  restoredFile->parent() == restored.get(),
              "M03 result file preserves large counters, columns and parent identity");
    }
}
}

namespace {
void eventLogComparisonBoundary()
{
    QTemporaryDir root;
    check(root.isValid(), "SR-07 isolated event-log fixture directory exists");
    if (!root.isValid()) return;
    const auto write = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               file.write(data) == data.size();
    };
    // Real FDS event-log shape from activate_vents: mixed text, optional trailing
    // fields, simultaneous events and a final event before T_END are all valid.
    const QByteArray events =
        "Time (s),Type,ID,State,Value,Units\n"
        " 3.00000E+00,CTRL,controller 1,T, 9.80000E-03\n"
        " 5.05000E+00,DEVC,timer 2,T, 5.05000E+00,s\n"
        " 5.05000E+00,DEVC,timer 5,T, 5.05000E+00,s\n"
        " 8.10000E+00,CTRL,controller 3,T\n";
    const QByteArray hrr = "s,kW\nTime,HRR\n0,0\n5,10\n20,100\n";
    const QByteArray devices = "s,C\nTime,Sensor\n0,20\n5,22\n20,25\n";
    const QByteArray controls = "s,status\nTime,controller 1\n0,-1\n5,1\n20,1\n";
    bool written = true;
    for (const QString& side : {QStringLiteral("reference"), QStringLiteral("candidate")}) {
        const QDir directory(root.filePath(side));
        written = QDir().mkpath(directory.path()) && written;
        written = write(directory.filePath("case.smv"), "TITLE\nSR-07 synthetic result\n") && written;
        written = write(directory.filePath("case.fds"), "&HEAD CHID='case' /\n&TIME T_END=20 /\n&TAIL /\n") && written;
        written = write(directory.filePath("case.out"),
                        "Revision : FDS-fixture-0\nTotal Time: 20 s\nSTOP: FDS completed successfully\n") && written;
        written = write(directory.filePath("case_hrr.csv"), hrr) && written;
        written = write(directory.filePath("case_devc.csv"), devices) && written;
        written = write(directory.filePath("case_ctrl.csv"), controls) && written;
        written = write(directory.filePath("case_devc_ctrl_log.csv"), events) && written;
    }
    check(written, "SR-07 complete physical time series and real-format event logs written");
    if (!written) return;
    const QString reference = root.filePath("reference/case.smv");
    const QString candidate = root.filePath("candidate/case.smv");
    const auto scan = FdsResultScanner().scanSmvFile(candidate);
    bool eventRetained = false;
    for (const auto& file : scan.files)
        if (file.exists && file.type == FcResultFileType::DeviceControlLog) eventRetained = true;
    check(scan.success() && eventRetained,
          "SR-07 scanner retains the event log with its distinct file type");
    const auto comparison = FdsResultComparator::compareSmvFiles(reference, candidate);
    check(comparison.passed() && comparison.matchedFileCount == 3 && comparison.quantities.size() == 3,
          "SR-07 real-format events do not invalidate complete HRR DEVC and CTRL time series");
    check(comparison.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Event log retained")),
          "SR-07 report discloses event logs excluded from numerical time-series comparison");
    QFile eventFile(root.filePath("candidate/case_devc_ctrl_log.csv"));
    check(eventFile.open(QIODevice::ReadOnly) && eventFile.readAll() == events,
          "SR-07 comparison preserves original event-log bytes");
    eventFile.close();
    const QString devicePath = root.filePath("candidate/case_devc.csv");
    check(QFile::remove(devicePath) && !FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-07 missing physical DEVC remains a comparison failure");
    check(write(devicePath, "s,C\nTime,Sensor\n") &&
              !FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-07 header-only physical DEVC remains a comparison failure");
    check(write(devicePath, "s,C\nTime,Sensor\n0,20\n5,22\n") &&
              !FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-07 incomplete physical DEVC coverage remains a comparison failure");
    check(write(devicePath, devices) &&
              write(root.filePath("candidate/case_ctrl.csv"),
                    "s,status\nTime,controller 1\n0,-1\n5,T\n20,1\n") &&
              !FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-07 sampled CTRL with invalid numeric data remains a comparison failure");
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    typedScenarioOverrides();
    typedFieldsAndReferences();
    resultPersistence();
    eventLogComparisonBoundary();
    std::cout << "Reacceptance model failures: " << failures << '\n';
    return failures ? 1 : 0;
}
