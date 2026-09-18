#include "app/MainWindow.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsExamples.h"
#include "ui/MessageWidget.h"
#include "ui/TutorialGuideWidget.h"
#include "ui/UiLanguage.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <iostream>

namespace {
int checks = 0;
int failures = 0;
void expect(bool condition, const char* message)
{
    ++checks;
    std::cerr << "CHECK " << checks << ": " << message << " => " << (condition ? "PASS" : "FAIL") << std::endl;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

void showLayoutOnly(MainWindow& window)
{
    // Exercise the real QMainWindow layout without asking the offscreen
    // platform to construct an OCCT/WGL rendering surface.
    window.centralWidget()->hide();
    window.show();
    settle();
}

void validate(MainWindow& window)
{
    std::cerr << "STAGE: begin validation" << std::endl;
    auto* action = window.findChild<QAction*>(QStringLiteral("ValidateModelAction"));
    expect(action != nullptr, "Production Validate Model action exists");
    if (!action) return;
    int dialogs = 0;
    QTimer closer;
    closer.setInterval(0);
    QObject::connect(&closer, &QTimer::timeout, &window, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QMessageBox*>(widget); dialog && dialog->isVisible()) {
                ++dialogs;
                dialog->accept();
            }
        }
    });
    closer.start();
    action->trigger();
    std::cerr << "STAGE: validation action returned" << std::endl;
    closer.stop();
    settle();
    expect(dialogs == 1, "Validation executes and displays its real result dialog");
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("WorkspaceInspectorTabs"));
    expect(tabs && tabs->currentIndex() == 2, "Validation opens the inspector validation tab");
    auto* messages = window.findChild<QPlainTextEdit*>(QStringLiteral("MessageLog"));
    expect(messages && messages->toPlainText().contains(QStringLiteral("[Error]")),
           "Invalid blank model produces real validation error messages");
}

bool onScreen(const QDockWidget& dock)
{
    for (QScreen* screen : QGuiApplication::screens())
        if (screen->availableGeometry().contains(dock.frameGeometry())) return true;
    return false;
}

void checkRepaired(const MainWindow& window, const QDockWidget& dock, const QSize& minimum)
{
    expect(dock.isFloating(), "Repair preserves floating state");
    const QRect available = dock.screen()->availableGeometry();
    const QSize required = minimum.boundedTo(available.adjusted(8, 8, -8, -8).size());
    expect(dock.width() >= required.width() && dock.height() >= required.height(),
           "Tiny feedback panel becomes readable in logical pixels");
    expect(onScreen(dock), "Repaired feedback panel fits the available screen");
    QRect expected(QPoint(), dock.frameGeometry().size());
    const QRect safe = available.adjusted(8, 8, -8, -8);
    expected.moveCenter(window.frameGeometry().center());
    expected.moveLeft(qBound(safe.left(), expected.left(), safe.right() - expected.width() + 1));
    expected.moveTop(qBound(safe.top(), expected.top(), safe.bottom() - expected.height() + 1));
    expect((expected.topLeft() - dock.frameGeometry().topLeft()).manhattanLength() <= 8,
           "Unreadable float is centered on the owner, bounded to the screen");
}

