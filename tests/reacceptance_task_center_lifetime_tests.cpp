#include "simulation/SimulationTaskManager.h"
#include "ui/SimulationTaskCenterWidget.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

namespace {
int failures = 0;
bool check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!condition) ++failures;
    return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

QString queueAndCancel(SimulationTaskManager* manager, const FdsRunRequest& request)
{
    QString error;
    const QString id = manager->enqueue(request, QStringLiteral("Lifetime"),
                                       QStringLiteral("Default"), 1.0, &error);
    if (id.isEmpty()) std::cerr << error.toStdString() << std::endl;
    if (!id.isEmpty() && !manager->cancel(id)) return {};
    return id;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAEReacceptanceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TaskCenterLifetime"));
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "temporary fixture directory exists")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    const QDir directory(temporary.path());
    FdsRunRequest request;
    request.inputFilePath = directory.filePath(QStringLiteral("lifetime.fds"));
    request.executablePath = directory.filePath(QStringLiteral("fds.exe"));
    request.mode = FdsRunMode::Serial;
    if (!check(writeFile(request.inputFilePath,
                        "&HEAD CHID='lifetime' /\n&MESH IJK=1,1,1, XB=0,1,0,1,0,1 /\n"
                        "&TIME T_END=1 /\n&TAIL /\n") &&
                   writeFile(request.executablePath, "Never executed: queued-task lifetime fixture.\n"),
               "creates input and nonexecuted solver placeholder")) return 1;

    // enqueue schedules startup for a later event-loop turn. Each task is
    // cancelled synchronously, and no events run before its manager is deleted;
    // this test never launches either the placeholder or a real solver.
    QWidget owner;
    auto* manager = new SimulationTaskManager(&owner);
    auto* center = new SimulationTaskCenterWidget(&owner);
    center->setManager(manager);
    auto* table = center->findChild<QTableWidget*>(QStringLiteral("SimulationTaskTable"));
    auto* retry = center->findChild<QPushButton*>(QStringLiteral("SimulationTaskRetryButton"));
    if (!check(table && retry, "task center controls exist")) return 1;
    const QString firstId = queueAndCancel(manager, request);
    if (!check(!firstId.isEmpty() && table->rowCount() == 1 &&
                   center->selectedTaskId() == firstId && retry->isEnabled(),
               "real manager record is selected before manager destruction")) return 1;
    std::cout << "STAGE: destroy manager while the task center remains alive" << std::endl;
    delete manager;
    if (!check(table->rowCount() == 0 && center->selectedTaskId().isEmpty() &&
                   !retry->isEnabled(),
               "destroyed manager invalidates task rows, selection and controls")) {
        // Keep the red run's cleanup from introducing an additional dangling-
        // pointer crash after the explicit lifetime assertion already failed.
        table->blockSignals(true);
        delete center;
        return 1;
    }
    table->clearSelection();
    center->retranslateUi();
    std::cout << "STAGE: bind a replacement manager after destruction" << std::endl;
    auto* replacement = new SimulationTaskManager(&owner);
    center->setManager(replacement);
    const QString secondId = queueAndCancel(replacement, request);
    check(!secondId.isEmpty() && table->rowCount() == 1 &&
              center->selectedTaskId() == secondId,
          "task center can bind and display a replacement manager");
    center->setManager(nullptr);
    check(table->rowCount() == 0 && !retry->isEnabled(),
          "explicitly detaching the manager clears the task UI");
    delete replacement;

    std::cout << "STAGE: destroy a shared parent with manager created first" << std::endl;
    auto nestedOwner = std::make_unique<QWidget>();
    auto* nestedManager = new SimulationTaskManager(nestedOwner.get());
    auto* nestedCenter = new SimulationTaskCenterWidget(nestedOwner.get());
    nestedCenter->setManager(nestedManager);
    check(!queueAndCancel(nestedManager, request).isEmpty(),
          "shared-parent fixture has a selected terminal task");
    nestedOwner.reset();
    std::cout << "Task center lifetime failures: " << failures << std::endl;
    return failures ? 1 : 0;
}
