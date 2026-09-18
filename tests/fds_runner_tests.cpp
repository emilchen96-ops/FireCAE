#include "simulation/FdsRunner.h"
#include "simulation/SimulationTaskManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <algorithm>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

FdsRunSummary runBlocking(const FdsRunRequest& request,
                          int timeoutMilliseconds,
                          int stopAfterStartMilliseconds = -1)
{
    FdsRunner runner;
    FdsRunSummary summary;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&runner, &loop]() {
        runner.stop();
        loop.quit();
    });
    QObject::connect(&runner, &FdsRunner::runStarted,
                     [&runner, stopAfterStartMilliseconds](const QString&, qint64) {
        if (stopAfterStartMilliseconds >= 0) {
            QTimer::singleShot(stopAfterStartMilliseconds, &runner,
                               [&runner]() { runner.stop(); });
        }
    });
    QObject::connect(&runner, &FdsRunner::runFinished,
                     [&summary, &loop](const FdsRunSummary& result) {
        summary = result;
        loop.quit();
    });
    QString error;
    if (!runner.start(request, &error)) {
        summary.errorMessage = error;
        return summary;
    }
    timeout.start(timeoutMilliseconds);
    loop.exec();
    return summary;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir isolatedPreferences;
    if (!isolatedPreferences.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolatedPreferences.path());

    const QString sourceInput =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("solver_smoke.fds"));
    check(FdsRunner::caseIdFromInput(sourceInput) == QStringLiteral("solver_smoke"),
          "FDS CHID is read from the input file");
    check(!FdsRunner::validateExecutable(QStringLiteral("Z:/missing/fds.exe")).isEmpty(),
          "Missing FDS executables are rejected");

    QTemporaryDir runDirectory;
    check(runDirectory.isValid(), "Temporary FDS run directory is available");
    const QString runInput =
        QDir(runDirectory.path()).filePath(QStringLiteral("solver_smoke.fds"));
    check(QFile::copy(sourceInput, runInput), "Smoke-test FDS input is copied");

    SolverLaunchContext backendContext;
    backendContext.inputFilePath = runInput;
    backendContext.fdsExecutablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    backendContext.processCount = 2;
    backendContext.threadCount = 3;
    const QVector<SolverBackendInfo> allBackends =
        SolverBackendRegistry::allBackends(backendContext);
    const QVector<SolverBackendInfo> availableBackends =
        SolverBackendRegistry::availableBackends(backendContext);
    const auto hasBackend = [](const QVector<SolverBackendInfo>& backends,
                               const QString& id) {
        return std::any_of(backends.cbegin(), backends.cend(),
                           [&id](const SolverBackendInfo& backend) {
                               return backend.id == id;
                           });
    };
    check(allBackends.size() == 5 &&
              hasBackend(availableBackends, QStringLiteral("fds.serial.cpu")) &&
              hasBackend(availableBackends, QStringLiteral("fds.openmp.cpu")) &&
              hasBackend(availableBackends, QStringLiteral("fds.mpi.cpu")),
          "Solver registry exposes implemented serial, OpenMP, and MPI CPU backends");
    check(!hasBackend(availableBackends, QStringLiteral("bfds")) &&
              !hasBackend(availableBackends, QStringLiteral("remote.cluster")),
          "Unconfigured BFDS and remote backends are hidden from available choices");
    bool mislabeledGpu = false;
    for (const SolverBackendInfo& backend : allBackends) {
        if (backend.id.startsWith(QStringLiteral("fds.")) &&
            (backend.displayName.contains(QStringLiteral("GPU"), Qt::CaseInsensitive) ||
             backend.computeType.contains(QStringLiteral("GPU"), Qt::CaseInsensitive))) {
            mislabeledGpu = true;
        }
    }
    check(!mislabeledGpu, "Native FDS serial/MPI backends are never mislabeled as GPU");
    const SolverLaunchPlan serialPlan = SolverBackendRegistry::create(
        SolverBackendKind::FdsSerialCpu)->createLaunchPlan(backendContext);
    check(serialPlan.valid() && QFileInfo(serialPlan.program).fileName().compare(
              QStringLiteral("fds_openmp.exe"), Qt::CaseInsensitive) == 0 &&
              serialPlan.environment.value(QStringLiteral("OMP_NUM_THREADS")) ==
                  QStringLiteral("1") &&
              serialPlan.environment.value(QStringLiteral("I_MPI_ROOT")).isEmpty() &&
              serialPlan.commandLine.contains(QStringLiteral("fds_openmp.exe"),
                                               Qt::CaseInsensitive),
          "Serial CPU backend launches non-MPI fds_openmp.exe with one thread");
    QString versionProbeError;
    check(FdsRunner::probeVersion(serialPlan, &versionProbeError) ==
              QStringLiteral("6.11.1") &&
              versionProbeError.isEmpty(),
          "Bundled FDS reports the 6.11.1 version used by the project Schema");
    const SolverLaunchPlan openMpPlan = SolverBackendRegistry::create(
        SolverBackendKind::FdsOpenMpCpu)->createLaunchPlan(backendContext);
    check(openMpPlan.valid() && QFileInfo(openMpPlan.program).fileName().compare(
              QStringLiteral("fds_openmp.exe"), Qt::CaseInsensitive) == 0 &&
              openMpPlan.environment.value(QStringLiteral("OMP_NUM_THREADS")) ==
                  QStringLiteral("3") &&
              openMpPlan.commandLine.contains(QStringLiteral("fds_openmp.exe"),
                                               Qt::CaseInsensitive),
          "OpenMP CPU backend launches fds_openmp.exe with the requested threads");
    const SolverLaunchPlan mpiPlan = SolverBackendRegistry::create(
        SolverBackendKind::FdsMpiCpu)->createLaunchPlan(backendContext);
    check(mpiPlan.valid() && QFileInfo(mpiPlan.program).fileName().compare(
              QStringLiteral("mpiexec.exe"), Qt::CaseInsensitive) == 0 &&
              mpiPlan.arguments.contains(QStringLiteral("2")),
          "MPI backend creates an explicit two-process CPU launch plan");

    FdsRunner runner;
    QString processOutput;
    FdsRunSummary summary;
    bool finished = false;
    bool timedOut = false;
    QEventLoop loop;
    QObject::connect(&runner, &FdsRunner::outputReceived,
                     [&processOutput](const QString& output) {
                         processOutput.append(output);
                     });
    QObject::connect(&runner, &FdsRunner::errorOutputReceived,
                     [&processOutput](const QString& output) {
                         processOutput.append(output);
                     });
    QObject::connect(&runner, &FdsRunner::runFinished,
                     [&summary, &finished, &loop](const FdsRunSummary& result) {
                         summary = result;
                         finished = true;
                         loop.quit();
                     });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&runner, &timedOut]() {
        timedOut = true;
        runner.stop();
    });

    FdsRunRequest request;
    request.inputFilePath = runInput;
    request.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    QString startError;
    const bool started = runner.start(request, &startError);
    check(started, "Bundled FDS calculation starts");
    if (!started) {
        std::cerr << startError.toStdString() << '\n';
    } else {
        timeout.start(120000);
        loop.exec();
    }

    check(!timedOut, "Bundled FDS smoke test completes within 120 seconds");
    check(finished, "FDS runner reports completion");
    check(summary.success, "FDS runner reports a successful calculation");
    check(summary.exitCode == 0, "FDS exits with code zero");
    check(summary.backendId == QStringLiteral("fds.serial.cpu") &&
              summary.backendDisplayName.contains(QStringLiteral("CPU")) &&
              summary.processCount == 1,
          "Run summary records the concrete CPU backend and process count");
    check(QFileInfo(summary.executablePath).fileName().compare(
              QStringLiteral("fds_openmp.exe"), Qt::CaseInsensitive) == 0 &&
              summary.threadCount == 1 && !summary.commandLine.isEmpty() &&
              summary.workingDirectory == QFileInfo(runInput).absolutePath(),
          "Serial runs remain single-threaded and retain command/work-directory evidence");
    check(QFileInfo::exists(summary.smvFilePath), "FDS creates a Smokeview result file");
    check(QFileInfo::exists(summary.outputFilePath), "FDS creates a text output file");
    check(processOutput.contains(QStringLiteral("Fire Dynamics Simulator"),
                                 Qt::CaseInsensitive),
          "FDS console output is captured");

    QTemporaryDir openMpDirectory;
    const QString openMpInput = QDir(openMpDirectory.path())
                                    .filePath(QStringLiteral("solver_smoke.fds"));
    check(QFile::copy(sourceInput, openMpInput), "OpenMP smoke-test input is prepared");
    FdsRunRequest openMpRequest;
    openMpRequest.inputFilePath = openMpInput;
    openMpRequest.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    openMpRequest.mode = FdsRunMode::OpenMp;
    openMpRequest.threadCount = 2;
    const FdsRunSummary openMpSummary = runBlocking(openMpRequest, 120000);
    check(openMpSummary.success &&
              openMpSummary.backendId == QStringLiteral("fds.openmp.cpu") &&
              openMpSummary.threadCount == 2 &&
              QFileInfo(openMpSummary.executablePath).fileName().compare(
                  QStringLiteral("fds_openmp.exe"), Qt::CaseInsensitive) == 0 &&
              QFileInfo::exists(openMpSummary.smvFilePath),
          "Two-thread CPU/OpenMP calculation completes with its explicit backend");

    QTemporaryDir mpiDirectory;
    const QString mpiSource = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                  .filePath(QStringLiteral("solver_mpi8.fds"));
    const QString mpiInput = QDir(mpiDirectory.path())
                                 .filePath(QStringLiteral("solver_mpi8.fds"));
    check(QFile::copy(mpiSource, mpiInput), "Eight-mesh MPI input is prepared");
    FdsRunRequest mpiRequest;
    mpiRequest.inputFilePath = mpiInput;
    mpiRequest.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    mpiRequest.mode = FdsRunMode::Mpi;
    mpiRequest.processCount = 8;
    const FdsRunSummary mpiSummary = runBlocking(mpiRequest, 120000);
    check(mpiSummary.success &&
              mpiSummary.backendId == QStringLiteral("fds.mpi.cpu") &&
              mpiSummary.processCount == 8 &&
              QFileInfo::exists(mpiSummary.smvFilePath),
          "Eight adjacent meshes complete through eight-process CPU/MPI");

    QTemporaryDir stopDirectory;
    const QString stopSource = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                   .filePath(QStringLiteral("solver_stop.fds"));
    const QString stopInput = QDir(stopDirectory.path())
                                  .filePath(QStringLiteral("solver_stop.fds"));
    check(QFile::copy(stopSource, stopInput), "Active-stop input is prepared");
    FdsRunRequest stopRequest;
    stopRequest.inputFilePath = stopInput;
    stopRequest.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    const FdsRunSummary stopSummary = runBlocking(stopRequest, 30000, 150);
    check(stopSummary.cancelled && !stopSummary.success,
          "An active Serial solver process can be stopped and is reported as cancelled");

