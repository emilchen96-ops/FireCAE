#include "simulation/FdsRunner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <string>
#include <cmath>
#include <cstddef>
#include <cstring>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#endif

struct FdsProcessJob
{
#ifdef Q_OS_WIN
    HANDLE handle = nullptr;
    ~FdsProcessJob() { if (handle) CloseHandle(handle); }
#endif
};

namespace
{
constexpr auto organizationName = "FireCAE";
constexpr auto applicationName = "FireCAE";
constexpr auto fdsSettingsKey = "applications/fdsExecutable";

QString normalizedExecutable(const QString& path)
{
    return path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}

QString outputPathForCase(const QFileInfo& inputInfo,
                          const QString& caseId,
                          const QString& suffix)
{
    return inputInfo.absoluteDir().filePath(caseId + suffix);
}

QByteArray fileFingerprint(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    const QFileInfo info(path);
    return hash.result() + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
}

QByteArray contentHash(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result() : QByteArray{};
}

QByteArray appendedBytes(const QString& path, qint64 offset, const QByteArray& originalHash, bool* valid)
{
    QFile file(path);
    *valid = file.open(QIODevice::ReadOnly) && file.size() >= offset;
    if (!*valid) return {};
    if (offset > 0) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = offset;
        while (remaining > 0) {
            const QByteArray chunk = file.read(qMin<qint64>(remaining, 1024 * 1024));
            if (chunk.isEmpty()) { *valid = false; return {}; }
            hash.addData(chunk); remaining -= chunk.size();
        }
        if (hash.result() != originalHash) { *valid = false; return {}; }
    }
    return file.readAll();
}

QString processWorkingDirectory(const QString& directory, qsizetype fileNameCharacters, QString* error)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(directory);
    QString extended = native;
    if (!extended.startsWith(QStringLiteral("\\\\?\\")))
        extended = extended.startsWith(QStringLiteral("\\\\"))
            ? QStringLiteral("\\\\?\\UNC\\") + extended.mid(2)
            : QStringLiteral("\\\\?\\") + extended;
    const DWORD needed = GetShortPathNameW(
        reinterpret_cast<LPCWSTR>(extended.utf16()), nullptr, 0);
    if (needed > 0) {
        std::wstring buffer(needed, L'\0');
        const DWORD written = GetShortPathNameW(
            reinterpret_cast<LPCWSTR>(extended.utf16()), buffer.data(), needed);
        if (written > 0 && written < needed) {
            QString shortened = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
            if (shortened.startsWith(QStringLiteral("\\\\?\\UNC\\")))
                shortened = QStringLiteral("\\\\") + shortened.mid(8);
            else if (shortened.startsWith(QStringLiteral("\\\\?\\")))
                shortened = shortened.mid(4);
            if (shortened.size() + 1 + fileNameCharacters < MAX_PATH) return shortened;
        }
    }
    if (native.size() + 1 + fileNameCharacters >= MAX_PATH) {
        if (error) *error = QStringLiteral(
            "The Windows solver input or output path would exceed 259 characters and no usable short path is available. "
            "Choose a shorter output directory: %1").arg(directory);
        return {};
    }
#else
    Q_UNUSED(fileNameCharacters);
    Q_UNUSED(error);
#endif
    return directory;
}

#ifdef Q_OS_WIN
bool jobIsEmpty(const std::shared_ptr<FdsProcessJob>& job)
{
    if (!job || !job->handle) return true;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    return QueryInformationJobObject(job->handle, JobObjectBasicAccountingInformation,
        &info, sizeof(info), nullptr) && info.ActiveProcesses == 0;
}

bool waitForJobEmpty(const std::shared_ptr<FdsProcessJob>& job, int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (!jobIsEmpty(job)) {
        if (timer.elapsed() >= qMax(0, timeoutMilliseconds)) return false;
        Sleep(10);
    }
    return true;
}

struct ProcessJobStartup
{
    std::shared_ptr<FdsProcessJob> job;
    STARTUPINFOEXW info{};
    QByteArray attributes;
    bool initialized = false;
    ~ProcessJobStartup() {
        if (initialized) DeleteProcThreadAttributeList(info.lpAttributeList);
    }

    bool initialize()
    {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) return false;
        attributes.resize(static_cast<qsizetype>(bytes));
        info.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        initialized = InitializeProcThreadAttributeList(info.lpAttributeList, 1, 0, &bytes);
        return initialized && UpdateProcThreadAttribute(info.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_JOB_LIST, &job->handle, sizeof(job->handle), nullptr, nullptr);
    }
};

