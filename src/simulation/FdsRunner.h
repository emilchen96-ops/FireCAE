#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QByteArray>
#include <QTemporaryDir>
#include <memory>

#include "simulation/SolverBackend.h"
#include "simulation/FdsOutputTextDecoder.h"

struct FdsProcessJob;

enum class FdsRunMode
{
    Serial,
    OpenMp,
    Mpi,
    Parallel = Mpi
};

struct FdsRunRequest
{
    QString inputFilePath;
    FdsRunMode mode = FdsRunMode::Serial;
    int processCount = 1;
    int threadCount = 1;
    QString executablePath;
    // The task manager freezes input/dependencies in a per-run directory.
    // These paths preserve the caller's origin across retries.
    QString sourceInputFilePath;
    QString outputRootDirectory;
    // Explicit continuation source, and the immutable baseline used by Retry.
    QString restartSourceDirectory;
    QString restartSnapshotDirectory;
    bool restartEnabled = false;
    bool restartAppend = false;
    QString restartSourceChid;
    double restartBaselineTime = -1.0;
    double requestedEndTime = -1.0;
};

struct FdsRunSummary
{
    bool success = false;
    bool cancelled = false;
    int exitCode = -1;
    qint64 elapsedMilliseconds = 0;
    QString inputFilePath;
    QString caseId;
    QString smvFilePath;
    QString outputFilePath;
    QString executablePath;
    QString backendId;
    QString backendDisplayName;
    int processCount = 1;
    int threadCount = 1;
    QString commandLine;
    QString workingDirectory;
    QString standardError;
    QString errorMessage;
    QString diagnostics;
    bool restarted = false;
    double observedEndTime = -1.0;
    qint64 inheritedOutputBytes = 0;
};

class FdsRunner final : public QObject
{
    Q_OBJECT

public:
    explicit FdsRunner(QObject* parent = nullptr);
    ~FdsRunner() override;

    bool isRunning() const;
    qint64 processId() const;
    QString currentInputFile() const;

    bool start(const FdsRunRequest& request, QString* errorMessage = nullptr);
    void stop();
    bool stopAndWait(int timeoutMilliseconds = 5000);

    static QString configuredExecutable();
    static void setConfiguredExecutable(const QString& executablePath);
    static QString detectExecutable();
    static QString validateExecutable(const QString& executablePath);
    static QString probeVersion(const SolverLaunchPlan& launchPlan,
                                QString* errorMessage = nullptr,
                                int timeoutMilliseconds = 5000);
    static QString caseIdFromInput(const QString& inputFilePath);

signals:
    void runStarted(const QString& inputFilePath, qint64 processId);
    void outputReceived(const QString& output);
    void errorOutputReceived(const QString& output);
    void runFinished(const FdsRunSummary& summary);

private:
    void readProcessOutput();
    void readProcessError();
    void appendProcessText(const QString& text, bool standardError);
    void flushProcessText();
    void finishRun(int exitCode, QProcess::ExitStatus exitStatus);
    void resetProcess();

    QProcess* m_process = nullptr;
    std::shared_ptr<FdsProcessJob> m_processJob;
    quint64 m_runGeneration = 0;
    FdsRunRequest m_request;
    QString m_caseId;
    QString m_executablePath;
    SolverBackendInfo m_backendInfo;
    qint64 m_startedAtMilliseconds = 0;
    bool m_stopRequested = false;
    bool m_finishReported = false;
    FdsOutputTextDecoder m_outputDecoder;
    FdsOutputTextDecoder m_errorDecoder;
    QString m_recentOutput;
    QString m_recentError;
    QString m_commandLine;
    QString m_workingDirectory;
    QByteArray m_previousSmvFingerprint;
    QByteArray m_previousOutputFingerprint;
    qint64 m_previousOutputBytes = 0;
    qint64 m_previousStepsBytes = 0;
    QByteArray m_previousOutputHash;
    QByteArray m_previousStepsHash;
    bool m_restartInheritanceVerified = false;
    std::unique_ptr<QTemporaryDir> m_processAliasRoot;
    QString m_processDirectoryAlias;
};