#ifdef Q_OS_WIN
    qint64 cleanupProcessId = 0;
    HANDLE cleanupProcessHandle = nullptr;
    DWORD openProcessError = ERROR_SUCCESS;
    {
        FdsRunner cleanupRunner;
        QEventLoop startupLoop;
        QObject::connect(&cleanupRunner, &FdsRunner::runStarted,
                         [&cleanupProcessId, &cleanupProcessHandle, &openProcessError,
                          &startupLoop](const QString&, qint64 pid) {
                             cleanupProcessId = pid;
                             cleanupProcessHandle = OpenProcess(
                                 SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, static_cast<DWORD>(pid));
                             if (!cleanupProcessHandle) openProcessError = GetLastError();
                             QTimer::singleShot(150, &startupLoop, &QEventLoop::quit);
                         });
        QString cleanupError;
        if (cleanupRunner.start(stopRequest, &cleanupError)) {
            QTimer startupTimeout;
            startupTimeout.setSingleShot(true);
            QObject::connect(&startupTimeout, &QTimer::timeout,
                             &startupLoop, &QEventLoop::quit);
            startupTimeout.start(5000);
            startupLoop.exec();
        } else {
            std::cerr << "Cleanup test solver failed to start: " << cleanupError.toStdString() << '\n';
        }
    }
    if (!cleanupProcessHandle) {
        std::cerr << "OpenProcess for actual solver PID " << cleanupProcessId
                  << " failed; Win32 error=" << openProcessError << '\n';
    }
    check(cleanupProcessId > 0 && cleanupProcessHandle != nullptr,
          "Destructor test holds a synchronization handle for the actual Serial solver process");
    bool cleanupConfirmed = false;
    if (cleanupProcessHandle) {
        const DWORD waitResult = WaitForSingleObject(cleanupProcessHandle, 5000);
        cleanupConfirmed = waitResult == WAIT_OBJECT_0;
        if (!cleanupConfirmed) {
            const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
            std::cerr << "WaitForSingleObject for solver PID " << cleanupProcessId
                      << " did not confirm termination; result=" << waitResult
                      << ", Win32 error=" << waitError << '\n';
        }
        if (!CloseHandle(cleanupProcessHandle)) {
            std::cerr << "CloseHandle for solver PID " << cleanupProcessId
                      << " failed; Win32 error=" << GetLastError() << '\n';
            check(false, "Destructor test closes its solver process handle");
        }
    }
    check(cleanupConfirmed,
          "Destroying an active FDS runner terminates the actual Serial solver process");
