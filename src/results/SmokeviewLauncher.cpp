#include "results/SmokeviewLauncher.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
constexpr auto organizationName = "FireCAE";
constexpr auto applicationName = "FireCAE";
constexpr auto smokeviewSettingsKey = "applications/smokeviewExecutable";

QHash<QString, QProcess*>& runningProcesses()
{
    static QHash<QString, QProcess*> processes;
    return processes;
}

QString modeName(SmokeviewLaunchMode mode)
{
    switch (mode) {
    case SmokeviewLaunchMode::SmokeAndFire: return QStringLiteral("smoke-fire");
    case SmokeviewLaunchMode::Slice: return QStringLiteral("slice");
    case SmokeviewLaunchMode::Particles: return QStringLiteral("particles");
    case SmokeviewLaunchMode::Standard: return QStringLiteral("standard");
    }
    return QStringLiteral("standard");
}
}

QString SmokeviewLauncher::configuredExecutable()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
                     QString::fromLatin1(applicationName))
        .value(QString::fromLatin1(smokeviewSettingsKey))
        .toString();
}

void SmokeviewLauncher::setConfiguredExecutable(const QString& executablePath)
{
    QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(organizationName),
              QString::fromLatin1(applicationName))
        .setValue(QString::fromLatin1(smokeviewSettingsKey),
                  executablePath.trimmed().isEmpty()
                      ? QString{}
                      : QFileInfo(executablePath).absoluteFilePath());
}

