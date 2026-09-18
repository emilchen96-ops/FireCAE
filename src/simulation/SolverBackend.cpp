#include "simulation/SolverBackend.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace
{
constexpr auto organizationName = "FireCAE";
constexpr auto applicationName = "FireCAE";
constexpr auto bfdsSettingsKey = "applications/bfdsExecutable";
constexpr auto mpiSettingsKey = "applications/mpiExecutable";

QString executableProblem(const QString& path, const QStringList& acceptedNames)
{
    if (path.trimmed().isEmpty()) return QStringLiteral("Executable is not configured.");
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return QStringLiteral("Executable does not exist: %1")
            .arg(QDir::toNativeSeparators(path));
    }
    if (!acceptedNames.isEmpty() &&
        !acceptedNames.contains(info.fileName(), Qt::CaseInsensitive)) {
        return QStringLiteral("Unexpected executable name: %1").arg(info.fileName());
    }
    return {};
}

QProcessEnvironment localEnvironment(const QFileInfo& executable,
                                     int openMpThreads = 1,
                                     bool includeMpiRuntime = false)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QDir binDirectory = executable.absoluteDir();
    const QString mpiDirectory = binDirectory.filePath(QStringLiteral("mpi"));
    // Intel's fds_openmp.exe is linked against impi.dll even though it does
    // not launch MPI ranks, so the bundled MPI directory must remain on PATH.
    // I_MPI_ROOT is still reserved for an explicit MPI run below.
    QStringList pathParts = {QDir::toNativeSeparators(mpiDirectory),
                             QDir::toNativeSeparators(binDirectory.absolutePath())};
    const QString existingPath = environment.value(QStringLiteral("PATH"));
    if (!existingPath.isEmpty()) pathParts.append(existingPath);
    environment.insert(QStringLiteral("PATH"), pathParts.join(QDir::listSeparator()));
    if (includeMpiRuntime) {
        environment.insert(QStringLiteral("I_MPI_ROOT"),
                           QDir::toNativeSeparators(mpiDirectory));
    } else {
        environment.remove(QStringLiteral("I_MPI_ROOT"));
    }
    environment.insert(QStringLiteral("OMP_NUM_THREADS"),
                       QString::number(qMax(1, openMpThreads)));
    return environment;
}

QFileInfo executableVariant(const QString& configured, const QString& fileName)
{
    const QFileInfo source(configured);
    if (source.fileName().compare(fileName, Qt::CaseInsensitive) == 0)
        return source;
    return QFileInfo(source.absoluteDir().filePath(fileName));
}

QFileInfo serialExecutableVariant(const QString& configured)
{
    // The MPI-enabled fds.exe initializes network communication even for a
    // one-process run and can therefore trigger a Windows Firewall prompt.
    // Prefer the non-MPI OpenMP binary with one thread for serial calculations;
    // fall back to fds.exe only for older installations without that binary.
    const QFileInfo openMp = executableVariant(
        configured, QStringLiteral("fds_openmp.exe"));
    if (openMp.exists() && openMp.isFile()) return openMp;
    return executableVariant(configured, QStringLiteral("fds.exe"));
}

QString mpiLauncherFor(const QFileInfo& fdsExecutable)
{
    const QString configured = SolverBackendRegistry::configuredMpiExecutable();
    if (!configured.trimmed().isEmpty()) return QFileInfo(configured).absoluteFilePath();
    return fdsExecutable.absoluteDir().filePath(QStringLiteral("mpi/mpiexec.exe"));
}

void finalizePlan(SolverLaunchPlan& plan)
{
    QStringList parts{SolverBackendRegistry::quoteCommandArgument(plan.program)};
    for (const QString& argument : plan.arguments)
        parts.append(SolverBackendRegistry::quoteCommandArgument(argument));
    plan.commandLine = parts.join(QLatin1Char(' '));
}

class FdsSerialBackend final : public SolverBackend
{
public:
    SolverBackendInfo info(const SolverLaunchContext& context) const override
    {
        const QString problem = executableProblem(
            serialExecutableVariant(context.fdsExecutablePath).absoluteFilePath(),
            {QStringLiteral("fds_openmp.exe"), QStringLiteral("fds.exe")});
        return {SolverBackendKind::FdsSerialCpu, QStringLiteral("fds.serial.cpu"),
                QStringLiteral("Native FDS — Serial CPU"), QStringLiteral("CPU"),
                problem.isEmpty(), problem};
    }

