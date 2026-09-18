#include "app/MainWindow.h"
#include "reliability/CrashDiagnostics.h"
#include "ui/UiLanguage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FireCAE"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE"));
    // An explicit profile keeps portable acceptance runs separate from the
    // user's installed application. Normal launches retain the native profile.
    const QString settingsDirectory =
        qEnvironmentVariable("FIRECAE_SETTINGS_DIRECTORY").trimmed();
    if (!settingsDirectory.isEmpty()) {
        if (!QDir::isAbsolutePath(settingsDirectory) ||
            !QDir().mkpath(settingsDirectory)) {
            QTextStream(stderr) << "FIRECAE_SETTINGS_DIRECTORY must be a writable absolute directory.\n";
            return 2;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory);
    }
    UiLanguageManager::initialize();
    CrashDiagnostics::install();
    CrashDiagnostics::recordOperation(QStringLiteral("Application startup"));

    const bool startupSmoke = application.arguments().contains(
        QStringLiteral("--startup-smoke"));
    if (startupSmoke) {
        qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT", "1");
        qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    }

    MainWindow mainWindow;
    mainWindow.show();

    // Used by the portable-package acceptance test.  This follows the same
    // startup path as an interactive launch, including Qt/OCCT initialization,
    // and exits only after the event loop has had time to paint the window.
    if (startupSmoke) {
        QTimer::singleShot(1500, &application, &QCoreApplication::quit);
    }

    const int result = application.exec();
    CrashDiagnostics::recordOperation(QStringLiteral("Application shutdown"));
    return result;
}
