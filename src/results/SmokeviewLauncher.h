#pragma once

#include <QString>

enum class SmokeviewLaunchMode
{
    Standard,
    SmokeAndFire,
    Slice,
    Particles
};

struct SmokeviewLaunchResult
{
    bool success = false;
    qint64 processId = 0;
    QString executablePath;
    QString scriptPath;
    QString errorMessage;
    bool alreadyRunning = false;
};

class SmokeviewLauncher final
{
public:
    static QString configuredExecutable();
    static void setConfiguredExecutable(const QString& executablePath);
    static QString detectExecutable();
    static QString validateExecutable(const QString& executablePath);
    static SmokeviewLaunchResult launch(
        const QString& smvFilePath,
        SmokeviewLaunchMode mode = SmokeviewLaunchMode::Standard);
    // Produces a unique interactive SSF. launch() owns its lifetime; direct
    // callers must remove it after it is no longer used.
    static QString createAnimationScript(const QString& smvFilePath,
                                         SmokeviewLaunchMode mode,
                                         QString* errorMessage = nullptr);
};
