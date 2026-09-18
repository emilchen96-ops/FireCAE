#pragma once

#include <QString>
#include <QStringList>

class CrashDiagnostics final
{
public:
    static void install();
    static QString logDirectory();
    static QString logFilePath();
    static void recordOperation(const QString& operation);
    static QStringList recentOperations();
    static QString diagnosticReport(const QString& fdsExecutable,
                                    const QString& mpiExecutable,
                                    const QString& smokeviewExecutable,
                                    const QString& autoSaveDirectory);
};