QString SmokeviewLauncher::detectExecutable()
{
    const QString configured = configuredExecutable();
    if (validateExecutable(configured).isEmpty()) {
        return QFileInfo(configured).absoluteFilePath();
    }

    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("smokeview/smokeview.exe"));
    if (validateExecutable(bundled).isEmpty()) {
        return QFileInfo(bundled).absoluteFilePath();
    }

    const QString pathExecutable = QStandardPaths::findExecutable(QStringLiteral("smokeview"));
    if (validateExecutable(pathExecutable).isEmpty()) {
        return QFileInfo(pathExecutable).absoluteFilePath();
    }

    const QStringList commonCandidates = {
        QStringLiteral("C:/Program Files/FireModels/SMV6/smokeview.exe"),
        QStringLiteral("C:/Program Files/FireModels/FDS6/bin/smokeview.exe"),
        QStringLiteral("C:/Program Files (x86)/FireModels/SMV6/smokeview.exe")};
    for (const QString& candidate : commonCandidates) {
        if (validateExecutable(candidate).isEmpty()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString SmokeviewLauncher::validateExecutable(const QString& executablePath)
{
    if (executablePath.isEmpty()) {
        return QStringLiteral("Smokeview executable is not configured.");
    }
    const QFileInfo info(executablePath);
    if (!info.exists() || !info.isFile()) {
        return QStringLiteral("Smokeview executable does not exist: %1")
            .arg(QDir::toNativeSeparators(executablePath));
    }
    if (info.fileName().compare(QStringLiteral("smokeview.exe"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("Selected executable is not smokeview.exe.");
    }
    return {};
}

QString SmokeviewLauncher::createAnimationScript(const QString& smvFilePath,
                                                  SmokeviewLaunchMode mode,
                                                  QString* errorMessage)
{
    const QFileInfo smvInfo(smvFilePath);
    if (!smvInfo.exists() || !smvInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Smokeview result file does not exist: %1")
                                .arg(QDir::toNativeSeparators(smvFilePath));
        }
        return {};
    }
    if (errorMessage) errorMessage->clear();
    QStringList commands;
    if (mode != SmokeviewLaunchMode::Standard) commands << QStringLiteral("UNLOADALL");
    const QDir caseDirectory = smvInfo.absoluteDir();
    if (mode == SmokeviewLaunchMode::SmokeAndFire) {
        QStringList files;
        QFile manifest(smvFilePath);
        if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream lines(&manifest);
            while (!lines.atEnd()) {
                const QString line = lines.readLine().trimmed();
                if (line.endsWith(QStringLiteral(".s3d"), Qt::CaseInsensitive) &&
                    QFileInfo(caseDirectory.filePath(line)).isFile() && !files.contains(line))
                    files.append(line);
            }
        }
        if (files.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("This result case has no 3D smoke/fire data (.s3d). ");
            }
            return {};
        }
        for (const QString& file : files) {
            commands << QStringLiteral("LOADFILE") << file;
        }
    } else if (mode == SmokeviewLaunchMode::Slice) {
        // SLICEAUTO selected the first record in this SMV. Keep that scope and
        // order when using a script: directory globbing could pick another
        // case's file (or a different quantity) from a shared result folder.
        QString firstSlice;
        QFile smv(smvFilePath);
        if (smv.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream lines(&smv);
            while (!lines.atEnd()) {
                const QString line = lines.readLine();
                // FDS writes record headers at column zero and indents data.
                // Do not reinterpret a title, quantity or filename as a record.
                if (line.isEmpty() || line.front().isSpace()) continue;
                const QString header = line.simplified();
                if (header == QStringLiteral("TITLE") || header == QStringLiteral("FDSVERSION") ||
                    header == QStringLiteral("INPF") || header == QStringLiteral("REVISION") ||
                    header == QStringLiteral("CHID")) {
                    if (!lines.atEnd()) lines.readLine();
                    continue;
                }
                if (header == QStringLiteral("CSVF")) {
                    for (int i = 0; i < 2 && !lines.atEnd(); ++i) lines.readLine();
                    continue;
                }
                const QStringList fields = header.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (fields.isEmpty()) continue;
                const QString& keyword = fields.constFirst();
                // These are the native structured, cell-centred and terrain
                // slice records. A bare header remains valid for single-mesh
                // legacy SMV files; an explicit mesh must be a positive index.
                if (keyword != QStringLiteral("SLCF") && keyword != QStringLiteral("SLCC") &&
                    keyword != QStringLiteral("SLCT")) continue;
                if (fields.size() > 1) {
                    bool validMesh = false;
                    const int meshIndex = fields.at(1).toInt(&validMesh);
                    if (!validMesh || meshIndex <= 0) continue;
                }
                if (!lines.atEnd()) {
                    firstSlice = lines.readLine().trimmed();
                    if (firstSlice.startsWith(QLatin1Char('"')) && firstSlice.endsWith(QLatin1Char('"'))) {
                        firstSlice = firstSlice.mid(1, firstSlice.size() - 2);
                    }
                    break;
                }
            }
        }
        if (!firstSlice.endsWith(QStringLiteral(".sf"), Qt::CaseInsensitive) ||
            !QFileInfo(caseDirectory.filePath(firstSlice)).isFile()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The first slice declared by this Smokeview case is missing or unreadable.");
            }
            return {};
        }
        commands << QStringLiteral("LOADFILE") << firstSlice;
    } else if (mode == SmokeviewLaunchMode::Particles) {
        const QFileInfoList files = caseDirectory.entryInfoList(
            {QStringLiteral("*.prt5")}, QDir::Files, QDir::Name);
        if (files.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("This result case has no particle data (.prt5). ");
            }
            return {};
        }
        commands << QStringLiteral("LOADPARTICLES");
    }
    // Data-loading scripts must stay interactive after selecting the first frame.
    commands << QStringLiteral("SETTIMEVAL") << QStringLiteral("0.0")
             << QStringLiteral("NOEXIT");

    const QString scriptDirectory = QDir(QStandardPaths::writableLocation(
                                             QStandardPaths::TempLocation))
                                        .filePath(QStringLiteral("FireCAE/smokeview-scripts"));
    if (!QDir().mkpath(scriptDirectory)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create the Smokeview script directory.");
        }
        return {};
    }
    const QString scriptPath = QDir(scriptDirectory).filePath(
        QStringLiteral("%1-%2-%3.ssf")
            .arg(smvInfo.completeBaseName(), modeName(mode),
                 QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QSaveFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write the Smokeview animation script: %1")
                                .arg(QDir::toNativeSeparators(scriptPath));
        }
        return {};
    }
    const QByteArray scriptText = (commands.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (script.write(scriptText) != scriptText.size() || !script.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not finish the Smokeview startup script.");
        return {};
    }
    return scriptPath;
}