bool createDirectoryJunction(const QString& link, const QString& target, QString* error)
{
    const QString nativeLink = QDir::toNativeSeparators(link);
    const QString nativeTarget = QDir::toNativeSeparators(target);
    QString substitute = nativeTarget.startsWith(QStringLiteral("\\\\"))
        ? QStringLiteral("\\??\\UNC\\") + nativeTarget.mid(2)
        : QStringLiteral("\\??\\") + nativeTarget;
    struct JunctionData {
        DWORD tag; WORD dataLength; WORD reserved;
        WORD substituteOffset; WORD substituteLength; WORD printOffset; WORD printLength;
        WCHAR path[1];
    };
    const int pathBytes = static_cast<int>((substitute.size() + nativeTarget.size() + 2) * sizeof(WCHAR));
    if (pathBytes + 8 > MAXIMUM_REPARSE_DATA_BUFFER_SIZE - 8 ||
        !CreateDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), nullptr)) {
        if (error) *error = QStringLiteral("Cannot create a temporary solver directory alias (Win32 %1).").arg(GetLastError());
        return false;
    }
    QByteArray storage(static_cast<qsizetype>(offsetof(JunctionData, path)) + pathBytes, '\0');
    auto* data = reinterpret_cast<JunctionData*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->dataLength = static_cast<WORD>(8 + pathBytes);
    data->substituteLength = static_cast<WORD>(substitute.size() * sizeof(WCHAR));
    data->printOffset = static_cast<WORD>(data->substituteLength + sizeof(WCHAR));
    data->printLength = static_cast<WORD>(nativeTarget.size() * sizeof(WCHAR));
    memcpy(data->path, substitute.utf16(), data->substituteLength);
    memcpy(reinterpret_cast<char*>(data->path) + data->printOffset, nativeTarget.utf16(), data->printLength);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    DWORD returned = 0;
    const bool success = handle != INVALID_HANDLE_VALUE && DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT,
        data, static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr);
    const DWORD failure = success ? ERROR_SUCCESS : GetLastError();
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!success) {
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()));
        if (error) *error = QStringLiteral("Cannot attach the temporary solver directory alias (Win32 %1).").arg(failure);
    }
    return success;
}

void removeOwnedDirectoryAlias(const QString& alias, const QString& aliasRoot)
{
    // These paths are created by this runner. Remove only the junction itself
    // and its empty temporary parent, never traverse into the result directory.
    if (!alias.isEmpty()) {
        const QString native = QDir::toNativeSeparators(alias);
        if (!RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()))) return;
    }
    if (!aliasRoot.isEmpty()) {
        QFile::remove(QDir(aliasRoot).filePath("pending-process.json"));
        const QString native = QDir::toNativeSeparators(aliasRoot);
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
    }
}
#endif

QString extractFdsDiagnostics(const QString& outputFilePath)
{
    QFile output(outputFilePath);
    if (!output.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QByteArray bytes = output.readAll();
    constexpr qsizetype maximumBytes = 512 * 1024;
    if (bytes.size() > maximumBytes) bytes = bytes.right(maximumBytes);
    const QString text = FdsOutputTextDecoder::decodeComplete(bytes);
    const QRegularExpression problem(
        QStringLiteral(R"((\bERROR\b|\bFATAL\b|forrtl:|STOP:|input file error))"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList diagnostics;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || !problem.match(line).hasMatch() ||
            diagnostics.contains(line)) continue;
        diagnostics.append(line);
        if (diagnostics.size() >= 20) break;
    }
    return diagnostics.join(QLatin1Char('\n'));
}
}

FdsRunner::FdsRunner(QObject* parent)
    : QObject(parent)
{
}

