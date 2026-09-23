#include "app/MainWindow.h"
#include "settings/ApplicationSettings.h"
#include "simulation/SimulationTaskManager.h"
#include "ui/ModelTreeWidget.h"
#include "ui/SimulationStatusWidget.h"
#include "ui/SimulationTaskCenterWidget.h"
#include "ui/UiLanguage.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* label)
{
    std::cout << (value ? "PASS " : "FAIL ") << label << std::endl;
    if (!value) ++failures;
}
void settle() { for (int n = 0; n < 5; ++n) QApplication::processEvents(); }
bool until(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 10000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}
void layoutOnly(MainWindow& window)
{
    // This tests real widgets/layout but never constructs a native OCCT/WGL view.
    auto* stack = qobject_cast<QStackedWidget*>(window.centralWidget());
    auto* placeholder = new QWidget(stack);
    placeholder->setMinimumSize(200, 100);
    stack->addWidget(placeholder);
    stack->setCurrentWidget(placeholder);
    window.resize(1400, 850);
    window.show();
    settle();
}
QString inputFile(const QTemporaryDir& temp, const QString& name, const QByteArray& mode)
{
    const QString path = temp.filePath(name + QStringLiteral(".fds"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write("&HEAD CHID='" + name.toUtf8() + "' /\n&MESH IJK=2,2,2, XB=0,1,0,1,0,1 /\n"
               "&TIME T_END=1 /\n! " + mode + "\n&TAIL /\n");
    return path;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (argc != 2) return 2; // Explicit controlled process fixture, never a real solver.
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.UxLayoutProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("BatchA"));
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temp.path());
    qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT", "1");
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    qputenv("FIRECAE_RECOVERY_DIRECTORY", temp.filePath("recovery").toUtf8());
    qunsetenv("FIRECAE_DISABLE_LAYOUT_RESTORE");
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);
    auto preferences = ApplicationSettingsStore().load();
    preferences.language = QStringLiteral("en");
    preferences.autoOpenResults = false;
    preferences.autoSaveEnabled = false;
    ApplicationSettingsStore().save(preferences);

    int savedTreeWidth = 0;
    {
        MainWindow window;
        layoutOnly(window);
        for (const char* name : {"MessagesDock", "SimulationTaskCenterDock", "FdsOutputDock", "ResultStatusDock"}) {
            auto* dock = window.findChild<QDockWidget*>(QString::fromLatin1(name));
            check(dock && dock->isHidden(), "Default bottom diagnostic dock is hidden");
        }
        auto* treeDock = window.findChild<QDockWidget*>(QStringLiteral("ModelTreeDock"));
        auto* tree = window.findChild<ModelTreeWidget*>();
        auto* treeView = tree ? tree->findChild<QTreeWidget*>() : nullptr;
        auto* center = window.findChild<SimulationTaskCenterWidget*>();
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("SimulationTaskCenterDock"));
        auto* status = window.findChild<SimulationStatusWidget*>();
        auto* manager = window.findChild<SimulationTaskManager*>();
        auto* detailsButton = status ? status->findChild<QToolButton*>("SimulationStatusDetailsButton") : nullptr;
        auto* stopButton = status ? status->findChild<QToolButton*>("SimulationStatusStopButton") : nullptr;
        check(treeDock && treeView && center && dock && status && manager && detailsButton && stopButton,
              "Production layout controls exist");
        if (!treeDock || !treeView || !center || !dock || !status || !manager || !detailsButton || !stopButton) return 1;
        check(status->isHidden(), "No empty simulation status area at startup");
        check(!center->detailsExpanded(), "Environment, command and raw log default to collapsed details");
        check(window.styleSheet().contains("separator") && window.styleSheet().contains("7px"),
              "Dock divider has a discoverable drag target");
        check(!window.runFdsFile(temp.filePath("missing-input.fds"), FdsRunMode::Serial),
              "Missing input is rejected before process launch");
        auto* messages = window.findChild<QDockWidget*>("MessagesDock");
        check(messages && !messages->isHidden() && manager->tasks().isEmpty(),
              "Preflight failure exposes its explanation even with diagnostics hidden");
        if (messages) messages->hide();
        const int originalWidth = treeDock->width();
        window.resizeDocks({treeDock}, {420}, Qt::Horizontal);
        settle();
        savedTreeWidth = treeDock->width();
        std::cout << "Tree resize: before=" << originalWidth << " after=" << savedTreeWidth
                  << " window=" << window.width() << " central=" << window.centralWidget()->width()
                  << " central minimum=" << window.centralWidget()->minimumWidth()
                  << " central minHint=" << window.centralWidget()->minimumSizeHint().width() << std::endl;
        check(savedTreeWidth > originalWidth + 30, "Model tree dock can expand using actual layout");
        treeView->header()->resizeSection(0, 720);

        FdsRunRequest request;
        request.mode = FdsRunMode::Serial;
        request.executablePath = QString::fromLocal8Bit(argv[1]);
        request.inputFilePath = inputFile(temp, "completed", "TEST_DELAY");
        QString error;
        const QString id = manager->enqueue(request, "Layout", "Default", 1.0, &error);
        check(!id.isEmpty(), "Controlled process fixture queues through the real task manager");
        if (id.isEmpty()) { std::cerr << error.toStdString(); return 1; }
        check(dock->isHidden() && !status->isHidden(), "Queue shows compact monitor without opening the dock");
        check(until([&]() { return manager->task(id)->state == SimulationTaskState::Running; }),
              "Fixture reaches real Running state");
        dock->show();
        dock->hide();
        check(manager->hasActiveTasks(), "Hiding task dock leaves the process active");
        check(until([&]() { return !manager->hasActiveTasks(); }), "Fixture completes while details are hidden");
        check(manager->task(id)->state == SimulationTaskState::Completed && dock->isHidden(),
              "Hidden panel does not interrupt completion or reopen itself");

        request.inputFilePath = inputFile(temp, "failed", "TEST_ZERO_ERROR");
        const QString failedId = manager->enqueue(request, "Failure", "Default", 1.0, &error);
        check(!failedId.isEmpty() && until([&]() {
            const auto state = manager->task(failedId)->state;
            return state == SimulationTaskState::Failed || state == SimulationTaskState::Completed;
        }), "Failure fixture terminates after leaving the queue");
        if (!failedId.isEmpty()) {
            std::cout << "Failure state: " << simulationTaskStateName(manager->task(failedId)->state).toStdString()
                      << "; status: " << detailsButton->text().toStdString()
                      << "; hidden dock: " << dock->isHidden() << std::endl;
        }
        check(!status->isHidden() && detailsButton->text().contains("Failed") && dock->isHidden(),
              "Failure remains visible in compact monitor without stealing workspace");
        detailsButton->click();
        settle();
        check(dock->isVisible() && center->selectedTaskId() == failedId && center->detailsExpanded(),
              "Details selects the exact failed UUID and opens its log");
        auto* failure = center->findChild<QLabel*>("SimulationTaskFailureSummary");
        auto* log = center->findChild<QPlainTextEdit*>("SimulationTaskLog");
        check(failure && failure->isVisible() && log && log->toPlainText().contains("controlled fixture failure"),
              "Failure explanation and unmodified raw error remain available");
        if (log) std::cout << "Selected failure raw log: " << log->toPlainText().toStdString() << std::endl;

        // Cancel before the queued start event; never launch a second process here.
        request.inputFilePath = inputFile(temp, "cancelled", "TEST_DELAY");
        const QString cancelled = manager->enqueue(request, "Cancel", "Default", 1.0, &error);
        check(!cancelled.isEmpty() && stopButton->isEnabled(), "Compact stop targets a cancellable job");
        stopButton->click();
        std::cout << "Cancelled fixture state: " << simulationTaskStateName(manager->task(cancelled)->state).toStdString() << std::endl;
        check(manager->task(cancelled)->state == SimulationTaskState::Cancelled, "Compact stop cancels its active UUID");
        check(detailsButton->text().contains("Failed"), "Other terminal jobs do not erase the failure indicator");
        center->setDetailsExpanded(false);
        window.setInterfaceLanguage(UiLanguage::ChineseSimplified);
        settle();
        auto* toggle = center->findChild<QToolButton*>("SimulationTaskDetailsToggle");
        check(toggle && toggle->text().contains(QString::fromUtf8("运行详情")) &&
              detailsButton->text().contains(QString::fromUtf8("失败")) &&
              failure->text().contains(QString::fromUtf8("任务失败")), "New monitor and detail controls translate to Chinese");
        center->setDetailsExpanded(true);
        dock->show();
        dock->raise();
        settle();
        savedTreeWidth = treeDock->width();
        check(window.close(), "Production close saves the chosen visible dock and width");
    }
    {
        MainWindow reopened;
        layoutOnly(reopened);
        auto* treeDock = reopened.findChild<QDockWidget*>("ModelTreeDock");
        auto* tree = reopened.findChild<ModelTreeWidget*>()->findChild<QTreeWidget*>();
        auto* dock = reopened.findChild<QDockWidget*>("SimulationTaskCenterDock");
        auto* center = reopened.findChild<SimulationTaskCenterWidget*>();
        check(treeDock && qAbs(treeDock->width() - savedTreeWidth) <= 8,
              "Tree dock width survives reopen and queued default sizing");
        check(tree->columnWidth(0) == 720, "Manual name column width survives reopen");
        check(dock && !dock->isHidden() && center->detailsExpanded(), "Intentional user expansion is respected on reopen");
        auto* reset = reopened.findChild<QAction*>("ResetLayoutAction");
        if (reset) reset->trigger();
        settle();
        check(reset && dock->isHidden(), "Reset Layout applies the compact modeling default");
        reopened.close();
    }
    std::cout << "UX workspace failures: " << failures << '\n';
    return failures ? 1 : 0;
}