    SolverLaunchPlan createLaunchPlan(const SolverLaunchContext& context) const override
    {
        SolverLaunchPlan plan;
        const SolverBackendInfo backend = info(context);
        if (!backend.available) { plan.errorMessage = backend.unavailableReason; return plan; }
        const QFileInfo executable = serialExecutableVariant(
            context.fdsExecutablePath);
        const QFileInfo input(context.inputFilePath);
        plan.program = executable.absoluteFilePath();
        plan.arguments = {input.fileName()};
        plan.workingDirectory = input.absolutePath();
        plan.environment = localEnvironment(executable, 1, false);
        plan.solverExecutable = executable.absoluteFilePath();
        finalizePlan(plan);
        return plan;
    }
};

class FdsOpenMpBackend final : public SolverBackend
{
public:
    SolverBackendInfo info(const SolverLaunchContext& context) const override
    {
        const QString problem = executableProblem(
            executableVariant(context.fdsExecutablePath,
                              QStringLiteral("fds_openmp.exe")).absoluteFilePath(),
            {QStringLiteral("fds_openmp.exe")});
        return {SolverBackendKind::FdsOpenMpCpu,
                QStringLiteral("fds.openmp.cpu"),
                QStringLiteral("Native FDS — OpenMP CPU"),
                QStringLiteral("CPU parallel (shared-memory OpenMP)"),
                problem.isEmpty(), problem};
    }

    SolverLaunchPlan createLaunchPlan(const SolverLaunchContext& context) const override
    {
        SolverLaunchPlan plan;
        const SolverBackendInfo backend = info(context);
        if (!backend.available) {
            plan.errorMessage = backend.unavailableReason;
            return plan;
        }
        const QFileInfo executable = executableVariant(
            context.fdsExecutablePath, QStringLiteral("fds_openmp.exe"));
        const QFileInfo input(context.inputFilePath);
        plan.program = executable.absoluteFilePath();
        plan.arguments = {input.fileName()};
        plan.workingDirectory = input.absolutePath();
        plan.environment = localEnvironment(executable,
                                             qMax(1, context.threadCount), false);
        plan.solverExecutable = executable.absoluteFilePath();
        finalizePlan(plan);
        return plan;
    }
};

class FdsMpiBackend final : public SolverBackend
{
public:
    SolverBackendInfo info(const SolverLaunchContext& context) const override
    {
        const QFileInfo executable = executableVariant(
            context.fdsExecutablePath, QStringLiteral("fds.exe"));
        QString problem = executableProblem(executable.absoluteFilePath(),
                                            {QStringLiteral("fds.exe")});
        if (problem.isEmpty()) {
            const QString mpi = mpiLauncherFor(executable);
            problem = executableProblem(mpi, {QStringLiteral("mpiexec.exe")});
            if (!problem.isEmpty()) {
                problem = SolverBackendRegistry::configuredMpiExecutable().trimmed().isEmpty()
                              ? QStringLiteral("Bundled MPI launcher is unavailable: ") + problem
                              : QStringLiteral("Configured MPI launcher is unavailable: ") + problem;
            }
        }
        return {SolverBackendKind::FdsMpiCpu, QStringLiteral("fds.mpi.cpu"),
                QStringLiteral("Native FDS — MPI CPU"), QStringLiteral("CPU parallel (MPI)"),
                problem.isEmpty(), problem};
    }

    SolverLaunchPlan createLaunchPlan(const SolverLaunchContext& context) const override
    {
        SolverLaunchPlan plan;
        const SolverBackendInfo backend = info(context);
        if (!backend.available) { plan.errorMessage = backend.unavailableReason; return plan; }
        const QFileInfo executable = executableVariant(
            context.fdsExecutablePath, QStringLiteral("fds.exe"));
        const QFileInfo input(context.inputFilePath);
        plan.program = mpiLauncherFor(executable);
        plan.arguments = {QStringLiteral("-localonly"), QStringLiteral("-n"),
                          QString::number(qMax(2, context.processCount)),
                          executable.absoluteFilePath(), input.fileName()};
        plan.workingDirectory = input.absolutePath();
        plan.environment = localEnvironment(executable, 1, true);
        const QString launcherDirectory = QFileInfo(plan.program).absolutePath();
        plan.environment.insert(
            QStringLiteral("PATH"),
            QDir::toNativeSeparators(launcherDirectory) + QDir::listSeparator() +
                plan.environment.value(QStringLiteral("PATH")));
        plan.solverExecutable = executable.absoluteFilePath();
        finalizePlan(plan);
        return plan;
    }
};