FdsRunner::~FdsRunner()
{
    if (m_process && isRunning() && !stopAndWait(5000)) {
        // A timed-out shutdown is not evidence that the child has exited.
        // Transfer its remaining lifetime and directory alias to its own
        // finished callback rather than destroying a live working directory.
        QProcess* const process = m_process;
        process->disconnect(this);
        process->setParent(nullptr);
        const QString alias = m_processDirectoryAlias;
        const QString aliasRoot = m_processAliasRoot ? m_processAliasRoot->path() : QString{};
        const auto job = m_processJob;
        if (!aliasRoot.isEmpty()) {
            QFile marker(QDir(aliasRoot).filePath("pending-process.json"));
            if (marker.open(QIODevice::WriteOnly)) {
                marker.write(QJsonDocument(QJsonObject{{"pid", process->processId()},
                    {"alias", alias}, {"workingDirectory", m_workingDirectory},
                    {"reason", "Shutdown timed out; remove alias only after the entire owned process job exits."}}).toJson());
            }
        }
#ifdef Q_OS_WIN
        // The launcher can exit before Intel MPI's detached bootstrap descendants.
        // Retain the job and alias until job accounting confirms every member ended.
        auto* watcher = new QTimer(process);
        connect(watcher, &QTimer::timeout, process, [process, alias, aliasRoot, job, watcher]() {
            if (!jobIsEmpty(job)) {
                TerminateJobObject(job->handle, 1);
                return;
            }
            if (process->state() != QProcess::NotRunning) return;
            watcher->stop();
            removeOwnedDirectoryAlias(alias, aliasRoot);
            process->deleteLater();
        });
        watcher->start(100);
#else
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
            [process](int, QProcess::ExitStatus) {
                process->deleteLater();
            });
#endif
        m_process = nullptr;
        m_processJob.reset();
        m_processDirectoryAlias.clear();
        m_processAliasRoot.reset(); // autoRemove is disabled: no filesystem cleanup here.
    } else {
        resetProcess();
    }
}

bool FdsRunner::isRunning() const
{
    return (m_process && m_process->state() != QProcess::NotRunning)
#ifdef Q_OS_WIN
        || !jobIsEmpty(m_processJob)
#endif
        ;
}

qint64 FdsRunner::processId() const
{
    return m_process ? m_process->processId() : 0;
}

QString FdsRunner::currentInputFile() const
{
    return m_request.inputFilePath;
}

QString FdsRunner::configuredExecutable()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
                     QString::fromLatin1(applicationName))
        .value(QString::fromLatin1(fdsSettingsKey))
        .toString();
}

void FdsRunner::setConfiguredExecutable(const QString& executablePath)
{
    QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
              QString::fromLatin1(applicationName))
        .setValue(QString::fromLatin1(fdsSettingsKey),
                  normalizedExecutable(executablePath));
}

QString FdsRunner::detectExecutable()
{
    const QString configured = configuredExecutable();
    if (validateExecutable(configured).isEmpty()) {
        return normalizedExecutable(configured);
    }

    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("fds/fds.exe"));
    if (validateExecutable(bundled).isEmpty()) {
        return normalizedExecutable(bundled);
    }

    const QString pathExecutable = QStandardPaths::findExecutable(QStringLiteral("fds"));
    if (validateExecutable(pathExecutable).isEmpty()) {
        return normalizedExecutable(pathExecutable);
    }

    const QStringList commonCandidates = {
        QStringLiteral("C:/Program Files/FireModels/FDS6/bin/fds.exe"),
        QStringLiteral("C:/Program Files (x86)/FireModels/FDS6/bin/fds.exe")};
    for (const QString& candidate : commonCandidates) {
        if (validateExecutable(candidate).isEmpty()) {
            return normalizedExecutable(candidate);
        }
    }
    return {};
}

