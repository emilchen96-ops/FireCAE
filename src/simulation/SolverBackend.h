#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

enum class SolverBackendKind
{
    FdsSerialCpu,
    FdsOpenMpCpu,
    FdsMpiCpu,
    Bfds,
    RemoteCluster
};

struct SolverBackendInfo
{
    SolverBackendKind kind = SolverBackendKind::FdsSerialCpu;
    QString id;
    QString displayName;
    QString computeType;
    bool available = false;
    QString unavailableReason;
};

struct SolverLaunchContext
{
    QString inputFilePath;
    QString fdsExecutablePath;
    QString bfdsExecutablePath;
    int processCount = 1;
    int threadCount = 1;
};

struct SolverLaunchPlan
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
    QString solverExecutable;
    QString commandLine;
    QString errorMessage;

    bool valid() const { return errorMessage.isEmpty() && !program.isEmpty(); }
};

class SolverBackend
{
public:
    virtual ~SolverBackend() = default;
    virtual SolverBackendInfo info(const SolverLaunchContext& context) const = 0;
    virtual SolverLaunchPlan createLaunchPlan(
        const SolverLaunchContext& context) const = 0;
};

class SolverBackendRegistry
{
public:
    static std::unique_ptr<SolverBackend> create(SolverBackendKind kind);
    static QVector<SolverBackendInfo> allBackends(
        const SolverLaunchContext& context);
    static QVector<SolverBackendInfo> availableBackends(
        const SolverLaunchContext& context);
    static QString quoteCommandArgument(const QString& argument);

    static QString configuredBfdsExecutable();
    static void setConfiguredBfdsExecutable(const QString& path);
    static QString configuredMpiExecutable();
    static void setConfiguredMpiExecutable(const QString& path);
};