class BfdsBackend final : public SolverBackend
{
public:
    SolverBackendInfo info(const SolverLaunchContext& context) const override
    {
        const QString problem = executableProblem(
            context.bfdsExecutablePath, {QStringLiteral("bfds.exe")});
        return {SolverBackendKind::Bfds, QStringLiteral("bfds"),
                QStringLiteral("BFDS"), QStringLiteral("As reported by installed BFDS runtime"),
                problem.isEmpty(), problem};
    }

    SolverLaunchPlan createLaunchPlan(const SolverLaunchContext& context) const override
    {
        SolverLaunchPlan plan;
        const SolverBackendInfo backend = info(context);
        if (!backend.available) { plan.errorMessage = backend.unavailableReason; return plan; }
        const QFileInfo executable(context.bfdsExecutablePath);
        const QFileInfo input(context.inputFilePath);
        plan.program = executable.absoluteFilePath();
        plan.arguments = {input.fileName()};
        plan.workingDirectory = input.absolutePath();
        plan.environment = localEnvironment(executable, 1);
        plan.solverExecutable = executable.absoluteFilePath();
        finalizePlan(plan);
        return plan;
    }
};

class RemoteClusterBackend final : public SolverBackend
{
public:
    SolverBackendInfo info(const SolverLaunchContext&) const override
    {
        return {SolverBackendKind::RemoteCluster, QStringLiteral("remote.cluster"),
                QStringLiteral("Remote Cluster"), QStringLiteral("CPU/MPI"), false,
                QStringLiteral("No remote SSH cluster profile is configured.")};
    }

    SolverLaunchPlan createLaunchPlan(const SolverLaunchContext& context) const override
    {
        SolverLaunchPlan plan;
        plan.errorMessage = info(context).unavailableReason;
        return plan;
    }
};
}

std::unique_ptr<SolverBackend> SolverBackendRegistry::create(SolverBackendKind kind)
{
    switch (kind) {
    case SolverBackendKind::FdsSerialCpu: return std::make_unique<FdsSerialBackend>();
    case SolverBackendKind::FdsOpenMpCpu: return std::make_unique<FdsOpenMpBackend>();
    case SolverBackendKind::FdsMpiCpu: return std::make_unique<FdsMpiBackend>();
    case SolverBackendKind::Bfds: return std::make_unique<BfdsBackend>();
    case SolverBackendKind::RemoteCluster: return std::make_unique<RemoteClusterBackend>();
    }
    return {};
}

QVector<SolverBackendInfo> SolverBackendRegistry::allBackends(
    const SolverLaunchContext& context)
{
    QVector<SolverBackendInfo> result;
    for (const SolverBackendKind kind : {SolverBackendKind::FdsSerialCpu,
                                         SolverBackendKind::FdsOpenMpCpu,
                                         SolverBackendKind::FdsMpiCpu,
                                         SolverBackendKind::Bfds,
                                         SolverBackendKind::RemoteCluster}) {
        result.append(create(kind)->info(context));
    }
    return result;
}

QString SolverBackendRegistry::quoteCommandArgument(const QString& argument)
{
    if (argument.isEmpty()) return QStringLiteral("\"\"");
    if (!argument.contains(QRegularExpression(QStringLiteral(R"([\s\"])") )))
        return argument;
    QString escaped = argument;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QVector<SolverBackendInfo> SolverBackendRegistry::availableBackends(
    const SolverLaunchContext& context)
{
    QVector<SolverBackendInfo> result;
    for (const SolverBackendInfo& backend : allBackends(context)) {
        if (backend.available) result.append(backend);
    }
    return result;
}

QString SolverBackendRegistry::configuredBfdsExecutable()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
                     QString::fromLatin1(applicationName))
        .value(QString::fromLatin1(bfdsSettingsKey)).toString();
}

void SolverBackendRegistry::setConfiguredBfdsExecutable(const QString& path)
{
    QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
              QString::fromLatin1(applicationName))
        .setValue(QString::fromLatin1(bfdsSettingsKey),
                  path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath());
}

QString SolverBackendRegistry::configuredMpiExecutable()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
                     QString::fromLatin1(applicationName))
        .value(QString::fromLatin1(mpiSettingsKey)).toString();
}

void SolverBackendRegistry::setConfiguredMpiExecutable(const QString& path)
{
    QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
              QString::fromLatin1(applicationName))
        .setValue(QString::fromLatin1(mpiSettingsKey),
                  path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath());
}