QString FdsRunner::validateExecutable(const QString& executablePath)
{
    if (executablePath.isEmpty()) {
        return QStringLiteral("FDS executable is not configured.");
    }
    const QFileInfo info(executablePath);
    if (!info.exists() || !info.isFile()) {
        return QStringLiteral("FDS executable does not exist: %1")
            .arg(QDir::toNativeSeparators(executablePath));
    }
    const QString fileName = info.fileName();
    if (fileName.compare(QStringLiteral("fds.exe"), Qt::CaseInsensitive) != 0 &&
        fileName.compare(QStringLiteral("fds_openmp.exe"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("Selected executable is not fds.exe or fds_openmp.exe.");
    }
    return {};
}

QString FdsRunner::probeVersion(const SolverLaunchPlan& launchPlan,
                                QString* errorMessage,
                                int timeoutMilliseconds)
{
    if (!launchPlan.valid() || launchPlan.solverExecutable.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("A valid FDS launch plan is required.");
        return {};
    }

    QProcess probe;
    probe.setObjectName(QStringLiteral("FdsVersionProbeProcess"));
    probe.setWorkingDirectory(QFileInfo(launchPlan.solverExecutable).absolutePath());
    probe.setProcessChannelMode(QProcess::SeparateChannels);
    probe.setProcessEnvironment(launchPlan.environment);
    probe.setProgram(launchPlan.solverExecutable);
    probe.setArguments({QStringLiteral("-v")});
#ifdef Q_OS_WIN
    probe.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
#endif
    probe.start();
    const int timeout = qMax(100, timeoutMilliseconds);
    if (!probe.waitForStarted(timeout)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not start FDS version probe: %1")
                                .arg(probe.errorString());
        }
        return {};
    }
    if (!probe.waitForFinished(timeout)) {
        probe.kill();
        probe.waitForFinished(1000);
        if (errorMessage) *errorMessage = QStringLiteral("FDS version probe timed out.");
        return {};
    }

    const QString output = QString::fromLocal8Bit(probe.readAllStandardOutput()) +
                           QLatin1Char('\n') +
                           QString::fromLocal8Bit(probe.readAllStandardError());
    const QRegularExpression versionExpression(
        QStringLiteral(R"((?:FDS-)?(\d+\.\d+(?:\.\d+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch versionMatch =
        versionExpression.match(output);
    const QString version = versionMatch.hasMatch()
                                ? versionMatch.captured(1) : QString{};
    if (version.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("FDS did not report a recognizable version.");
        }
        return {};
    }
    if (errorMessage) errorMessage->clear();
    return version;
}

QString FdsRunner::caseIdFromInput(const QString& inputFilePath)
{
    QFile input(inputFilePath);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QFileInfo(inputFilePath).completeBaseName();
    }
    const QString contents = QString::fromUtf8(input.readAll());
    // Preserve offsets while hiding comments and quoted values. A TITLE or
    // comment containing CHID/CATF must not change the expected output paths.
    QString structural = contents;
    QChar quote;
    bool comment = false;
    for (qsizetype i = 0; i < contents.size(); ++i) {
        const QChar ch = contents.at(i);
        if (comment) {
            if (ch == QLatin1Char('\n')) comment = false;
            else structural[i] = QLatin1Char(' ');
        } else if (!quote.isNull()) {
            structural[i] = QLatin1Char(' ');
            if (ch == quote) {
                if (i + 1 < contents.size() && contents.at(i + 1) == quote)
                    structural[++i] = QLatin1Char(' ');
                else quote = {};
            }
        } else if (ch == QLatin1Char('!')) {
            comment = true;
            structural[i] = QLatin1Char(' ');
        } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
            structural[i] = QLatin1Char(' ');
        }
    }
    QString caseId = QFileInfo(inputFilePath).completeBaseName();
    const QRegularExpression records(QStringLiteral(R"(&([A-Za-z][A-Za-z0-9_]*)\b([^/]*)/)"));
    const QRegularExpression chidKey(QStringLiteral(R"(\bCHID\s*=)"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression chidValue(QStringLiteral(R"(^\s*('(?:''|[^'])*'|"(?:""|[^"])*"|[^,\s/]+))"));
    bool hasCatf = false, foundHead = false;
    auto matches = records.globalMatch(structural);
    while (matches.hasNext()) {
        const auto record = matches.next();
        const QString keyword = record.captured(1).toUpper();
        if (keyword == QStringLiteral("CATF")) hasCatf = true;
        if (keyword != QStringLiteral("HEAD") || foundHead) continue;
        foundHead = true;
        const auto key = chidKey.match(record.captured(2));
        if (!key.hasMatch()) continue;
        const auto value = chidValue.match(contents.mid(record.capturedStart(2) + key.capturedEnd()));
        if (!value.hasMatch()) continue;
        QString parsed = value.captured(1);
        if (parsed.startsWith(QLatin1Char('\'')) || parsed.startsWith(QLatin1Char('"'))) {
            const QChar delimiter = parsed.front();
            parsed = parsed.mid(1, parsed.size() - 2);
            parsed.replace(QString(2, delimiter), QString(delimiter));
        }
        if (!parsed.trimmed().isEmpty()) caseId = parsed.trimmed();
    }
    // FDS READ_CATF generates <CHID>_cat.fds with this derived HEAD CHID.
    return hasCatf ? caseId + QStringLiteral("_cat") : caseId;
}

bool FdsRunner::start(const FdsRunRequest& request, QString* errorMessage)
{
    if (isRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("An FDS calculation is already running.");
        }
        return false;
    }

    const QFileInfo inputInfo(request.inputFilePath);
    if (!inputInfo.exists() || !inputInfo.isFile() ||
        inputInfo.suffix().compare(QStringLiteral("fds"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("FDS input file does not exist or is not a .fds file: %1")
                                .arg(QDir::toNativeSeparators(request.inputFilePath));
        }
        return false;
    }

    QString executable = request.executablePath.isEmpty()
                             ? detectExecutable()
                             : normalizedExecutable(request.executablePath);
    const QString executableError = validateExecutable(executable);
    if (!executableError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = executableError;
        }
        return false;
    }

    resetProcess();
    m_request = request;
    m_request.inputFilePath = inputInfo.absoluteFilePath();
    m_request.processCount = request.mode == FdsRunMode::Mpi
                                 ? qMax(2, request.processCount) : 1;
    m_request.threadCount = request.mode == FdsRunMode::OpenMp
                                ? qMax(1, request.threadCount) : 1;
    m_caseId = caseIdFromInput(m_request.inputFilePath);
    SolverLaunchContext launchContext;
    launchContext.inputFilePath = inputInfo.absoluteFilePath();
    launchContext.fdsExecutablePath = executable;
    launchContext.bfdsExecutablePath =
        SolverBackendRegistry::configuredBfdsExecutable();
    launchContext.processCount = m_request.processCount;
    launchContext.threadCount = m_request.threadCount;
    const SolverBackendKind backendKind =
        request.mode == FdsRunMode::Mpi
            ? SolverBackendKind::FdsMpiCpu
            : request.mode == FdsRunMode::OpenMp
                  ? SolverBackendKind::FdsOpenMpCpu
                  : SolverBackendKind::FdsSerialCpu;
    std::unique_ptr<SolverBackend> backend = SolverBackendRegistry::create(backendKind);
    const SolverLaunchPlan launchPlan = backend->createLaunchPlan(launchContext);
    m_backendInfo = backend->info(launchContext);
    if (!launchPlan.valid()) {
        if (errorMessage) *errorMessage = launchPlan.errorMessage;
        return false;
    }
    m_executablePath = launchPlan.solverExecutable;
    m_stopRequested = false;
    m_finishReported = false;
    m_recentOutput.clear();
    m_recentError.clear();
    m_commandLine = launchPlan.commandLine;
    m_workingDirectory = launchPlan.workingDirectory;
    m_startedAtMilliseconds = QDateTime::currentMSecsSinceEpoch();
    m_previousSmvFingerprint = fileFingerprint(
        outputPathForCase(inputInfo, m_caseId, QStringLiteral(".smv")));
    m_previousOutputFingerprint = fileFingerprint(
        outputPathForCase(inputInfo, m_caseId, QStringLiteral(".out")));
    m_previousOutputBytes = 0;
    m_previousStepsBytes = 0;
    m_previousOutputHash.clear();
    m_previousStepsHash.clear();
    m_restartInheritanceVerified = false;
    if (m_request.restartEnabled && m_request.restartAppend) {
        const QString baselineSmv = QDir(m_request.restartSnapshotDirectory).filePath(m_caseId + ".smv");
        const QString smv = outputPathForCase(inputInfo, m_caseId, ".smv");
        const QString output = outputPathForCase(inputInfo, m_caseId, ".out");
        const QString steps = outputPathForCase(inputInfo, m_caseId, "_steps.csv");
        const QByteArray inheritedSmvHash = contentHash(baselineSmv);
        m_restartInheritanceVerified = !inheritedSmvHash.isEmpty() && inheritedSmvHash == contentHash(smv);
        if (!m_restartInheritanceVerified) {
            if (errorMessage) *errorMessage = QStringLiteral("RESTART SMV is not associated with the immutable inherited baseline.");
            return false;
        }
        m_previousOutputBytes = QFileInfo(output).size();
        m_previousStepsBytes = QFileInfo(steps).size();
        m_previousOutputHash = contentHash(output);
        m_previousStepsHash = contentHash(steps);
    }

    // FDS appends diagnostic/output suffixes and mesh/frame indices to CHID.
    // Reserve 64 characters for those suffixes, including _devc_ctrl_log.csv;
    // checking only the directory misses valid-length cwd + overlong filenames.
    const qsizetype maximumFileNameCharacters = qMax(inputInfo.fileName().size(), m_caseId.size() + 64);
    QString processDirectory = processWorkingDirectory(launchPlan.workingDirectory, maximumFileNameCharacters, errorMessage);
#ifdef Q_OS_WIN
    if (processDirectory.isEmpty()) {
        m_processAliasRoot = std::make_unique<QTemporaryDir>(QDir(QDir::tempPath()).filePath("firecae-run-XXXXXX"));
        // Never let recursive QTemporaryDir cleanup traverse the target.
        m_processAliasRoot->setAutoRemove(false);
        const QString alias = m_processAliasRoot->filePath("run");
        if (m_processAliasRoot->isValid() && alias.size() + 1 + maximumFileNameCharacters < MAX_PATH &&
            createDirectoryJunction(alias, launchPlan.workingDirectory, errorMessage)) {
            m_processDirectoryAlias = alias;
            processDirectory = alias;
            if (errorMessage) errorMessage->clear();
        }
    }
#endif
    if (processDirectory.isEmpty()) { resetProcess(); return false; }
    m_process = new QProcess(this);
    m_process->setObjectName(QStringLiteral("FdsSolverProcess"));
    m_process->setWorkingDirectory(processDirectory);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProcessEnvironment(launchPlan.environment);
    m_process->setProgram(launchPlan.program);
    m_process->setArguments(launchPlan.arguments);

#ifdef Q_OS_WIN
    m_processJob = std::make_shared<FdsProcessJob>();
    m_processJob->handle = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    auto startup = std::make_shared<ProcessJobStartup>();
    startup->job = m_processJob;
    if (!m_processJob->handle || !SetInformationJobObject(m_processJob->handle,
            JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !startup->initialize()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Cannot establish the Windows solver process job (Win32 %1).").arg(GetLastError());
        resetProcess();
        return false;
    }
    m_process->setCreateProcessArgumentsModifier(
        [startup](QProcess::CreateProcessArguments* arguments) {
            startup->info.StartupInfo = *reinterpret_cast<STARTUPINFOW*>(arguments->startupInfo);
            startup->info.StartupInfo.cb = sizeof(STARTUPINFOEXW);
            arguments->startupInfo = reinterpret_cast<Q_STARTUPINFO*>(&startup->info);
            // Windows assigns JOB_LIST before the initial thread may execute.
            // This avoids a post-start assignment race with short-lived MPI launchers.
            arguments->flags |= CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
        });
#endif

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FdsRunner::readProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &FdsRunner::readProcessError);
    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &FdsRunner::finishRun);
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Windows could not start FDS: %1")
                                .arg(m_process->errorString());
        }
        resetProcess();
        return false;
    }

    emit runStarted(m_request.inputFilePath, m_process->processId());
    return true;
}