void checkMessageWrapping()
{
    MessageWidget widget;
    QString activated;
    widget.setObjectActivationHandler([&](const QString& id) { activated = id; });
    const QString uuid = QStringLiteral("{12345678-1234-1234-1234-123456789abc}");
    const QString message = QStringLiteral("[Error] ") + QString(220, QLatin1Char('W')) + uuid;
    widget.appendMessage(message);
    widget.resize(300, 180);
    widget.show();
    settle();
    auto* log = widget.findChild<QPlainTextEdit*>(QStringLiteral("MessageLog"));
    expect(log != nullptr, "Message log can be identified");
    if (!log) return;
    log->moveCursor(QTextCursor::Start);
    log->ensureCursorVisible();
    settle();
    expect(log->toPlainText() == message && log->isReadOnly(),
           "Wrapping preserves the exact read-only message text");
    expect(log->document()->firstBlock().layout()->lineCount() > 1 &&
               log->horizontalScrollBar()->maximum() == 0,
           "Long unbroken error text wraps without horizontal clipping");
    const QPointF local(12, 8);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, local,
                           log->viewport()->mapToGlobal(local.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(log->viewport(), &doubleClick);
    expect(activated == uuid,
           "Double-clicking an early wrapped line still locates the UUID at the end of its message");
}

void checkTutorial(const QString& id, std::unique_ptr<FcProject> project)
{
    TutorialGuideWidget guide;
    guide.startTutorial(id, true);
    guide.setContext(project.get(), {}, {}, false);
    auto* steps = guide.findChild<QListWidget*>(QStringLiteral("TutorialGuideStepList"));
    auto* next = guide.findChild<QPushButton*>(QStringLiteral("TutorialGuideNextButton"));
    expect(steps && next, "Real tutorial navigation controls exist");
    if (!steps || !next) return;
    steps->setCurrentRow(1);
    QString detail;
    expect(guide.currentStepIndex() == 1, "Tutorial selects object-construction step");
    expect(guide.currentStepComplete(&detail) && next->isEnabled(),
           "Canonical short-smoke recipe can complete the object step without a fictional SM3D record");
    expect(!detail.contains(QStringLiteral("SM3D")), "No missing-SM3D error remains");
    auto meshGroup = project->document()->meshesGroup();
    QString meshId;
    for (const auto& child : meshGroup->children()) {
        auto item = std::dynamic_pointer_cast<FcFdsNamelist>(child);
        if (item && item->keyword() == QStringLiteral("MESH")) meshId = item->id();
    }
    expect(!meshId.isEmpty() && meshGroup->removeChild(meshId), "Negative tutorial fixture removes its actual mesh");
    guide.setContext(project.get(), {}, {}, false);
    expect(!guide.currentStepComplete(&detail) && !next->isEnabled() && detail.contains(QStringLiteral("MESH")),
           "The object gate still blocks a real missing mesh");
}
}

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // Keep a failing isolated probe from blocking the user's desktop with a
    // Windows application-error dialog. This is process-local, not a system
    // WER setting. The debug launcher can still collect the exception stack.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    std::cerr << "STAGE: before QApplication" << std::endl;
    QApplication app(argc, argv);
    std::cerr << "STAGE: QApplication ready" << std::endl;
    const QString platform = QGuiApplication::platformName();
    if (platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal")) {
        std::cerr << "This isolated regression must use QT_QPA_PLATFORM=offscreen or minimal.\n";
        return 2;
    }
    std::cerr << "STAGE: isolated platform=" << platform.toStdString() << std::endl;
    // MainWindow intentionally skips persistence in executables named Tests.
    // This probe uses the actual startup restore path and isolated INI files.
    expect(!QFileInfo(QCoreApplication::applicationFilePath()).baseName().contains(
               QStringLiteral("Tests"), Qt::CaseInsensitive),
           "Probe name exercises production layout persistence");
    QTemporaryDir sandbox;
    expect(sandbox.isValid(), "Private settings/recovery sandbox exists");
    if (!sandbox.isValid()) return 1;
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.LayoutProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("FeedbackDockRegression"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, sandbox.path());
    qputenv("FIRECAE_RECOVERY_DIRECTORY", sandbox.filePath(QStringLiteral("recovery")).toLocal8Bit());
    qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT", "1");
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    qunsetenv("FIRECAE_DISABLE_LAYOUT_RESTORE");
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);

    QRect savedMessages;
    QRect savedInspector;
    {
        std::cerr << "STAGE: construct initial MainWindow" << std::endl;
        MainWindow window;
        std::cerr << "STAGE: show layout only" << std::endl;
        showLayoutOnly(window);
        auto* messages = window.findChild<QDockWidget*>(QStringLiteral("MessagesDock"));
        auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("WorkspaceInspectorDock"));
        expect(messages && inspector, "Actual feedback docks exist");
        if (!messages || !inspector) return 1;
        expect(!messages->isFloating() && !inspector->isFloating(), "Default feedback panels remain docked");
        validate(window);
        expect(!messages->isFloating() && !inspector->isFloating(), "Validation does not undock normal panels");

        for (QDockWidget* dock : {messages, inspector}) {
            dock->hide();
            dock->setFloating(true);
            dock->setMinimumSize(0, 0);
            dock->resize(170, 120);
            dock->move(-4000, -4000);
        }
        validate(window);
        checkRepaired(window, *messages, QSize(480, 260));
        checkRepaired(window, *inspector, QSize(420, 320));

        const QPoint safe = messages->screen()->availableGeometry().topLeft() + QPoint(24, 32);
        messages->resize(600, 300);
        messages->move(safe);
        inspector->resize(450, 350);
        inspector->move(safe + QPoint(45, 65));
        settle();
        savedMessages = messages->geometry();
        savedInspector = inspector->geometry();
        messages->hide();
        inspector->hide();
        validate(window);
        expect(messages->geometry() == savedMessages && inspector->geometry() == savedInspector,
               "Reopening preserves readable, visible user-chosen floating size and position");

        messages->hide();
        messages->move(-4000, -4000);
        validate(window);
        expect(onScreen(*messages) && messages->size() == savedMessages.size(),
               "Off-screen float is recovered without changing a readable size");
        messages->setGeometry(savedMessages);

        messages->setFloating(false);
        inspector->setFloating(false);
        validate(window);
        expect(!messages->isFloating() && !inspector->isFloating(), "User redocking remains docked after validation");
        expect(messages->minimumWidth() < 480 && inspector->minimumWidth() < 420,
               "Floating-only minimums are released on redocking");

        messages->setFloating(true);
        inspector->setFloating(true);
        auto* reset = window.findChild<QAction*>(QStringLiteral("ResetLayoutAction"));
        expect(reset != nullptr, "Reset layout has an identifiable production action");
        if (reset) reset->trigger();
        settle();
        expect(!messages->isFloating() && !inspector->isFloating(), "Reset Layout returns feedback panels to docks");

        messages->setFloating(true);
        inspector->setFloating(true);
        settle();
        messages->setGeometry(savedMessages);
        inspector->setGeometry(savedInspector);
        settle();
        savedMessages = messages->geometry();
        savedInspector = inspector->geometry();
        for (QObject* child : window.findChildren<QObject*>()) {
            if (child->objectName().isEmpty() && !qobject_cast<QDockWidget*>(child)) continue;
            const std::string label = (QString::fromLatin1(child->metaObject()->className()) +
                                      QLatin1Char(' ') + child->objectName()).toStdString();
            QObject::connect(child, &QObject::destroyed, &app, [label]() {
                std::cerr << "DESTROY: " << label << std::endl;
            });
        }
        QObject::connect(&window, &QObject::destroyed, &app, []() {
            std::cerr << "DESTROY: MainWindow reached QObject destruction" << std::endl;
        });
        std::cerr << "STAGE: close initial MainWindow" << std::endl;
        expect(window.close(), "Layout-only window closes through production settings save");
    }
    expect(true, "Initial MainWindow and its floating docks complete normal destruction");
    {
        std::cerr << "STAGE: construct restored MainWindow" << std::endl;
        MainWindow reopened;
        showLayoutOnly(reopened);
        auto* messages = reopened.findChild<QDockWidget*>(QStringLiteral("MessagesDock"));
        auto* inspector = reopened.findChild<QDockWidget*>(QStringLiteral("WorkspaceInspectorDock"));
        expect(messages && inspector && messages->isFloating() && inspector->isFloating(),
               "Startup restores intentionally floating feedback panels");
        if (messages && inspector) {
            expect(messages->geometry() == savedMessages && inspector->geometry() == savedInspector,
                   "Startup preserves valid floating geometry after all queued default-layout work");
            validate(reopened);
            expect(messages->geometry() == savedMessages && inspector->geometry() == savedInspector,
                   "Validation preserves restored user layout");
        }
        reopened.close();
    }
    expect(true, "Restored MainWindow and its floating docks complete normal destruction");
    checkMessageWrapping();
    checkTutorial(QStringLiteral("couch_smoke_12s"), FdsExamples::createCouchSmoke12sProject());
    checkTutorial(QStringLiteral("tunnel_smoke_10s"), FdsExamples::createTunnelSmoke10sProject());
    std::cout << "Feedback layout and tutorial regression: " << checks << " checks, " << failures << " failures.\n";
    return failures == 0 ? 0 : 1;
}
