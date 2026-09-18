#include "simulation/FdsRunner.h"
#include "results/FdsResultScanner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>
#include <algorithm>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: FireCAEBenchmarkRunner <input.fds> <fds.exe> [process-count]\n";
        return 2;
    }

    bool processCountOk = true;
    const int processCount = argc == 4
                                 ? QString::fromLocal8Bit(argv[3]).toInt(&processCountOk)
                                 : 1;
    if (!processCountOk || processCount < 1) {
        std::cerr << "process-count must be a positive integer.\n";
        return 2;
    }

    FdsRunner runner;
    FdsRunSummary summary;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&runner, &FdsRunner::outputReceived, [](const QString& output) {
        std::cout << output.toStdString() << std::flush;
    });
    QObject::connect(&runner, &FdsRunner::runFinished,
                     [&summary, &loop](const FdsRunSummary& result) {
                         summary = result;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, [&runner]() { runner.stop(); });

    FdsRunRequest request;
    request.inputFilePath = QString::fromLocal8Bit(argv[1]);
    request.executablePath = QString::fromLocal8Bit(argv[2]);
    request.mode = processCount > 1 ? FdsRunMode::Parallel : FdsRunMode::Serial;
    request.processCount = processCount;
    QString error;
    if (!runner.start(request, &error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    // The original 600 s couch tutorial can require several wall-clock hours.
    // Keep an unattended-run guard, but make it configurable and long enough
    // for the full official tutorials instead of silently converting them
    // into shortened smoke tests.
    bool timeoutOk = false;
    int timeoutMinutes = qEnvironmentVariableIntValue(
        "FIRECAE_BENCHMARK_TIMEOUT_MINUTES", &timeoutOk);
    if (!timeoutOk || timeoutMinutes <= 0) timeoutMinutes = 6 * 60;
    std::cout << "FireCAE acceptance timeout: " << timeoutMinutes
              << " minutes.\n";
    timeout.start(timeoutMinutes * 60 * 1000);
    loop.exec();

    if (!summary.success) {
        std::cerr << summary.errorMessage.toStdString() << '\n';
        return summary.cancelled ? 3 : 1;
    }
    std::cout << "\nFireCAE FdsRunner completed "
              << summary.caseId.toStdString() << " in "
              << summary.elapsedMilliseconds << " ms.\n";
    const FdsResultScanResult scan =
        FdsResultScanner().scanSmvFile(summary.smvFilePath);
    if (!scan.success()) {
        std::cerr << "Result scan failed: " << scan.errorMessage.toStdString() << '\n';
        return 4;
    }
    const auto existingFiles = std::count_if(
        scan.files.cbegin(), scan.files.cend(),
        [](const FdsResultFileInfo& file) { return file.exists; });
    std::cout << "FireCAE result scanner loaded " << existingFiles << '/'
              << scan.files.size() << " result files (status "
              << static_cast<int>(scan.status) << ").\n";
    return 0;
}
