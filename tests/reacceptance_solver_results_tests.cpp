// Boundary regressions for SR-01..SR-05. Generated fixtures are synthetic and
// must never be reported as evidence of successful real FDS calculations.
#include "results/FdsResultComparator.h"
#include "results/FdsResultScanner.h"
#include "results/FdsSliceReader.h"
#include "simulation/FdsRunner.h"
#include "simulation/SimulationTaskManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSettings>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
    if (!ok) ++failures;
}
bool put(const QString& path, const QString& value) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(value.toUtf8()) == value.toUtf8().size();
}
QString get(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}
QString csv(const QString& rows, const QString& unit = QStringLiteral("C")) {
    return QStringLiteral("s,%1\nTime,Probe\n%2").arg(unit, rows);
}
QString makeCase(const QString& directory, const QString& chid,
                 const QString& deviceCsv, const QString& hrrRows =
                     QStringLiteral("0,0\n1,10\n10,100\n")) {
    QDir().mkpath(directory);
    const QDir dir(directory);
    const QString smv = dir.filePath(chid + QStringLiteral(".smv"));
    check(put(smv, QStringLiteral("TITLE\n Synthetic regression fixture\n")) &&
          put(dir.filePath(chid + QStringLiteral(".fds")),
              QStringLiteral("&HEAD CHID='%1' /\n&TIME T_END=10 /\n&TAIL /\n").arg(chid)) &&
          put(dir.filePath(chid + QStringLiteral(".out")),
              QStringLiteral("Revision : FDS-fixture-0\nTotal Time: 10 s\n"
                             "STOP: FDS completed successfully\n")) &&
          put(dir.filePath(chid + QStringLiteral("_hrr.csv")),
              QStringLiteral("s,kW\nTime,HRR\n") + hrrRows) &&
          put(dir.filePath(chid + QStringLiteral("_devc.csv")), deviceCsv),
          "Synthetic result fixture written");
    return smv;
}
void resultRegressions() {
    QTemporaryDir root;
    check(root.isValid(), "Result fixture root created");
    if (!root.isValid()) return;
    const QString reference = makeCase(root.filePath(QStringLiteral("reference")),
                                      QStringLiteral("case"), csv("0,0\n1,10\n10,100\n"));
    const QString candidate = makeCase(root.filePath(QStringLiteral("candidate")),
                                      QStringLiteral("case"), csv("0,0\n1,10\n10,100\n"));
    check(FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "Control: independent complete same-unit cases compare successfully");

    const QString devc = QFileInfo(candidate).absoluteDir().filePath("case_devc.csv");
    put(devc, csv("0,0\n1,10\n"));
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-04 incomplete DEVC time coverage cannot pass despite full HRR coverage");
    put(devc, "s,C\nTime,Probe\n");
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-04 header-only required physical CSV cannot be silently omitted");
    put(devc, csv("2,20\n3,30\n"));
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-04 required quantity with no reference sample overlap cannot pass");
    put(devc, csv("0,0\n1,10\n10,100\n", "m/s"));
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-05 same name and numbers with incompatible physical units cannot pass");
    put(devc, "min,C\nTime,Probe\n0,0\n1,10\n10,100\n");
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-05 same time numbers in seconds versus minutes cannot pass");

    const QString referenceDevc = QFileInfo(reference).absoluteDir().filePath("case_devc.csv");
    put(referenceDevc, csv("0,0\n1,10\n10,100\n", "mW"));
    put(devc, csv("0,0\n1,10\n10,100\n", "MW"));
    check(!FdsResultComparator::compareSmvFiles(reference, candidate).passed(),
          "SR-05 SI unit prefixes remain case-sensitive (mW is not MW)");
    put(referenceDevc, "s,C,C\nTime,Probe1,Probe2\n0,10,20\n1,11,21\n10,12,22\n");
    put(devc, "s,C,C\nTime,Probe1,Probe2\n0,10,20\n1,11\n10,12,22\n");
    bool noException = true, didNotPass = false;
    try { didNotPass = !FdsResultComparator::compareSmvFiles(reference, candidate).passed(); }
    catch (const std::exception& e) {
        noException = false;
        std::cout << "Caught current application exception: " << e.what() << '\n';
    }
    check(noException && didNotPass,
          "SR-04 short numeric row returns a structured non-pass without throwing");

    const QString shared = root.filePath(QStringLiteral("shared"));
    makeCase(shared, "a", csv("0,0\n100,100\n"), "0,0\n100,100\n");
    const QString b = makeCase(shared, "b", csv("0,0\n2,20\n"), "0,0\n2,20\n");
    put(QDir(shared).filePath("b.out"),
        "Revision : FDS-fixture-b\nERROR: b did not complete\n");
    const auto scan = FdsResultScanner().scanSmvFile(b);
    const bool noForeignFiles = std::none_of(scan.files.begin(), scan.files.end(),
        [](const FdsResultFileInfo& file) { return file.name.startsWith("a.") ||
                                                      file.name.startsWith("a_"); });
    check(scan.success() && noForeignFiles && scan.endTime.toDouble() == 2.0,
          "SR-01 b scan excludes neighboring a files and preserves b end time");
    const auto provenance = FdsResultComparator::compareSmvFiles(b, b).referenceProvenance;
    check(provenance.outputLogFile.endsWith("b.out") && !provenance.normalTermination,
          "SR-01 b provenance uses b output log, not alphabetically first neighboring log");
    const QString prefixed = root.filePath(QStringLiteral("prefixed"));
    const QString aSmv = makeCase(prefixed, "a", csv("0,0\n2,20\n"), "0,0\n2,20\n");
    makeCase(prefixed, "a_x", csv("0,0\n100,100\n"), "0,0\n100,100\n");
    const auto prefixScan = FdsResultScanner().scanSmvFile(aSmv);
    check(prefixScan.success() && prefixScan.endTime.toDouble() == 2.0 &&
          std::none_of(prefixScan.files.begin(), prefixScan.files.end(),
              [](const FdsResultFileInfo& file) { return file.name.startsWith("a_x"); }),
          "SR-01 underscore prefix case a never absorbs neighboring case a_x");
}
void outputDecodingRegressions(const QString& fixtureExecutable) {
    const QString title = QStringLiteral("桌面验收 Save First Fire");
    const QByteArray utf8 = title.toUtf8();
    const QByteArray local = QByteArray::fromHex("4c4f43414c3a20b4edcef30d0a");
    const QByteArray ascii = "Simulation Time: 30.0\r\nSTOP: FDS completed successfully\n";
    const QList<QByteArray> samples{utf8 + "\r\n", local, ascii, utf8};
    const QStringList expected{title + "\r\n", QString::fromLocal8Bit(local),
                               QString::fromLatin1(ascii), title};
    for (qsizetype sample = 0; sample < samples.size(); ++sample) {
        bool exact = true;
        for (qsizetype split = 0; split <= samples[sample].size(); ++split) {
            FdsOutputTextDecoder decoder;
            QString decoded = decoder.append(samples[sample].left(split));
            decoded += decoder.append(samples[sample].mid(split));
            decoded += decoder.finish();
            exact = exact && decoded == expected[sample] && decoder.finish().isEmpty();
            exact = exact && decoder.append("next run\n") == "next run\n";
        }
        check(exact, "Output decoder preserves every UTF-8/local/ASCII/EOF split and resets after flush");
    }
    FdsOutputTextDecoder outputDecoder, errorDecoder;
    QString output, error;
    for (qsizetype i = 0; i < qMax(utf8.size(), local.size()); ++i) {
        if (i < utf8.size()) output += outputDecoder.append(utf8.mid(i, 1));
        if (i < local.size()) error += errorDecoder.append(local.mid(i, 1));
    }
    check(output.isEmpty(), "Output decoder retains an unterminated UTF-8 line until EOF");
    output += outputDecoder.finish();
    error += errorDecoder.finish();
    check(output == title && error == QString::fromLocal8Bit(local),
          "Output decoder retains independent interleaved stdout and stderr byte state");
    check(FdsOutputTextDecoder::decodeComplete(utf8 + "\n" + local + utf8) ==
              title + "\n" + QString::fromLocal8Bit(local) + title,
          "Output decoder chooses encoding per line, including an unterminated final line");
    check(FdsOutputTextDecoder::decodeComplete(QByteArray::fromHex("c2a9")) ==
              QString(QChar(0x00a9)),
          "Output decoder explicitly prefers UTF-8 when bytes are valid in both encodings");

    check(QFileInfo::exists(fixtureExecutable), "Stream fixture executable exists");
    if (!QFileInfo::exists(fixtureExecutable)) return;
    QTemporaryDir root;
    if (!root.isValid()) { check(false, "Stream fixture root created"); return; }
    FdsRunner runner;
    const QString expectedOutput = "UTF8: " + title + "\r\nASCII: 30.0\r\nUTF8 tail: " + title;
    const QByteArray expectedErrorBytes = local + "local tail: " + QByteArray::fromHex("b4edcef3");
    const QString expectedError = QString::fromLocal8Bit(expectedErrorBytes);
    // Reuse one runner: normal EOF, synchronous stop/reset, then another run.
    for (int iteration = 0; iteration < 3; ++iteration) {
        const bool stopAtReady = iteration == 1;
        const QString directory = root.filePath(QString::number(iteration));
        QDir().mkpath(directory);
        const QString input = QDir(directory).filePath("stream.fds");
        put(input, "&HEAD CHID='stream' /\n&TIME T_END=1 /\n&TAIL /\n! TEST_STREAM_ENCODING\n" +
                   QString(stopAtReady ? "! TEST_STREAM_WAIT\n" : ""));
        FdsRunRequest request;
        request.inputFilePath = input;
        request.executablePath = fixtureExecutable;
        QString observedOutput, observedError, startError;
        FdsRunSummary summary;
        bool finished = false, stopped = false, tailsAtFinish = false;
        QEventLoop loop;
        QTimer timeout, ready;
        timeout.setSingleShot(true);
        QObject::connect(&runner, &FdsRunner::outputReceived, &loop,
            [&](const QString& text) { observedOutput += text; });
        QObject::connect(&runner, &FdsRunner::errorOutputReceived, &loop,
            [&](const QString& text) { observedError += text; });
        QObject::connect(&runner, &FdsRunner::runFinished, &loop,
            [&](const FdsRunSummary& value) {
                summary = value;
                tailsAtFinish = observedOutput == expectedOutput && observedError == expectedError;
                finished = true;
                loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&ready, &QTimer::timeout, &loop, [&]() {
            if (!QFileInfo::exists(QDir(directory).filePath("stream.ready"))) return;
            ready.stop();
            stopped = runner.stopAndWait();
            loop.quit();
        });
        const bool started = runner.start(request, &startError);
        if (started) {
            timeout.start(15000);
            if (stopAtReady) ready.start(10);
            if (!finished) loop.exec();
        }
        if (runner.isRunning()) runner.stopAndWait();
        check(started && (stopAtReady ? stopped : finished && summary.success),
              "Stream fixture completes or stops within its deadline");
        check(observedOutput == expectedOutput && observedError == expectedError,
              "FdsRunner preserves both channels, UTF-8 title and local EOF tail through finish/reset/reuse");
        if (!stopAtReady) {
            check(tailsAtFinish && summary.standardError == expectedError,
                  "FdsRunner flushes EOF before runFinished and includes stderr tail in summary");
        }
    }
}
FdsRunSummary run(const FdsRunRequest& request) {
    FdsRunner runner;
    FdsRunSummary summary;
    bool finished = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&runner, &FdsRunner::runFinished, &loop,
        [&](const FdsRunSummary& value) { summary = value; finished = true; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QString error;
    if (!runner.start(request, &error)) { summary.errorMessage = error; return summary; }
    timer.start(15000);
    if (!finished) loop.exec();
    if (!finished) { runner.stopAndWait(); summary.errorMessage = "Controlled fixture timed out"; }
    return summary;
}
FdsRunSummary runQueued(const FdsRunRequest& request,
                        const std::function<void()>& afterEnqueue = {}) {
    SimulationTaskManager manager;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    FdsRunSummary summary;
    bool finished = false;
    QObject::connect(&manager, &SimulationTaskManager::taskFinished, &loop,
        [&](const QString&, const FdsRunSummary& result) {
            summary = result; finished = true; loop.quit();
        });
    QObject::connect(&manager, &SimulationTaskManager::taskUpdated, &loop,
        [&](const QString& id) {
            const auto* record = manager.task(id);
            if (record && (record->state == SimulationTaskState::Failed || record->state == SimulationTaskState::Cancelled)) {
                summary = record->summary; summary.errorMessage = record->errorMessage;
                finished = true; loop.quit();
            }
        });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QString error;
    const QString id = manager.enqueue(request, "Acceptance regression", "Dependency compatibility", 0.2, &error);
    if (id.isEmpty()) { summary.errorMessage = error; return summary; }
    if (afterEnqueue) afterEnqueue();
    timer.start(15000);
    if (!finished) loop.exec();
    if (!finished) {
        const auto* task = manager.task(id);
        if (task && task->state == SimulationTaskState::Failed) {
            summary = task->summary; summary.errorMessage = task->errorMessage;
        } else { manager.cancel(id); summary.errorMessage = "Task regression timed out"; }
    }
    if (!summary.success) std::cout << "Task diagnostic: " << summary.errorMessage.toStdString() << '\n';
    return summary;
}
void solverRegressions(const QString& fixtureExecutable) {
    check(QFileInfo::exists(fixtureExecutable), "Controlled solver fixture executable exists");
    if (!QFileInfo::exists(fixtureExecutable)) return;
    QTemporaryDir root;
    check(root.isValid(), "Solver fixture root created");
    if (!root.isValid()) return;
    const QString input = root.filePath("case.fds");
    const QString head = "&HEAD CHID='case' /\n&TIME T_END=1 /\n&TAIL /\n";
    FdsRunRequest request;
    request.inputFilePath = input;
    request.executablePath = fixtureExecutable;
    put(input, head + "! TEST_ZERO_NO_OUTPUT\n");
    put(root.filePath("case.smv"), "TITLE\n Stale previous result\n");
    put(root.filePath("case.out"), "STOP: FDS completed successfully\n");
    check(!run(request).success, "SR-03 exit zero without fresh outputs cannot accept stale SMV");
    put(input, head + "! TEST_ZERO_ERROR\n");
    check(!run(request).success, "SR-03 exit zero with explicit ERROR cannot count as completed");
    QDir().mkpath(root.filePath("data"));
    put(root.filePath("data/dependency.csv"), "111\n");
    const QString fileReference = "&CSVF TMPFILE='data/dependency.csv' /\n";
    put(input, head + fileReference + "! TEST_VALUE=11\n! TEST_DELAY\n");
    SimulationTaskManager manager;
    manager.setMaximumConcurrentTasks(2);
    QString error;
    const QString first = manager.enqueue(request, "Synthetic test", "Before edit", 1, &error);
    put(root.filePath("data/dependency.csv"), "222\n");
    put(input, head + fileReference + "! TEST_VALUE=22\n! TEST_DELAY\n");
    const QString second = manager.enqueue(request, "Synthetic test", "After edit", 1, &error);
    check(!first.isEmpty() && !second.isEmpty(), "SR-02 two source revisions enqueue independently");
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    int completed = 0;
    QObject::connect(&manager, &SimulationTaskManager::taskFinished, &loop,
        [&](const QString&, const FdsRunSummary&) { if (++completed >= 2) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec();
    const auto* a = manager.task(first);
    const auto* b = manager.task(second);
    check(completed == 2 && a && b && a->summary.success && b->summary.success,
          "Control: both synthetic task revisions terminate successfully");
    if (a && b) {
        check(a->summary.workingDirectory != b->summary.workingDirectory &&
              get(a->summary.outputFilePath).contains("TEST_VALUE=11") &&
              get(b->summary.outputFilePath).contains("TEST_VALUE=22"),
              "SR-02 queue freezes source revision and isolates each task output directory");
        check(get(a->summary.outputFilePath).contains("DEPENDENCY=111") &&
              get(b->summary.outputFilePath).contains("DEPENDENCY=222"),
              "SR-02 relative FILE dependency content is frozen separately for each queued revision");
        const QString firstOutputPath = a->summary.outputFilePath;
        const QString firstOutputBeforeRetry = get(firstOutputPath);
        put(root.filePath("data/dependency.csv"), "333\n");
        put(input, head + "! TEST_VALUE=33\n");
        const QString retryId = manager.retry(first, &error);
        QEventLoop retryLoop;
        QTimer retryTimer;
        retryTimer.setSingleShot(true);
        QObject::connect(&manager, &SimulationTaskManager::taskFinished, &retryLoop,
            [&](const QString& id, const FdsRunSummary&) { if (id == retryId) retryLoop.quit(); });
        QObject::connect(&retryTimer, &QTimer::timeout, &retryLoop, &QEventLoop::quit);
        retryTimer.start(15000);
        if (!retryId.isEmpty()) retryLoop.exec();
        const auto* retried = manager.task(retryId);
        check(retried && retried->summary.success &&
              retried->summary.outputFilePath != firstOutputPath &&
              get(retried->summary.outputFilePath).contains("TEST_VALUE=11") &&
              get(retried->summary.outputFilePath).contains("DEPENDENCY=111") &&
              get(firstOutputPath) == firstOutputBeforeRetry,
              "SR-02 Retry reuses frozen input/dependency and leaves original task output unchanged");
    }
    const QByteArray legacyBytes = head.toUtf8() + QByteArray("! legacy comment ") +
                                  QByteArray(1, static_cast<char>(0xff)) + QByteArray("\n");
    QFile legacyInput(input);
    bool legacyWritten = legacyInput.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (legacyWritten) legacyWritten = legacyInput.write(legacyBytes) == legacyBytes.size();
    legacyInput.close();
    SimulationTaskManager byteManager;
    const QString legacyId = legacyWritten
        ? byteManager.enqueue(request, "Synthetic test", "Byte-preserving input", 1, &error) : QString{};
    const auto* legacyTask = byteManager.task(legacyId);
    QFile frozenInput(legacyTask ? legacyTask->request.inputFilePath : QString{});
    check(legacyTask && frozenInput.open(QIODevice::ReadOnly) && frozenInput.readAll() == legacyBytes,
          "SR-02 input without rewritten dependencies retains exact original bytes, including legacy comments");
    byteManager.cancel(legacyId);

    put(input, "! &HEAD CHID='wrong' / &CATF OTHER_FILES='missing' /\n"
               "&HEAD TITLE=\"CHID='wrong' &CATF /\", CHID='case' /\n&TAIL /\n");
    check(FdsRunner::caseIdFromInput(input) == "case",
          "SR-02 output CHID ignores apparent assignments and CATF inside comments or strings");
    put(root.filePath("data/include.fds"), "&MESH IJK=4,4,4 XB=0,1,0,1,0,1 /\n");
    put(input, "&HEAD CHID='case' /\n&CATF OTHER_FILES='data/include.fds' /\n"
               "&TIME T_END=1 /\n&TAIL /\n! TEST_CATF\n");
    const auto catf = runQueued(request);
    check(catf.success && catf.caseId == "case_cat" && catf.smvFilePath.endsWith("case_cat.smv"),
          "SR-02 CATF tracks derived CHID outputs after freezing relative include files");

    put(root.filePath("data/field_m1.csv"), "111\n");
    put(root.filePath("data/field_m2.csv"), "222\n");
    put(root.filePath("data/other_hrr.csv"), "unrelated history\n");
    put(input, head + "&CSVF TMPFILE='data/field_m1.csv' /\n! TEST_CSVF_TWO_MESHES\n");
    const auto pair = runQueued(request, [&] { put(root.filePath("data/field_m2.csv"), "333\n"); });
    const auto manifest = QJsonDocument::fromJson(get(QDir(pair.workingDirectory)
        .filePath("run-manifest.json")).toUtf8()).object();
    check(pair.success && get(pair.outputFilePath).contains("SIBLING=222") &&
          manifest.value("dependencies").toArray().size() == 2,
          "SR-02 CSVF freezes implicit second mesh and excludes unrelated historical files");
    QDir().mkpath(root.filePath("incomplete"));
    put(root.filePath("incomplete/field_m1.csv"), "111\n");
    put(input, head + "&CSVF TMPFILE='incomplete/field_m1.csv' /\n! TEST_CSVF_TWO_MESHES\n");
    check(!runQueued(request).success,
          "SR-03 missing implicit CSVF mesh file cannot complete despite exit zero and SMV");
    for (const QString& enabled : {QStringLiteral("T"), QStringLiteral("TRUE"),
                                   QStringLiteral(".T."), QStringLiteral(".TRUE.")}) {
        put(input, head + "&MISC RESTART=" + enabled + " /\n");
        SimulationTaskManager unsupported;
        const QString id = unsupported.enqueue(request, "Regression", "Explicit restart boundary", 1, &error);
        check(id.isEmpty() && error.contains("RESTART"),
              "SR-02 missing RESTART checkpoints fail explicitly for all Fortran true spellings");
        if (!id.isEmpty()) unsupported.cancel(id);
    }
    const QString restartSource = root.filePath("restart-source");
    QDir().mkpath(restartSource);
    put(QDir(restartSource).filePath("resume_1.restart"), "0.2\n");
    put(QDir(restartSource).filePath("resume.smv"), "TITLE\n Inherited fixture\n");
    put(QDir(restartSource).filePath("resume.out"), "Total Time: 0.2 s\nSTOP: FDS completed successfully\n");
    put(QDir(restartSource).filePath("resume_hrr.csv"), "s,kW\nTime,HRR\n0,0\n0.2,0\n");
    put(QDir(restartSource).filePath("resume_steps.csv"),
        ",,s,s,s\nTime Step,Wall Time,Step Size,Simulation Time,CPU Time\n1,fixture,0.2,0.2,0\n");
    const QString restartInput = root.filePath("resume.fds");
    FdsRunRequest continuation = request;
    continuation.inputFilePath = restartInput;
    continuation.restartSourceDirectory = restartSource;
    const QString restartText = "&HEAD CHID='resume' /\n&MESH IJK=4,4,4 XB=0,1,0,1,0,1 /\n"
                               "&MISC RESTART=.TRUE. /\n&TIME T_END=.4 /\n&TAIL /\n";
    put(restartInput, restartText + "! TEST_ZERO_NO_OUTPUT\n");
    check(!runQueued(continuation).success, "RESTART inherited success marker and exit zero without new output cannot pass");
    put(restartInput, restartText + "! TEST_RESTART_NO_ADVANCE\n");
    check(!runQueued(continuation).success, "RESTART new success marker without new time advancement cannot pass");
    put(restartInput, restartText + "! TEST_RESTART\n");
    const auto resumed = runQueued(continuation);
    check(resumed.success && resumed.restarted && std::abs(resumed.observedEndTime - .4) < 1e-9 &&
          get(QDir(restartSource).filePath("resume_1.restart")) == "0.2\n" &&
          get(resumed.smvFilePath) == get(QDir(restartSource).filePath("resume.smv")),
          "RESTART same CHID inherits unchanged SMV, advances time, and preserves source checkpoint");
    const QString baseline = QDir(resumed.workingDirectory).filePath("_restart_source");
    continuation.inputFilePath = resumed.inputFilePath;
    continuation.restartSnapshotDirectory = baseline;
    put(QDir(resumed.workingDirectory).filePath("resume_1.restart"), "99\n");
    const auto retried = runQueued(continuation);
    check(retried.success && get(retried.outputFilePath).contains("SOURCE_BASELINE=0.2") &&
          get(QDir(baseline).filePath("resume_1.restart")) == "0.2\n",
          "RESTART replay uses immutable baseline instead of mutated previous-run checkpoint");
    put(restartInput, restartText + "! TEST_RESTART\n");
    continuation.inputFilePath = restartInput;
    continuation.restartSnapshotDirectory.clear();
    continuation.restartSourceDirectory = root.filePath("missing-checkpoint");
    const auto missingRestart = runQueued(continuation);
    check(!missingRestart.success && missingRestart.errorMessage.contains("checkpoint"),
          "RESTART missing explicit checkpoint is a preparation failure");
}

bool allTemperatures(const QString& path, int probes, double expected) {
    int rows = 0;
    for (const QString& line : get(path).split('\n')) {
        const QStringList values = line.trimmed().split(',');
        bool numeric = false;
        values.value(0).toDouble(&numeric);
        if (!numeric) continue;
        if (values.size() != probes + 1) return false;
        for (int i = 1; i <= probes; ++i) {
            const double value = values.at(i).toDouble(&numeric);
            if (!numeric || std::abs(value - expected) > 0.01) return false;
        }
        ++rows;
    }
    return rows >= 2;
}

QString hashFile(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? QString::fromLatin1(hash.result().toHex()) : QString{};
}

FdsRunSummary waitTask(SimulationTaskManager& manager, const QString& id) {
    FdsRunSummary result;
    if (id.isEmpty()) return result;
    const auto terminal = [&] {
        const auto* record = manager.task(id);
        if (!record || (record->state != SimulationTaskState::Completed &&
                        record->state != SimulationTaskState::Failed && record->state != SimulationTaskState::Cancelled)) return false;
        result = record->summary;
        if (result.errorMessage.isEmpty()) result.errorMessage = record->errorMessage;
        return true;
    };
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&manager, &SimulationTaskManager::taskUpdated, &loop,
        [&](const QString& updated) { if (updated == id && terminal()) loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20000);
    if (!terminal()) loop.exec();
    if (!terminal()) { manager.cancel(id); result.errorMessage = "Native restart task timed out"; }
    if (!result.success) std::cout << "Native restart diagnostic: " << result.errorMessage.toStdString() << '\n';
    return result;
}

void nativeRestartCompatibility(const QString& executable, const QString& evidenceDirectory) {
    QDir().mkpath(evidenceDirectory);
    QTemporaryDir evidence(QDir(evidenceDirectory).filePath("restart-wrapper-XXXXXX"));
    evidence.setAutoRemove(false);
    check(evidence.isValid(), "Retained native RESTART evidence directory created");
    if (!evidence.isValid()) return;
    std::cout << "Real RESTART evidence: " << evidence.path().toStdString() << '\n';
    for (const FdsRunMode mode : {FdsRunMode::Serial, FdsRunMode::OpenMp, FdsRunMode::Mpi}) {
        const QString label = mode == FdsRunMode::Serial ? "serial" : mode == FdsRunMode::OpenMp ? "openmp" : "mpi";
        const int count = mode == FdsRunMode::Serial ? 1 : 2;
        const QString directory = evidence.filePath(label);
        QDir().mkpath(directory);
        const QString chid = "restart_" + label;
        const QString input = QDir(directory).filePath("case.fds");
        const QString mesh = "&MESH IJK=4,4,4 XB=0,1,0,1,0,1 /\n" +
            (count == 2 ? QString("&MESH IJK=4,4,4 XB=1,2,0,1,0,1 /\n") : QString{});
        const QString probes = "&DEVC ID='Left', XYZ=.5,.5,.5, QUANTITY='TEMPERATURE' /\n" +
            (count == 2 ? QString("&DEVC ID='Right', XYZ=1.5,.5,.5, QUANTITY='TEMPERATURE' /\n") : QString{});
        const auto textFor = [&](const QString& name, bool restart, const QString& source, double end) {
            return QString("&HEAD CHID='%1' /\n").arg(name) + mesh + probes +
                "&SLCF PBZ=.5, QUANTITY='TEMPERATURE' /\n" +
                QString("&TIME T_END=%1, DT=.1 /\n").arg(end, 0, 'f', 1) +
                "&DUMP NFRAMES=4, DT_DEVC=.1, DT_HRR=.1, DT_RESTART=.1 /\n" +
                "&MISC TMPA=40, NOISE=.FALSE." +
                (restart ? QString(", RESTART=.TRUE., RESTART_CHID='%1'").arg(source) : QString{}) + " /\n&TAIL /\n";
        };
        put(input, textFor(chid, false, {}, .2));
        FdsRunRequest request;
        request.inputFilePath = input; request.executablePath = executable;
        request.mode = mode; request.threadCount = 1; request.processCount = 2;
        const auto seed = runQueued(request);
        check(seed.success, "Real RESTART seed completes for Serial/OpenMP/MPI");
        if (!seed.success) continue;
        const QString seedSmvHash = hashFile(seed.smvFilePath);
        const QString seedCheckpoint = QDir(seed.workingDirectory).filePath(chid + "_1.restart");
        const QString seedCheckpointHash = hashFile(seedCheckpoint);
        put(input, textFor(chid, true, chid, .4));
        SimulationTaskManager manager;
        QString error;
        const QString firstId = manager.enqueue(request, "Real restart", label, .4, &error);
        check(!firstId.isEmpty(), "RESTART resolves latest successful same sourceInput and CHID");
        if (firstId.isEmpty()) { std::cout << error.toStdString() << '\n'; continue; }
        const auto resumed = waitTask(manager, firstId);
        check(resumed.success && resumed.restarted && std::abs(resumed.observedEndTime - .4) < 1e-6 &&
              hashFile(resumed.smvFilePath) == seedSmvHash && hashFile(seedCheckpoint) == seedCheckpointHash &&
              allTemperatures(QDir(resumed.workingDirectory).filePath(chid + "_devc.csv"), count, 40),
              "Real same-CHID RESTART advances to 0.4, inherits unchanged SMV, and leaves seed immutable");
        const auto scanned = FdsResultScanner().scanSmvFile(resumed.smvFilePath);
        check(scanned.success() && scanned.fdsInputFilePath == resumed.inputFilePath && scanned.endTime.toDouble() >= .4,
              "RESTART scanner resolves current input and actual advanced result time");
        const QFileInfoList slices = QDir(resumed.workingDirectory).entryInfoList({chid + "*.sf"}, QDir::Files);
        bool advancedSlices = slices.size() == count;
        for (const QFileInfo& slice : slices) {
            const auto data = FdsSliceReader::read(slice.absoluteFilePath());
            advancedSlices = advancedSlices && data.success() && !data.frames.empty() && data.frames.back().time >= .399F;
        }
        check(advancedSlices, "Real RESTART retains and advances inherited SLCF files on every mesh");
        const QString baselinePath = manager.task(firstId)->request.restartSnapshotDirectory;
        const QString baselineHash = hashFile(QDir(baselinePath).filePath(chid + "_1.restart"));
        const QString resultBeforeRetry = hashFile(resumed.outputFilePath);
        const QString retryId = manager.retry(firstId, &error);
        const auto retried = waitTask(manager, retryId);
        const auto* retryRecord = manager.task(retryId);
        check(retried.success && retryRecord && retried.workingDirectory != resumed.workingDirectory &&
              hashFile(QDir(retryRecord->request.restartSnapshotDirectory).filePath(chid + "_1.restart")) == baselineHash &&
              hashFile(resumed.outputFilePath) == resultBeforeRetry &&
              std::abs(retried.observedEndTime - .4) < 1e-6,
              "Real manager Retry reuses immutable 0.2 checkpoint despite prior run advancing its working checkpoint");

        const QString branchSource = QDir(directory).filePath("checkpoint-only");
        QDir().mkpath(branchSource);
        for (int i = 1; i <= count; ++i) {
            const QString name = chid + QStringLiteral("_%1.restart").arg(i);
            QFile::copy(QDir(seed.workingDirectory).filePath(name), QDir(branchSource).filePath(name));
        }
        const QString branchInput = QDir(directory).filePath("branch.fds");
        const QString branchChid = chid + "_branch";
        put(branchInput, textFor(branchChid, true, chid, .4));
        request.inputFilePath = branchInput; request.restartSourceDirectory = branchSource;
        const auto branched = runQueued(request);
        check(branched.success && branched.caseId == branchChid && branched.inheritedOutputBytes == 0 &&
              branched.observedEndTime >= .4 && hashFile(seedCheckpoint) == seedCheckpointHash,
              "Real explicit new CHID restarts from checkpoint-only source and produces independent results");
        const QString missing = QDir(directory).filePath("missing-files");
        QDir().mkpath(missing);
        if (count > 1) QFile::copy(seedCheckpoint, QDir(missing).filePath(chid + "_1.restart"));
        request.restartSourceDirectory = missing;
        const auto missingResult = runQueued(request);
        check(!missingResult.success && missingResult.errorMessage.contains("checkpoint"),
              "Real RESTART request with absent checkpoint member fails before launch");
        const QString missingCsv = QDir(directory).filePath("missing-csv");
        QDir().mkpath(missingCsv);
        for (const QFileInfo& file : QDir(seed.workingDirectory).entryInfoList(QDir::Files)) {
            if (file.fileName() != chid + "_devc.csv")
                QFile::copy(file.absoluteFilePath(), QDir(missingCsv).filePath(file.fileName()));
        }
        request.inputFilePath = input; request.restartSourceDirectory = missingCsv;
        const auto noCsv = runQueued(request);
        check(!noCsv.success && noCsv.errorMessage.contains("_devc.csv"),
              "Real same-CHID RESTART missing SMV-referenced CSV cannot launch as a successful continuation");
        request.inputFilePath = input; request.restartSourceDirectory = seed.workingDirectory;
        put(input, textFor(chid, true, chid, .2));
        const auto noAdvance = runQueued(request);
        check(!noAdvance.success && noAdvance.errorMessage.contains("advance"),
              "Real RESTART no-advance T_END cannot reuse inherited successful result");
    }

    // Regression: exported input A and configured output root B are distinct.
    // Production source discovery must use the root where seed tasks were saved.
    const QString inputRoot = evidence.filePath("split-roots/A");
    const QString outputRoot = evidence.filePath("split-roots/B");
    check(QDir().mkpath(inputRoot) && QDir().mkpath(outputRoot), "RESTART A/B regression directories created");
    FdsRunRequest splitRequest;
    splitRequest.inputFilePath = QDir(inputRoot).filePath("case.fds");
    splitRequest.outputRootDirectory = outputRoot;
    splitRequest.executablePath = executable;
    splitRequest.mode = FdsRunMode::OpenMp;
    splitRequest.threadCount = 1;
    const QString splitCommon = "&HEAD CHID='restart_split' /\n&MESH IJK=4,4,4 XB=0,1,0,1,0,1 /\n"
                                "&DEVC ID='Temperature', XYZ=.5,.5,.5, QUANTITY='TEMPERATURE' /\n"
                                "&DUMP DT_RESTART=.1, DT_DEVC=.1, DT_HRR=.1 /\n";
    put(splitRequest.inputFilePath, splitCommon + "&TIME T_END=.2, DT=.1 /\n&MISC NOISE=.FALSE. /\n&TAIL /\n");
    const auto splitSeed = runQueued(splitRequest);
    const QString splitCheckpoint = QDir(splitSeed.workingDirectory).filePath("restart_split_1.restart");
    const QString splitCheckpointHash = hashFile(splitCheckpoint);
    check(splitSeed.success && splitSeed.workingDirectory.startsWith(outputRoot + "/.firecae-runs/") &&
          !splitCheckpointHash.isEmpty(), "Real input A/output B seed completes and saves checkpoint in configured B");
    if (splitSeed.success) {
        put(splitRequest.inputFilePath, splitCommon +
            "&TIME T_END=.4, DT=.1 /\n&MISC NOISE=.FALSE., RESTART=.TRUE. /\n&TAIL /\n");
        const auto splitResume = runQueued(splitRequest);
        if (!splitResume.success) std::cout << "A/B restart diagnostic: " << splitResume.errorMessage.toStdString() << '\n';
        check(splitResume.success && splitResume.restarted && splitResume.observedEndTime >= .4 &&
              splitResume.workingDirectory.startsWith(outputRoot + "/.firecae-runs/") &&
              splitResume.workingDirectory != splitSeed.workingDirectory &&
              hashFile(splitCheckpoint) == splitCheckpointHash,
              "RESTART auto discovery finds exact A sourceInput and CHID under configured output root B");
        if (splitResume.success) {
            splitRequest.outputRootDirectory = QDir::current().relativeFilePath(outputRoot);
            put(splitRequest.inputFilePath, splitCommon +
                "&TIME T_END=.6, DT=.1 /\n&MISC NOISE=.FALSE., RESTART=.TRUE. /\n&TAIL /\n");
            const auto relativeResume = runQueued(splitRequest);
            check(relativeResume.success && relativeResume.observedEndTime >= .6 &&
                  relativeResume.workingDirectory.startsWith(outputRoot + "/.firecae-runs/") &&
                  hashFile(splitCheckpoint) == splitCheckpointHash,
                  "RESTART relative output root resolves identically to task creation and selects latest successful exact source");
        }
    }
}

// Optional real-runtime entry point, separate from the controlled-process tests.
// Retains every task input, manifest and native FDS output in a fresh directory.
void nativeCompatibility(const QString& executable, const QString& evidenceDirectory) {
    check(QDir().mkpath(evidenceDirectory), "Native evidence parent directory available");
    QTemporaryDir evidence(QDir(evidenceDirectory).filePath("wrapper-XXXXXX"));
    evidence.setAutoRemove(false);
    check(evidence.isValid(), "Fresh retained native evidence directory created");
    if (!evidence.isValid()) return;
    std::cout << "Real FDS wrapper evidence: " << evidence.path().toStdString() << '\n';
    FdsRunRequest request;
    request.executablePath = executable;
    request.mode = FdsRunMode::OpenMp;
    request.threadCount = 1;
    const QString meshes = "&MESH IJK=4,4,4 XB=0,1,0,1,0,1 /\n"
                           "&MESH IJK=4,4,4 XB=1,2,0,1,0,1 /\n";
    const QString probes = "&DEVC ID='Left', XYZ=.5,.5,.5, QUANTITY='TEMPERATURE' /\n"
                           "&DEVC ID='Right', XYZ=1.5,.5,.5, QUANTITY='TEMPERATURE' /\n";
    const QString time = "&TIME T_END=.2 /\n&DUMP NFRAMES=2, DT_DEVC=.1, DT_TMP=.1 /\n";
    const auto prepare = [&](const QString& folder, const QString& text) {
        const QString directory = evidence.filePath(folder);
        QDir().mkpath(directory);
        request.inputFilePath = QDir(directory).filePath("case.fds");
        check(put(request.inputFilePath, text), "Real native input written");
        return directory;
    };
    prepare("producer", "&HEAD CHID='native_pair' /\n" + meshes + probes + time +
                        "&MISC TMPA=60, NOISE=.FALSE. /\n&TAIL /\n");
    const auto producer = runQueued(request);
    check(producer.success && allTemperatures(QDir(producer.workingDirectory)
        .filePath("native_pair_devc.csv"), 2, 60), "Real FDS producer completes at 60 C on two meshes");
    if (!producer.success) return;
    const QString consumer = prepare("consumer", "&HEAD CHID='native_consumer' /\n" + meshes + probes + time +
        "&MISC TMPA=20, NOISE=.FALSE. /\n&CSVF TMPFILE='data/native_pair_tmp_t1_m1.csv' /\n&TAIL /\n");
    QDir().mkpath(QDir(consumer).filePath("data"));
    for (int i = 1; i <= 2; ++i) {
        const QString name = QStringLiteral("native_pair_tmp_t1_m%1.csv").arg(i);
        check(QFile::copy(QDir(producer.workingDirectory).filePath(name),
                          QDir(consumer).filePath("data/" + name)), "Real CSVF mesh input copied from native output");
    }
    const auto consumed = runQueued(request);
    check(consumed.success && allTemperatures(QDir(consumed.workingDirectory)
        .filePath("native_consumer_devc.csv"), 2, 60),
        "Real FireCAE queued CSVF resolves both mesh inputs and retains 60 C despite TMPA 20 C");

    const QString catfDirectory = prepare("catf", "&HEAD CHID='native_catf' /\n"
        "&CATF OTHER_FILES='data/mesh_and_probes.fds' /\n" + time +
        "&MISC NOISE=.FALSE. /\n&TAIL /\n");
    QDir().mkpath(QDir(catfDirectory).filePath("data"));
    put(QDir(catfDirectory).filePath("data/mesh_and_probes.fds"), meshes + probes);
    const auto catf = runQueued(request);
    check(catf.success && catf.caseId == "native_catf_cat" &&
          allTemperatures(QDir(catf.workingDirectory).filePath("native_catf_cat_devc.csv"), 2, 20),
          "Real FireCAE CATF task completes with derived CHID and included temperature probes");

    const QString missing = prepare("missing-sibling", "&HEAD CHID='native_missing' /\n" + meshes + probes + time +
        "&MISC TMPA=20, NOISE=.FALSE. /\n&CSVF TMPFILE='data/native_pair_tmp_t1_m1.csv' /\n&TAIL /\n");
    QDir().mkpath(QDir(missing).filePath("data"));
    QFile::copy(QDir(producer.workingDirectory).filePath("native_pair_tmp_t1_m1.csv"),
                QDir(missing).filePath("data/native_pair_tmp_t1_m1.csv"));
    const auto failed = runQueued(request);
    check(!failed.success && failed.exitCode == 0 && QFileInfo::exists(failed.smvFilePath) &&
          failed.diagnostics.contains("ERROR(440)"),
          "Real CSVF missing sibling is Failed with ERROR440 despite exit zero and existing SMV");
#ifdef Q_OS_WIN
    // Reproduce the SR-06 boundary where the directory is short enough, but
    // the native DEVC/CTRL event-log filename takes its full path over MAX_PATH.
    const QString eventChid = QStringLiteral("fire_protection_controls");
    QString eventDirectory = evidence.filePath(QStringLiteral("event-log-path-"));
    eventDirectory += QString(std::max(0, 219 - static_cast<int>(eventDirectory.size())), 'e');
    const QString eventLog = QDir(eventDirectory).filePath(eventChid + "_devc_ctrl_log.csv");
    const bool eventBoundary = eventDirectory.size() == 219 && eventLog.size() == 262;
    check(eventBoundary && QDir().mkpath(eventDirectory),
          "SR-06 native event-log fixture has a 219-character cwd and 262-character output path");
    if (eventBoundary) {
        FdsRunRequest eventRequest = request;
        eventRequest.inputFilePath = QDir(eventDirectory).filePath("case.fds");
        check(put(eventRequest.inputFilePath,
            "&HEAD CHID='fire_protection_controls' /\n"
            "&MESH IJK=4,4,4, XB=0,1,0,1,0,1 /\n"
            "&TIME T_END=.2, DT=.025 /\n"
            "&DUMP NFRAMES=2, DT_DEVC=.05, DT_HRR=.05 /\n"
            "&MISC NOISE=.FALSE. /\n"
            "&DEVC ID='timer', XYZ=.5,.5,.5, QUANTITY='TIME', SETPOINT=.05 /\n"
            "&CTRL ID='delayed timer', FUNCTION_TYPE='TIME_DELAY', INPUT_ID='timer', DELAY=.05 /\n"
            "&SURF ID='BLOW', VEL=-.1 /\n"
            "&VENT XB=0,1,0,1,0,0, SURF_ID='BLOW', CTRL_ID='delayed timer' /\n"
            "&VENT MB='ZMAX', SURF_ID='OPEN' /\n&TAIL /\n"),
            "SR-06 real native DEVC/CTRL event-log input written");
        const auto eventRun = run(eventRequest);
        std::cout << "Native event-log boundary: cwd=" << eventDirectory.toStdString()
                  << "; event log=" << eventLog.toStdString()
                  << "; summary cwd=" << eventRun.workingDirectory.toStdString() << '\n';
        if (!eventRun.success) std::cout << "Native event-log diagnostic: "
            << eventRun.errorMessage.toStdString() << '\n';
        const auto eventScan = FdsResultScanner().scanSmvFile(eventRun.smvFilePath);
        check(eventRun.success && eventRun.exitCode == 0 && !eventRun.cancelled &&
              eventScan.success() && eventScan.endTime.toDouble() >= .2 &&
              get(eventRun.outputFilePath).contains("FDS completed successfully"),
              "SR-06 real FDS completes 0.2 seconds when its event-log path exceeds MAX_PATH");
        const QString events = get(eventLog);
        check(QFileInfo::exists(eventLog) && events.contains(",DEVC,timer,T") &&
              events.contains(",CTRL,delayed timer,T"),
              "SR-06 long native event log exists and records both device and control activation");
        check(eventRun.workingDirectory == eventDirectory &&
              eventRun.inputFilePath == eventRequest.inputFilePath &&
              eventRun.outputFilePath == QDir(eventDirectory).filePath(eventChid + ".out") &&
              eventRun.smvFilePath == QDir(eventDirectory).filePath(eventChid + ".smv"),
              "SR-06 event-log run summary preserves original working, input and result paths");
    }
    const QString longFolder = QString::fromUtf8("中文运行目录_") + QString(130, 'a') +
                               QLatin1Char('/') + QString(110, 'b');
    prepare(longFolder, "&HEAD CHID='native_long' /\n" + meshes + probes + time +
                        "&MISC NOISE=.FALSE. /\n&TAIL /\n");
    const auto longRun = runQueued(request);
    check(longRun.success && longRun.workingDirectory.size() > 260 &&
          longRun.workingDirectory.contains(QString::fromUtf8("中文运行目录_")) &&
          allTemperatures(QDir(longRun.workingDirectory).filePath("native_long_devc.csv"), 2, 20),
          "SR-06 real FDS launches from Chinese directory over MAX_PATH while summary retains original path");
#endif
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir isolatedPreferences;
    if (!isolatedPreferences.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolatedPreferences.path());
    const QStringList arguments = application.arguments();
    if (arguments.size() == 4 && arguments.at(1) == "--native-restart") {
        nativeRestartCompatibility(arguments.at(2), arguments.at(3));
    } else if (arguments.size() == 4 && arguments.at(1) == "--native-compatibility") {
        nativeCompatibility(arguments.at(2), arguments.at(3));
    } else {
        resultRegressions();
        if (argc == 2) {
            outputDecodingRegressions(arguments.at(1));
            solverRegressions(arguments.at(1));
        }
        else check(false, "Usage: regression executable <controlled fds.exe> OR --native-compatibility <real fds_openmp.exe> <evidence directory>");
    }
    std::cout << "Boundary regression failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
