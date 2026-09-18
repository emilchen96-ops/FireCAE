#include "app/MainWindow.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcProjectSerializer.h"
#include "geometry/FcGeometryObject.h"
#include "modeling/BuildingGeometryService.h"
#include "ui/ModelTreeWidget.h"
#include "ui/ObjectReferenceDialog.h"
#include "ui/UiLanguage.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>

namespace
{
int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

struct Fixture
{
    QString filePath;
    QString wall;
    QString door;
    QString window;
    QString marker;
    QStringList originalOrder;
};

std::shared_ptr<FcGeometryObject> makeGeometry(
    const QString& name, const BuildingGeometryRequest& request)
{
    QString error;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request, &error);
    expect(!shape.IsNull() && error.isEmpty(), "Fixture geometry has a valid production shape");
    auto object = std::make_shared<FcGeometryObject>(name, shape);
    object->setGeometryKind(request.kind);
    object->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    return object;
}

Fixture writeFixture(const QString& directory)
{
    FcProject project(QStringLiteral("Geometry delete reference regression"));
    project.setChid(QStringLiteral("geometry_delete_references"));
    BuildingGeometryRequest wallRequest;
    wallRequest.kind = FcGeometryKind::Wall;
    wallRequest.endX = 6;
    wallRequest.height = 3;
    wallRequest.thickness = 0.25;
    const auto wall = makeGeometry(QStringLiteral("Host wall"), wallRequest);

    BuildingGeometryRequest doorRequest;
    doorRequest.kind = FcGeometryKind::Door;
    doorRequest.x = 1;
    doorRequest.width = 1;
    doorRequest.height = 2;
    const auto door = makeGeometry(
        QStringLiteral("Same-name opening"),
        BuildingGeometryService::attachOpeningToWall(doorRequest, wallRequest));
    door->setHostObjectId(wall->id());

    BuildingGeometryRequest windowRequest = doorRequest;
    windowRequest.kind = FcGeometryKind::Window;
    windowRequest.x = 3;
    windowRequest.z = 1;
    windowRequest.height = 1;
    const auto window = makeGeometry(
        QStringLiteral("Same-name opening"),
        BuildingGeometryService::attachOpeningToWall(windowRequest, wallRequest));
    window->setHostObjectId(wall->id());

    BuildingGeometryRequest markerRequest;
    markerRequest.kind = FcGeometryKind::Box;
    markerRequest.x = 10;
    markerRequest.width = markerRequest.depth = markerRequest.height = 0.5;
    const auto marker = makeGeometry(QStringLiteral("Unselected marker"), markerRequest);

    // An unselected sibling between the host and its openings makes incorrect
    // restoration of sibling indices observable after a compound Undo.
    for (const auto& geometry : {wall, marker, door, window}) {
        project.document()->geometryGroup()->addChild(geometry);
    }
    Fixture fixture;
    fixture.filePath = QDir(directory).filePath(QStringLiteral("fixture.firecae"));
    fixture.wall = wall->id();
    fixture.door = door->id();
    fixture.window = window->id();
    fixture.marker = marker->id();
    fixture.originalOrder = {wall->id(), marker->id(), door->id(), window->id()};
    QString error;
    expect(FcProjectSerializer::save(project, fixture.filePath, &error),
           "Fixture is saved through the production serializer");
    return fixture;
}

QStringList sorted(QStringList values)
{
    std::sort(values.begin(), values.end());
    return values;
}

bool hostReferencesResolve(const FcDocument& document)
{
    bool valid = true;
    const std::function<void(const FcObject::Ptr&)> visit =
        [&](const FcObject::Ptr& object) {
            if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
                if (!geometry->hostObjectId().isEmpty() &&
                    !document.findObject(geometry->hostObjectId())) valid = false;
            }
            for (const auto& child : object->children()) visit(child);
        };
    for (const auto& group : document.groups()) visit(group);
    return valid;
}

