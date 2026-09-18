#include "reliability/CrashDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

#include <cstdlib>
#include <exception>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef FIRECAE_OCCT_VERSION
#define FIRECAE_OCCT_VERSION "unknown"
#endif

namespace
{
QMutex diagnosticMutex;
QString currentLogPath;
QStringList operations;
QtMessageHandler previousMessageHandler = nullptr;

QString executableStatus(const QString& label, const QString& configuredPath)
{
    if (configuredPath.trimmed().isEmpty()) {
        return QStringLiteral("%1: not configured (bundled discovery will be used)")
            .arg(label);
    }
    const QFileInfo file(configuredPath);
    return QStringLiteral("%1: %2 [%3]")
        .arg(label, QDir::toNativeSeparators(file.absoluteFilePath()),
             file.exists() && file.isFile() ? QStringLiteral("OK")
                                             : QStringLiteral("MISSING"));
}

QString messageLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARNING");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void writeDiagnosticLine(const QString& line)
{
    QMutexLocker locker(&diagnosticMutex);
    if (currentLogPath.isEmpty()) return;
    QFile file(currentLogPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << " " << line << "\n";
    stream.flush();
}

void fireCaeMessageHandler(QtMsgType type,
                           const QMessageLogContext& context,
                           const QString& message)
{
    QString location;
    if (context.file) {
        location = QStringLiteral(" [%1:%2]")
                       .arg(QString::fromLocal8Bit(context.file))
                       .arg(context.line);
    }
    writeDiagnosticLine(QStringLiteral("[%1] %2%3")
                            .arg(messageLevel(type), message, location));
    if (previousMessageHandler) previousMessageHandler(type, context, message);
}

void terminateHandler()
{
    writeDiagnosticLine(QStringLiteral("[CRASH] std::terminate invoked."));
    std::abort();
}

#ifdef Q_OS_WIN
LONG WINAPI unhandledExceptionHandler(EXCEPTION_POINTERS* pointers)
{
    const unsigned long code = pointers && pointers->ExceptionRecord
                                   ? pointers->ExceptionRecord->ExceptionCode
                                   : 0;
    const quintptr address = pointers && pointers->ExceptionRecord
                                 ? reinterpret_cast<quintptr>(
                                       pointers->ExceptionRecord->ExceptionAddress)
                                 : 0;
    writeDiagnosticLine(QStringLiteral("[CRASH] Windows exception code=0x%1 address=0x%2")
                            .arg(code, 8, 16, QLatin1Char('0'))
                            .arg(address, sizeof(quintptr) * 2, 16, QLatin1Char('0')));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
}

void CrashDiagnostics::install()
{
    QDir().mkpath(logDirectory());
    currentLogPath = QDir(logDirectory()).filePath(
        QStringLiteral("FireCAE_%1_%2.log")
            .arg(QDateTime::currentDateTimeUtc().toString(
                     QStringLiteral("yyyyMMdd_HHmmss_zzz")))
            .arg(QCoreApplication::applicationPid()));
    previousMessageHandler = qInstallMessageHandler(fireCaeMessageHandler);
    std::set_terminate(terminateHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionHandler);
#endif
    writeDiagnosticLine(QStringLiteral("[START] FireCAE %1; executable=%2")
                            .arg(QCoreApplication::applicationVersion(),
                                 QDir::toNativeSeparators(
                                     QCoreApplication::applicationFilePath())));
}

QString CrashDiagnostics::logDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs"));
}

QString CrashDiagnostics::logFilePath()
{
    QMutexLocker locker(&diagnosticMutex);
    return currentLogPath;
}

void CrashDiagnostics::recordOperation(const QString& operation)
{
    const QString normalized = operation.simplified().left(300);
    if (normalized.isEmpty()) return;
    {
        QMutexLocker locker(&diagnosticMutex);
        operations.append(QStringLiteral("%1  %2")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                   normalized));
        while (operations.size() > 50) operations.removeFirst();
    }
    writeDiagnosticLine(QStringLiteral("[OPERATION] %1").arg(normalized));
}

QStringList CrashDiagnostics::recentOperations()
{
    QMutexLocker locker(&diagnosticMutex);
    return operations;
}

QString CrashDiagnostics::diagnosticReport(const QString& fdsExecutable,
                                           const QString& mpiExecutable,
                                           const QString& smokeviewExecutable,
                                           const QString& autoSaveDirectory)
{
    QString report;
    QTextStream stream(&report);
    stream << "FireCAE diagnostic report\n"
           << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n"
           << "FireCAE version: " << QCoreApplication::applicationVersion() << "\n"
           << "Executable: " << QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) << "\n"
           << "OS: " << QSysInfo::prettyProductName() << " " << QSysInfo::currentCpuArchitecture() << "\n"
           << "Qt: " << QLibraryInfo::version().toString() << "\n"
           << "OpenCascade: " << FIRECAE_OCCT_VERSION << "\n"
           << executableStatus(QStringLiteral("FDS executable"), fdsExecutable) << "\n"
           << executableStatus(QStringLiteral("MPI launcher"), mpiExecutable) << "\n"
           << executableStatus(QStringLiteral("Smokeview executable"), smokeviewExecutable) << "\n"
           << "Log: " << QDir::toNativeSeparators(logFilePath()) << "\n"
           << "Auto-save directory: " << QDir::toNativeSeparators(autoSaveDirectory) << "\n"
           << "Required deployed runtimes:\n";
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QStringList runtimeFiles = {
        QStringLiteral("Qt6Core.dll"), QStringLiteral("Qt6Gui.dll"),
        QStringLiteral("Qt6Widgets.dll"), QStringLiteral("TKernel.dll"),
        QStringLiteral("TKOpenGl.dll"), QStringLiteral("TKV3d.dll"),
        QStringLiteral("tbb12.dll")};
    for (const QString& runtime : runtimeFiles) {
        const QString path = applicationDirectory.filePath(runtime);
        stream << "  " << runtime << ": "
               << (QFileInfo::exists(path) ? "OK" : "MISSING") << "\n";
    }
    stream << "Runtime diagnostics: "
           << (std::all_of(runtimeFiles.cbegin(), runtimeFiles.cend(),
                           [&applicationDirectory](const QString& runtime) {
                               return QFileInfo::exists(
                                   applicationDirectory.filePath(runtime));
                           })
                   ? "all required deployed DLLs found"
                   : "one or more required DLLs are missing")
           << "\n"
           << "Recent operations (no project file contents are recorded):\n";
    for (const QString& operation : recentOperations()) stream << "  " << operation << "\n";
    return report;
}