#endif

    QTemporaryDir failureDirectory;
    const QString failureSource = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                      .filePath(QStringLiteral("solver_failure.fds"));
    const QString failureInput = QDir(failureDirectory.path())
                                     .filePath(QStringLiteral("solver_failure.fds"));
    check(QFile::copy(failureSource, failureInput), "Expected-failure input is prepared");
    FdsRunRequest failureRequest;
    failureRequest.inputFilePath = failureInput;
    failureRequest.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    const FdsRunSummary failureSummary = runBlocking(failureRequest, 30000);
    check(!failureSummary.success && !failureSummary.errorMessage.isEmpty(),
          "Solver input failures reach a terminal failed summary with an explanation");
    check(!failureSummary.diagnostics.isEmpty() &&
              failureSummary.errorMessage.contains(QStringLiteral("FDS diagnostics")),
          "FDS .out diagnostics are extracted into the failed task summary");

    QTemporaryDir queueRoot;
    const QString firstDirectory = QDir(queueRoot.path()).filePath(QStringLiteral("first"));
    const QString secondDirectory = QDir(queueRoot.path()).filePath(QStringLiteral("second"));
    QDir().mkpath(firstDirectory);
    QDir().mkpath(secondDirectory);
    const QString firstInput = QDir(firstDirectory).filePath(QStringLiteral("solver_smoke.fds"));
    const QString secondInput = QDir(secondDirectory).filePath(QStringLiteral("solver_smoke.fds"));
    check(QFile::copy(sourceInput, firstInput) && QFile::copy(sourceInput, secondInput),
          "Queued task inputs are prepared in independent working directories");
    SimulationTaskManager manager;
    manager.setMaximumConcurrentTasks(1);
    FdsRunRequest firstRequest;
    firstRequest.inputFilePath = firstInput;
    firstRequest.executablePath = QStringLiteral(FIRECAE_TEST_FDS_EXECUTABLE);
    FdsRunRequest secondRequest = firstRequest;
    secondRequest.inputFilePath = secondInput;
    QString queueError;
    const QString firstTaskId = manager.enqueue(
        firstRequest, QStringLiteral("Queue Test"), QStringLiteral("Base"), 0.1,
        &queueError);
    const QString secondTaskId = manager.enqueue(
        secondRequest, QStringLiteral("Queue Test"), QStringLiteral("Variant"), 0.1,
        &queueError);
    check(!firstTaskId.isEmpty() && !secondTaskId.isEmpty() &&
              manager.tasks().size() == 2,
          "Simulation task center queues multiple independent calculations");
    check(manager.cancel(secondTaskId) &&
              manager.task(secondTaskId)->state == SimulationTaskState::Cancelled,
          "A waiting simulation task can be cancelled without launching a process");
    QEventLoop queueLoop;
    bool firstTaskFinished = false;
    QObject::connect(&manager, &SimulationTaskManager::taskFinished,
                     [&queueLoop, &firstTaskFinished, firstTaskId](
                         const QString& taskId, const FdsRunSummary&) {
                         if (taskId == firstTaskId) {
                             firstTaskFinished = true;
                             queueLoop.quit();
                         }
                     });
    QTimer queueTimeout;
    queueTimeout.setSingleShot(true);
    QObject::connect(&queueTimeout, &QTimer::timeout, &queueLoop, &QEventLoop::quit);
    queueTimeout.start(120000);
    queueLoop.exec();
    check(firstTaskFinished &&
              manager.task(firstTaskId)->state == SimulationTaskState::Completed &&
              !manager.task(firstTaskId)->commandLine.isEmpty() &&
              !manager.task(firstTaskId)->environmentCheck.isEmpty() &&
              (!manager.task(firstTaskId)->standardOutput.isEmpty() ||
               !manager.task(firstTaskId)->standardError.isEmpty()),
          "Queued task completes with command, environment, stdout/stderr evidence");
    const QString retryTaskId = manager.retry(secondTaskId, &queueError);
    check(!retryTaskId.isEmpty() && manager.tasks().size() == 3,
          "Cancelled simulation tasks can be enqueued again with Retry");
    QEventLoop retryLoop;
    bool retryFinished = false;
    QObject::connect(&manager, &SimulationTaskManager::taskFinished,
                     [&retryLoop, &retryFinished, retryTaskId](
                         const QString& taskId, const FdsRunSummary&) {
                         if (taskId == retryTaskId) {
                             retryFinished = true;
                             retryLoop.quit();
                         }
                     });
    QTimer retryTimeout;
    retryTimeout.setSingleShot(true);
    QObject::connect(&retryTimeout, &QTimer::timeout, &retryLoop, &QEventLoop::quit);
    retryTimeout.start(120000);
    retryLoop.exec();
    check(retryFinished &&
              manager.task(retryTaskId)->state == SimulationTaskState::Completed &&
              manager.task(retryTaskId)->summary.backendId ==
                  QStringLiteral("fds.serial.cpu"),
          "Retried task completes through the concrete serial CPU backend");

    if (failures == 0) {
        std::cout << "FireCAE FDS runner tests passed.\n";
        return 0;
    }
    return 1;
}