void expectIntact(const FcProjectLoadResult& loaded, const Fixture& fixture)
{
    expect(loaded.success(), "Saved controller state reopens successfully");
    if (!loaded.success()) return;
    const auto& document = *loaded.project->document();
    expect(document.findObject(fixture.wall) != nullptr, "Host wall remains in the project");
    expect(document.findObject(fixture.marker) != nullptr, "Unselected sibling remains in the project");
    for (const QString& id : {fixture.door, fixture.window}) {
        const auto opening = std::dynamic_pointer_cast<FcGeometryObject>(document.findObject(id));
        expect(opening && opening->hostObjectId() == fixture.wall,
               "Opening keeps its original host UUID after saving and reopening");
    }
    expect(hostReferencesResolve(document), "Every surviving geometry host UUID resolves");
    QStringList order;
    for (const auto& object : document.geometryGroup()->children()) order.append(object->id());
    expect(order == fixture.originalOrder, "Geometry sibling order is preserved");
}

void expectDeletedTogether(const FcProjectLoadResult& loaded, const Fixture& fixture)
{
    expect(loaded.success(), "State after compound deletion reopens successfully");
    if (!loaded.success()) return;
    const auto& document = *loaded.project->document();
    for (const QString& id : {fixture.wall, fixture.door, fixture.window}) {
        expect(!document.findObject(id), "Selected host and openings are removed together");
    }
    expect(document.findObject(fixture.marker) != nullptr,
           "Compound deletion preserves the unselected sibling");
    expect(document.geometryGroup()->children().size() == 1,
           "Compound deletion removes only the selected geometry set");
    expect(hostReferencesResolve(document), "Compound deletion leaves no dangling host UUID");
}

FcProjectLoadResult snapshot(MainWindow& window, const QString& directory, const QString& name)
{
    const QString filePath = QDir(directory).filePath(name + QStringLiteral(".firecae"));
    expect(window.saveProjectFile(filePath), "Controller state saves after the real action");
    return FcProjectSerializer::load(filePath);
}

struct DeleteOutcome
{
    int referenceDialogs = 0;
    int confirmations = 0;
    QStringList referencingOwners;
    bool defaultIsNo = false;
};

DeleteOutcome triggerDelete(MainWindow& window,
                            QAction& action,
                            QMessageBox::StandardButton confirmation,
                            const QString& locateOwner = {})
{
    DeleteOutcome outcome;
    QTimer modalHandler;
    modalHandler.setInterval(20);
    QObject::connect(&modalHandler, &QTimer::timeout, &window, [&]() {
        auto* active = QApplication::activeModalWidget();
        if (!active || !active->isVisible()) return;
        if (active->objectName() == QStringLiteral("ObjectReferenceDialog")) {
            ++outcome.referenceDialogs;
            auto* dialog = static_cast<ObjectReferenceDialog*>(active);
            auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("ObjectReferenceOwnersTable"));
            int locateRow = -1;
            for (int row = 0; table && row < table->rowCount(); ++row) {
                const QString uuid = table->item(row, 0)->data(Qt::UserRole).toString();
                outcome.referencingOwners.append(uuid);
                if (uuid == locateOwner) locateRow = row;
            }
            if (locateRow >= 0) {
                table->selectRow(locateRow);
                auto* button = dialog->findChild<QPushButton*>(QStringLiteral("ObjectReferenceLocateButton"));
                expect(button != nullptr, "Reference protection exposes a locate-owner button");
                if (button) { button->click(); return; }
            }
            dialog->reject();
            return;
        }
        if (auto* prompt = qobject_cast<QMessageBox*>(active)) {
            ++outcome.confirmations;
            expect(prompt->standardButtons().testFlag(QMessageBox::Yes) &&
                       prompt->standardButtons().testFlag(QMessageBox::No),
                   "Geometry deletion requires an explicit Yes/No choice");
            outcome.defaultIsNo = prompt->defaultButton() == prompt->button(QMessageBox::No);
            // Baseline deletion is deliberately confirmed so the regression
            // observes the dangling reference instead of masking it with No.
            if (auto* button = prompt->button(confirmation)) button->click();
            else prompt->reject();
            return;
        }
        expect(false, "Delete action did not open an unexpected modal dialog");
        if (auto* dialog = qobject_cast<QDialog*>(active)) dialog->reject();
    });
    expect(action.isEnabled(), "The production Delete QAction is enabled for the selection");
    modalHandler.start();
    action.trigger();
    modalHandler.stop();
    QApplication::processEvents();
    return outcome;
}