SmokeviewLaunchResult SmokeviewLauncher::launch(const QString& smvFilePath,
                                                 SmokeviewLaunchMode mode)
{
    SmokeviewLaunchResult result;
    const QFileInfo smvInfo(smvFilePath);
    if (!smvInfo.exists() || !smvInfo.isFile()) {
        result.errorMessage = QStringLiteral("Smokeview result file does not exist: %1")
                                  .arg(QDir::toNativeSeparators(smvFilePath));
        return result;
    }

    result.executablePath = detectExecutable();
    result.errorMessage = validateExecutable(result.executablePath);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    const QString processKey = smvInfo.canonicalFilePath() + QLatin1Char('|') + modeName(mode);
    if (QProcess* existing = runningProcesses().value(processKey, nullptr)) {
        if (existing->state() != QProcess::NotRunning) {
            result.success = true;
            result.alreadyRunning = true;
            result.processId = existing->processId();
            return result;
        }
        runningProcesses().remove(processKey);
        existing->deleteLater();
    }

    QStringList arguments = {smvInfo.absoluteFilePath()};
    if (mode == SmokeviewLaunchMode::SmokeAndFire) {
        if (smvInfo.absoluteDir().entryList({QStringLiteral("*.s3d")}, QDir::Files).isEmpty()) {
            result.errorMessage = QStringLiteral("This result case has no 3D smoke/fire data (.s3d).");
            return result;
        }
        arguments << QStringLiteral("-load_soot") << QStringLiteral("-load_hrrpuv");
    } else if (mode != SmokeviewLaunchMode::Standard) {
        result.scriptPath = createAnimationScript(smvFilePath, mode, &result.errorMessage);
        if (result.scriptPath.isEmpty()) return result;
        arguments << QStringLiteral("-script") << result.scriptPath;
    }
    // Ordinary and smoke/fire opens use native startup. LOADINIFILE startup
    // scripts have a non-graphics early-exit path and intermittent access faults.
    // Display defaults belong to the bundled INI, below case-specific settings.
    const QString scriptPath = result.scriptPath;

    auto* process = new QProcess(QCoreApplication::instance());
    process->setProgram(result.executablePath);
    process->setArguments(arguments);
    process->setWorkingDirectory(smvInfo.absolutePath());
    process->setStandardInputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    QString logDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                     .filePath(QStringLiteral("logs/smokeview"));
    if (!QDir().mkpath(logDirectory))
        logDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("FireCAE/smokeview-logs"));
    QString logPath;
    if (QDir().mkpath(logDirectory)) {
        logPath = QDir(logDirectory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".log"));
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setStandardOutputFile(logPath);
    }
    qInfo().noquote() << "Smokeview starting:" << result.executablePath << arguments << "log:" << logPath;
    QObject::connect(process,
                     qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     process,
                     [processKey, process, scriptPath, logPath](int exitCode, QProcess::ExitStatus exitStatus) {
                         qInfo().noquote() << "Smokeview finished: exitCode=" << exitCode
                                          << "status=" << exitStatus << "log:" << logPath;
                         if (runningProcesses().value(processKey) == process) {
                             runningProcesses().remove(processKey);
                         }
                         if (!scriptPath.isEmpty()) QFile::remove(scriptPath);
                         process->deleteLater();
                     });
    process->start();
    result.success = process->waitForStarted(5000);
    result.processId = process->processId();
    if (result.success) {
        runningProcesses().insert(processKey, process);
    } else {
        result.errorMessage = QStringLiteral("Windows could not start Smokeview.");
        if (!scriptPath.isEmpty()) QFile::remove(scriptPath);
        process->deleteLater();
    }
    return result;
}