void FdsRunner::stop()
{
    if (!isRunning()) {
        return;
    }
    m_stopRequested = true;

#ifdef Q_OS_WIN
    if (m_processJob && m_processJob->handle) {
        if (!TerminateJobObject(m_processJob->handle, 1))
            m_recentError += QStringLiteral("\nERROR: Cannot terminate the owned solver job (Win32 %1).\n").arg(GetLastError());
    }
#else
    m_process->terminate();
#endif

    const quint64 runGeneration = m_runGeneration;
    QTimer::singleShot(2000, this, [this, runGeneration]() {
        if (runGeneration != m_runGeneration) return;
        if (isRunning()) {
#ifdef Q_OS_WIN
            if (m_processJob && m_processJob->handle) TerminateJobObject(m_processJob->handle, 1);
#else
            m_process->kill();
#endif
        }
    });
}

bool FdsRunner::stopAndWait(int timeoutMilliseconds)
{
    if (!isRunning()) { resetProcess(); return true; }
    m_stopRequested = true;
    // This path is used while an owning window/task manager is shutting down.
    // QProcess::waitForFinished() may emit finished() synchronously; disconnect
    // first so finishRun() cannot clear m_process while this method is still
    // dereferencing it.
    QProcess* const process = m_process;
    process->disconnect(this);
#ifdef Q_OS_WIN
    if (m_processJob && m_processJob->handle && !TerminateJobObject(m_processJob->handle, 1))
        m_recentError += QStringLiteral("\nERROR: Cannot terminate the owned solver job (Win32 %1).\n").arg(GetLastError());
#else
    process->terminate();
#endif
    if (process->state() != QProcess::NotRunning &&
        !process->waitForFinished(qMax(100, timeoutMilliseconds))) {
        process->kill();
        process->waitForFinished(2000);
    }
    const bool stopped = process->state() == QProcess::NotRunning;
    const bool entireRunStopped = stopped
#ifdef Q_OS_WIN
        && waitForJobEmpty(m_processJob, timeoutMilliseconds)
#endif
        ;
    if (m_process == process) {
        if (entireRunStopped) {
            resetProcess();
        } else {
            connect(process, &QProcess::readyReadStandardOutput, this, &FdsRunner::readProcessOutput);
            connect(process, &QProcess::readyReadStandardError, this, &FdsRunner::readProcessError);
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                    this, &FdsRunner::finishRun);
        }
    }
    return entireRunStopped;
}