void runM05SelectionRegression(MainWindow& window, ModelTreeWidget& tree, const Fixture& fixture)
{
    const int firstCheck = checks;
    const int firstFailure = failures;
    std::cout << "CASE: M05 programmatic multi-selection and real Select by Type action\n";
    expect(window.openProjectFile(fixture.filePath), "M05 starts from an intact saved fixture");
    QApplication::processEvents();

    int pluralSignals = 0;
    int singleSignals = 0;
    QStringList notifiedIds;
    const auto pluralConnection = QObject::connect(
        &tree, &ModelTreeWidget::objectsSelected, &window, [&](const QStringList& ids) {
            ++pluralSignals;
            notifiedIds = ids;
        });
    const auto singleConnection = QObject::connect(
        &tree, &ModelTreeWidget::objectSelected, &window, [&](const QString&) {
            ++singleSignals;
        });

    const QStringList pair{fixture.door, fixture.window};
    expect(tree.selectObjectsByIds(pair, true), "M05 public selection method accepts both UUIDs");
    expect(sorted(tree.selectedObjectIds()) == sorted(pair),
           "M05 public selection method retains every requested row");
    expect(pluralSignals == 1 && sorted(notifiedIds) == sorted(pair),
           "M05 notify=true emits one complete multi-selection notification");
    expect(singleSignals == 0, "M05 a two-object request emits no false single-object notification");

    pluralSignals = 0;
    singleSignals = 0;
    const QStringList silentPair{fixture.wall, fixture.marker};
    expect(tree.selectObjectsByIds(silentPair, false), "M05 silent synchronization accepts both UUIDs");
    expect(sorted(tree.selectedObjectIds()) == sorted(silentPair),
           "M05 notify=false preserves the complete synchronized selection");
    expect(pluralSignals == 0 && singleSignals == 0,
           "M05 silent synchronization does not emit selection callbacks");

    expect(tree.selectObjectById(fixture.marker, true), "M05 single-object selection remains available");
    expect(tree.selectedObjectIds() == QStringList{fixture.marker},
           "M05 single-object selection contains only its requested UUID");
    expect(pluralSignals == 1 && singleSignals == 1 && notifiedIds == QStringList{fixture.marker},
           "M05 existing single-object notification behavior is preserved");
    QObject::disconnect(pluralConnection);
    QObject::disconnect(singleConnection);

    QAction* selectByType = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == UiLanguageManager::text(QStringLiteral("Select by Type..."))) {
            selectByType = action;
            break;
        }
    }
    expect(selectByType != nullptr, "M05 actual Select by Type QAction exists");
    if (selectByType) {
        bool inputDialogSeen = false;
        QTimer inputHandler;
        inputHandler.setInterval(20);
        QObject::connect(&inputHandler, &QTimer::timeout, &window, [&]() {
            auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!input) return;
            inputDialogSeen = true;
            input->setTextValue(UiLanguageManager::text(QStringLiteral("Geometry")));
            input->accept();
        });
        inputHandler.start();
        selectByType->trigger();
        inputHandler.stop();
        QApplication::processEvents();
        expect(inputDialogSeen, "M05 real action opens its production type-selection dialog");
        expect(sorted(tree.selectedObjectIds()) == sorted(fixture.originalOrder),
               "M05 MainWindow Select by Type synchronization retains all four geometry UUIDs");
        expect(window.statusBar()->currentMessage().contains(QStringLiteral("4 object(s) selected")),
               "M05 selection summary agrees with the actual four selected tree rows");
    }

    expectIntact(snapshot(window, QFileInfo(fixture.filePath).absolutePath(),
                          QStringLiteral("m05-selection-only")), fixture);
    std::cout << "M05 selection regression: " << checks - firstCheck << " checks, "
              << failures - firstFailure << " failures.\n";
}
QAction* actionWithShortcut(MainWindow& window, const QKeySequence& shortcut)
{
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == shortcut) return action;
    }
    return nullptr;
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir sandbox;
    expect(sandbox.isValid(), "A private test sandbox exists");
    if (!sandbox.isValid()) return 1;
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.Reacceptance.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("GeometryDeleteReferences"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, sandbox.path());
    qputenv("FIRECAE_RECOVERY_DIRECTORY", sandbox.filePath(QStringLiteral("recovery")).toLocal8Bit());
    qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT", "1");
    qunsetenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED");
    qunsetenv("FIRECAE_UI_LANGUAGE");
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);

    QString evidence = qEnvironmentVariable("FIRECAE_GEOMETRY_DELETE_EVIDENCE_DIR");
    if (evidence.isEmpty()) evidence = sandbox.filePath(QStringLiteral("evidence"));
    expect(QDir().mkpath(evidence), "Geometry regression evidence directory exists");
    const Fixture fixture = writeFixture(evidence);
    MainWindow window;
    auto* tree = window.findChild<ModelTreeWidget*>();
    auto* deleteAction = window.findChild<QAction*>(QStringLiteral("DeleteObjectAction"));
    auto* undoStack = window.findChild<QUndoStack*>();
    auto* undoAction = actionWithShortcut(window, QKeySequence::Undo);
    auto* redoAction = actionWithShortcut(window, QKeySequence::Redo);
    expect(tree && deleteAction && undoStack && undoAction && redoAction,
           "Production tree, Delete, Undo and Redo actions exist");
    if (!tree || !deleteAction || !undoStack || !undoAction || !redoAction) return 1;

    const auto openFixture = [&]() {
        expect(window.openProjectFile(fixture.filePath), "MainWindow opens the saved geometry fixture");
        QApplication::processEvents();
        expect(undoStack->count() == 0, "Each independent scenario starts with clean undo history");
    };
    const auto select = [&](const QStringList& ids) {
        auto* widget = tree->findChild<QTreeWidget*>();
        expect(widget != nullptr, "Production tree exposes its actual selection widget");
        if (!widget) return false;
        QList<QTreeWidgetItem*> items;
        for (const QString& id : ids) {
            for (QTreeWidgetItemIterator it(widget); *it; ++it) {
                if ((*it)->data(0, Qt::UserRole).toString() == id) {
                    items.append(*it);
                    break;
                }
            }
        }
        expect(items.size() == ids.size(), "Every requested UUID has a selectable tree row");
        if (items.size() != ids.size() || items.isEmpty()) return false;
        // Set the current row before extending the selection. The convenience
        // selectObjectsByIds method sets it last, which collapses Qt's extended
        // selection; that separate behavior must not invalidate this fixture.
        widget->clearSelection();
        widget->setCurrentItem(items.constFirst());
        for (QTreeWidgetItem* item : items) item->setSelected(true);
        QApplication::processEvents();
        const bool exact = sorted(tree->selectedObjectIds()) == sorted(ids);
        expect(exact, "Tree selection contains the exact deletion set");
        return exact;
    };

    std::cout << "CASE: single host with two external opening references\n";
    openFixture();
    if (!select({fixture.wall})) return 1;
    const auto single = triggerDelete(window, *deleteAction, QMessageBox::Yes);
    expect(single.referenceDialogs == 1 && single.confirmations == 0,
           "Single-wall deletion is blocked before the destructive confirmation");
    expect(sorted(single.referencingOwners) == sorted({fixture.door, fixture.window}),
           "Blocked single deletion lists both same-name opening UUIDs");
    expect(undoStack->count() == 0, "Blocked single deletion adds no undo command");
    expectIntact(snapshot(window, evidence, QStringLiteral("single-blocked")), fixture);
    expect(window.openProjectFile(QDir(evidence).filePath(QStringLiteral("single-blocked.firecae"))),
           "Blocked single-deletion project reopens in the actual controller");
    expectIntact(snapshot(window, evidence, QStringLiteral("single-blocked-reopened")), fixture);

    std::cout << "CASE: selected wall and door with an unselected window reference\n";
    openFixture();
    if (!select({fixture.wall, fixture.door})) return 1;
    const auto partial = triggerDelete(window, *deleteAction, QMessageBox::Yes, fixture.window);
    expect(partial.referenceDialogs == 1 && partial.confirmations == 0,
           "A reference from outside the multi-selection blocks the whole deletion");
    expect(partial.referencingOwners == QStringList{fixture.window},
           "Blocked multi-selection reports only the surviving external owner");
    expect(tree->selectedObjectId() == fixture.window,
           "Locate-owner selects the correct same-name external window UUID");
    expect(undoStack->count() == 0, "Blocked multi-selection adds no partial undo command");
    expectIntact(snapshot(window, evidence, QStringLiteral("partial-blocked")), fixture);

    std::cout << "CASE: complete host/opening set requires confirmation and supports Undo/Redo\n";
    openFixture();
    const QStringList deletionSet{fixture.window, fixture.wall, fixture.door};
    if (!select(deletionSet)) return 1;
    const auto cancelled = triggerDelete(window, *deleteAction, QMessageBox::No);
    expect(cancelled.referenceDialogs == 0 && cancelled.confirmations == 1 && cancelled.defaultIsNo,
           "A closed deletion set has one confirmation that defaults to No");
    expect(undoStack->count() == 0, "Cancelling complete-set deletion adds no undo command");
    expectIntact(snapshot(window, evidence, QStringLiteral("complete-cancelled")), fixture);

    if (!select(deletionSet)) return 1;
    const auto accepted = triggerDelete(window, *deleteAction, QMessageBox::Yes);
    expect(accepted.referenceDialogs == 0 && accepted.confirmations == 1,
           "Host plus every referencing opening can be explicitly deleted together");
    expect(undoStack->count() == 1 && undoStack->index() == 1,
           "Complete-set deletion is one atomic undo command");
    expectDeletedTogether(snapshot(window, evidence, QStringLiteral("complete-deleted")), fixture);

    expect(undoAction->isEnabled(), "Production Undo action is enabled after deletion");
    undoAction->trigger();
    QApplication::processEvents();
    expect(undoStack->index() == 0, "Undo restores the complete set in one step");
    expectIntact(snapshot(window, evidence, QStringLiteral("complete-undone")), fixture);
    expect(redoAction->isEnabled(), "Production Redo action is enabled after Undo");
    redoAction->trigger();
    QApplication::processEvents();
    expect(undoStack->index() == 1, "Redo deletes the complete set in one step");
    expectDeletedTogether(snapshot(window, evidence, QStringLiteral("complete-redone")), fixture);

    undoAction->trigger();
    QApplication::processEvents();
    expectIntact(snapshot(window, evidence, QStringLiteral("complete-restored")), fixture);
    expect(window.openProjectFile(QDir(evidence).filePath(QStringLiteral("complete-restored.firecae"))),
           "Undo-restored project reopens in the actual controller");
    expectIntact(snapshot(window, evidence, QStringLiteral("complete-restored-reopened")), fixture);

    std::cout << "M02 geometry deletion regression: " << checks << " checks, " << failures << " failures.\n";
    runM05SelectionRegression(window, *tree, fixture);
    std::cout << "Geometry UI regression total: " << checks << " checks, " << failures << " failures.\n";
    return failures == 0 ? 0 : 1;
}