void FdsRunner::readProcessOutput()
{
    if (!m_process) return;
    appendProcessText(m_outputDecoder.append(m_process->readAllStandardOutput()), false);
}

void FdsRunner::readProcessError()
{
    if (!m_process) return;
    appendProcessText(m_errorDecoder.append(m_process->readAllStandardError()), true);
}

void FdsRunner::appendProcessText(const QString& text, bool standardError)
{
    if (text.isEmpty()) return;
    QString& recent = standardError ? m_recentError : m_recentOutput;
    recent.append(text);
    constexpr qsizetype maximumCharacters = 512 * 1024;
    if (recent.size() > maximumCharacters) {
        recent.remove(0, recent.size() - maximumCharacters);
    }
    if (standardError) emit errorOutputReceived(text);
    else emit outputReceived(text);
}

void FdsRunner::flushProcessText()
{
    appendProcessText(m_outputDecoder.finish(), false);
    appendProcessText(m_errorDecoder.finish(), true);
}

void FdsRunner::finishRun(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_finishReported) {
        return;
    }
#ifdef Q_OS_WIN
    if (!jobIsEmpty(m_processJob)) {
        // A launcher exit alone does not prove its descendants have terminated.
        // Close any remaining owned members, and defer terminal state/alias cleanup
        // until job accounting confirms the complete run is no longer active.
        if (!TerminateJobObject(m_processJob->handle, 1) &&
            !m_recentError.contains(QStringLiteral("Cannot terminate the owned solver job")))
            m_recentError += QStringLiteral("\nERROR: Cannot terminate the owned solver job (Win32 %1).\n").arg(GetLastError());
        const quint64 runGeneration = m_runGeneration;
        QTimer::singleShot(25, this, [this, runGeneration, exitCode, exitStatus]() {
            if (runGeneration == m_runGeneration) finishRun(exitCode, exitStatus);
        });
        return;
    }
#endif
    m_finishReported = true;
    readProcessOutput();
    readProcessError();
    flushProcessText();

    const QFileInfo inputInfo(m_request.inputFilePath);
    FdsRunSummary summary;
    summary.cancelled = m_stopRequested;
    summary.exitCode = exitCode;
    summary.elapsedMilliseconds =
        qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_startedAtMilliseconds);
    summary.inputFilePath = m_request.inputFilePath;
    summary.caseId = m_caseId;
    summary.smvFilePath = outputPathForCase(inputInfo, m_caseId, QStringLiteral(".smv"));
    summary.outputFilePath = outputPathForCase(inputInfo, m_caseId, QStringLiteral(".out"));
    summary.executablePath = m_executablePath;
    summary.backendId = m_backendInfo.id;
    summary.backendDisplayName = m_backendInfo.displayName;
    summary.processCount = m_request.processCount;
    summary.threadCount = m_request.threadCount;
    summary.commandLine = m_commandLine;
    summary.workingDirectory = m_workingDirectory;
    summary.standardError = m_recentError;
    summary.restarted = m_request.restartEnabled;
    summary.inheritedOutputBytes = m_previousOutputBytes;
    bool outputPrefixValid = true;
    const QByteArray currentOutput = appendedBytes(summary.outputFilePath,
        m_previousOutputBytes, m_previousOutputHash, &outputPrefixValid);
    const QString outputText = FdsOutputTextDecoder::decodeComplete(currentOutput);
    summary.diagnostics = m_request.restartEnabled ? QString{} : extractFdsDiagnostics(summary.outputFilePath);
    if (summary.diagnostics.isEmpty()) {
        const QRegularExpression problem(
            QStringLiteral(R"((\bERROR\b|\bFATAL\b|forrtl:|STOP:|input file error))"),
            QRegularExpression::CaseInsensitiveOption);
        QStringList lines;
        const QString combinedRecent = outputText + QLatin1Char('\n') + m_recentOutput + QLatin1Char('\n') + m_recentError;
        for (QString line : combinedRecent.split(QLatin1Char('\n'))) {
            line = line.trimmed();
            if (!line.isEmpty() && problem.match(line).hasMatch() &&
                !lines.contains(line)) lines.append(line);
            if (lines.size() >= 20) break;
        }
        summary.diagnostics = lines.join(QLatin1Char('\n'));
    }
    const bool completedNormally = outputText.contains(
        QStringLiteral("STOP: FDS completed successfully"), Qt::CaseInsensitive);
    const QRegularExpression fatalDiagnostic(
        QStringLiteral(R"(^\s*(?:ERROR(?:\s*\([^\)]*\))?\s*:|FATAL\b|forrtl:))"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const bool fatal = fatalDiagnostic.match(outputText + QLatin1Char('\n') +
                                             m_recentError).hasMatch();
    const QByteArray smvFingerprint = fileFingerprint(summary.smvFilePath);
    const QByteArray outputFingerprint = fileFingerprint(summary.outputFilePath);
    const bool freshOutputs = !smvFingerprint.isEmpty() && !outputFingerprint.isEmpty() &&
                              (smvFingerprint != m_previousSmvFingerprint || m_restartInheritanceVerified) &&
                              outputFingerprint != m_previousOutputFingerprint && outputPrefixValid;
    bool advanced = !m_request.restartEnabled;
    if (m_request.restartEnabled) {
        bool stepsPrefixValid = false;
        const QByteArray newSteps = appendedBytes(outputPathForCase(inputInfo, m_caseId, "_steps.csv"),
            m_previousStepsBytes, m_previousStepsHash, &stepsPrefixValid);
        double lastTime = -1.0;
        bool hasPositiveStep = false, ordered = true;
        for (const QByteArray& line : newSteps.split('\n')) {
            const QList<QByteArray> values = line.trimmed().split(',');
            if (values.size() < 4) continue;
            bool stepOk = false, timeOk = false;
            const double step = values.at(2).trimmed().toDouble(&stepOk);
            const double time = values.at(3).trimmed().toDouble(&timeOk);
            if (!stepOk || !timeOk) continue; // Header rows are not steps.
            if (!std::isfinite(step) || !std::isfinite(time) || step <= 0 ||
                (lastTime >= 0 && time <= lastTime)) { ordered = false; break; }
            hasPositiveStep = hasPositiveStep || step > 1e-9 * qMax(1.0, std::abs(time));
            lastTime = time;
        }
        summary.observedEndTime = lastTime;
        const double tolerance = 1e-5 * qMax(1.0, std::abs(m_request.requestedEndTime));
        advanced = stepsPrefixValid && ordered && hasPositiveStep && lastTime >= 0 &&
                   lastTime > m_request.restartBaselineTime + 1e-9 &&
                   std::abs(lastTime - m_request.requestedEndTime) <= tolerance;
    }
    summary.success = !summary.cancelled && exitStatus == QProcess::NormalExit &&
                      exitCode == 0 && freshOutputs && completedNormally && !fatal && advanced;

    if (summary.cancelled) {
        summary.errorMessage = QStringLiteral("FDS calculation was stopped by the user.");
    } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        summary.errorMessage = QStringLiteral("FDS exited with code %1.").arg(exitCode);
    } else if (!QFileInfo::exists(summary.smvFilePath)) {
        summary.errorMessage = QStringLiteral("FDS finished but did not create the expected SMV file: %1")
                                   .arg(QDir::toNativeSeparators(summary.smvFilePath));
    } else if (!freshOutputs) {
        summary.errorMessage = QStringLiteral("FDS exited without fresh SMV and output-log evidence for this run.");
    } else if (!completedNormally || fatal) {
        summary.errorMessage = QStringLiteral("FDS exited without a valid normal-completion record.");
    } else if (!advanced) {
        summary.errorMessage = QStringLiteral("RESTART has no verified new time advancement to the requested T_END.");
    }
    if (!summary.success && !summary.diagnostics.isEmpty()) {
        summary.errorMessage += QStringLiteral("\nFDS diagnostics:\n") +
                                summary.diagnostics;
    }

    emit runFinished(summary);
    resetProcess();
}

void FdsRunner::resetProcess()
{
    // Every alias cleanup must be downstream of confirmed process termination.
    if (isRunning()) return;
    readProcessOutput();
    readProcessError();
    flushProcessText();
    ++m_runGeneration;
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
#ifdef Q_OS_WIN
    m_processJob.reset();
    removeOwnedDirectoryAlias(m_processDirectoryAlias, m_processAliasRoot ? m_processAliasRoot->path() : QString{});
    m_processDirectoryAlias.clear();
    m_processAliasRoot.reset();
#endif
}
