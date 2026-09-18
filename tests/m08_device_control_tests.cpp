// Production device/control wizard, persistence, and validation regression tests.
#include "app/MainWindow.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsExamples.h"
#include "fds/FdsSchema.h"
#include "fds/FdsWriter.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <memory>
#include <utility>

namespace
{
int fail(const QString& message)
{
    std::cerr << "M08: " << message.toStdString() << '\n';
    return 1;
}

FcFdsParameter parameter(const FcFdsNamelist& object, const QString& key)
{
    for (const auto& value : object.parameters())
        if (value.key.compare(key, Qt::CaseInsensitive) == 0) return value;
    return {};
}

bool equalParameters(const std::vector<FcFdsParameter>& a, const std::vector<FcFdsParameter>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].key != b[i].key || a[i].kind != b[i].kind || a[i].value != b[i].value ||
            a[i].targetObjectIds != b[i].targetObjectIds) return false;
    return true;
}

void collect(const FcObject::Ptr& object, std::vector<std::shared_ptr<FcFdsNamelist>>& records,
             QSet<QString>& ids)
{
    if (!object) return;
    ids.insert(object->id());
    if (auto record = std::dynamic_pointer_cast<FcFdsNamelist>(object)) records.push_back(record);
    for (const auto& child : object->children()) collect(child, records, ids);
}

std::vector<std::shared_ptr<FcFdsNamelist>> records(const FcProject& project)
{
    std::vector<std::shared_ptr<FcFdsNamelist>> result;
    QSet<QString> ignored;
    for (const auto& group : project.document()->groups()) collect(group, result, ignored);
    return result;
}

QSet<QString> allIds(const FcProject& project)
{
    std::vector<std::shared_ptr<FcFdsNamelist>> ignored;
    QSet<QString> result;
    for (const auto& group : project.document()->groups()) collect(group, ignored, result);
    return result;
}

std::shared_ptr<FcFdsNamelist> byId(const FcProject& project, const QString& uuid)
{
    return std::dynamic_pointer_cast<FcFdsNamelist>(project.document()->findObject(uuid));
}

std::shared_ptr<FcFdsNamelist> byFdsId(const FcProject& project, const QString& id)
{
    for (const auto& record : records(project)) if (record->fdsId() == id) return record;
    return {};
}

bool sameModel(const FcProject& a, const FcProject& b)
{
    if (allIds(a) != allIds(b)) return false;
    if (a.activeScenarioId() != b.activeScenarioId() || a.defaultScenarioId() != b.defaultScenarioId() ||
        a.scenarios().size() != b.scenarios().size()) return false;
    for (const auto& scenario : a.scenarios()) {
        const auto* other = b.scenario(scenario.id);
        if (!other || scenario.name != other->name || scenario.chid != other->chid ||
            scenario.outputDirectory != other->outputDirectory || scenario.solverBackendId != other->solverBackendId ||
            scenario.processCount != other->processCount || scenario.disabledObjectIds != other->disabledObjectIds ||
            scenario.parameterOverrides.size() != other->parameterOverrides.size()) return false;
        for (int index = 0; index < scenario.parameterOverrides.size(); ++index) {
            const auto& left = scenario.parameterOverrides[index];
            const auto& right = other->parameterOverrides[index];
            if (left.objectId != right.objectId || left.parameterKey != right.parameterKey || left.value != right.value ||
                left.reference != right.reference || left.targetObjectIds != right.targetObjectIds) return false;
        }
    }
    for (const auto& record : records(a)) {
        const auto other = byId(b, record->id());
        if (!other || record->name() != other->name() || record->keyword() != other->keyword() ||
            record->fdsId() != other->fdsId() || !equalParameters(record->parameters(), other->parameters()))
            return false;
    }
    return true;
}

struct Fixture
{
    std::unique_ptr<FcProject> project;
    std::shared_ptr<FcFdsNamelist> target;
};

Fixture fixture(const QString& keyword)
{
    Fixture result{FdsExamples::createSimpleTestProject(), {}};
    auto oldDevice = std::make_shared<FcFdsNamelist>(QStringLiteral("Existing sensor"),
        FcObjectType::Device, QStringLiteral("DEVC"), QStringLiteral("M08_OLD_DEVC"), 200);
    oldDevice->addStringParameter(QStringLiteral("QUANTITY"), QStringLiteral("TEMPERATURE"));
    oldDevice->addRawParameter(QStringLiteral("XYZ"), QStringLiteral("1,1,1"));
    oldDevice->addRawParameter(QStringLiteral("SETPOINT"), QStringLiteral("100"));
    result.project->document()->devicesGroup()->addChild(oldDevice);
    result.target = std::make_shared<FcFdsNamelist>(QStringLiteral("Controlled target"),
        keyword == QStringLiteral("OBST") ? FcObjectType::Obstruction : FcObjectType::Vent,
        keyword, QStringLiteral("M08_TARGET"), 201);
    result.target->addRawParameter(QStringLiteral("XB"), keyword == QStringLiteral("OBST")
        ? QStringLiteral("1,1.2,1,1.2,0,0.5") : QStringLiteral("0,0,0.2,0.8,0.2,0.8"));
    if (keyword == QStringLiteral("VENT"))
        result.target->addStringParameter(QStringLiteral("SURF_ID"), QStringLiteral("BURNER"));
    result.target->addStringParameter(QStringLiteral("FYI"), QStringLiteral("preserve_target_note"));
    result.target->addReferenceParameter(QStringLiteral("DEVC_ID"), {oldDevice->id()});
    (keyword == QStringLiteral("OBST") ? result.project->document()->geometryGroup()
                                       : result.project->document()->ventsGroup())->addChild(result.target);
    return result;
}

void replaceParameter(FcFdsNamelist& object, FcFdsParameter value)
{
    auto values = object.parameters();
    for (auto& existing : values) {
        if (existing.key == value.key) {
            existing = std::move(value);
            object.setParameters(values);
            return;
        }
    }
    values.push_back(std::move(value));
    object.setParameters(values);
}

void removeParameter(FcFdsNamelist& object, const QString& key)
{
    std::vector<FcFdsParameter> values;
    for (const auto& value : object.parameters()) if (value.key != key) values.push_back(value);
    object.setParameters(values);
}

struct Plan
{
    QString targetUuid;
    QString deviceId;
    QString controlId;
    bool activate = true;
    int route = 0; // direct DEVC, ANY, TIME_DELAY
    int tripDirection = 1;
    bool cancel = false;
};

struct WizardResult
{
    bool visited = false;
    bool clickedOk = false;
    bool clickedCancel = false;
    bool noTargetControlsDisabled = false;
    QString driverError;
    QString warning;
};

WizardResult driveWizard(MainWindow& window, const Plan& plan)
{
    WizardResult result;
    auto* action = window.findChild<QAction*>(QStringLiteral("DeviceControlWizardAction"));
    if (!action) { result.driverError = QStringLiteral("Production action missing."); return result; }
    bool configured = false;
    QTimer driver;
    QTimer warningCloser;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&warningCloser, &QTimer::timeout, &window, [&]() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            result.warning = box->text();
            box->reject();
        }
    });
    QObject::connect(&watchdog, &QTimer::timeout, &window, [&]() {
        result.driverError = QStringLiteral("Wizard driver exceeded its 15-second bound.");
        if (auto* active = qobject_cast<QDialog*>(QApplication::activeModalWidget())) active->reject();
    });
    QObject::connect(&driver, &QTimer::timeout, &window, [&]() {
        if (qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) return;
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (dialog->objectName() != QStringLiteral("DeviceControlWizardDialog")) {
            result.driverError = QStringLiteral("Unexpected modal dialog.");
            dialog->reject();
            return;
        }
        result.visited = true;
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!buttons) { result.driverError = QStringLiteral("Button box missing."); dialog->reject(); return; }
        if (configured) {
            // An unsuccessful OK leaves the wizard open after its validation warning.
            result.clickedCancel = true;
            buttons->button(QDialogButtonBox::Cancel)->click();
            return;
        }
        configured = true;
        // The OK handler may synchronously enter a warning's nested event loop.
        // Stop this driver; an independent timer closes that warning.
        driver.stop();
        auto* target = dialog->findChild<QComboBox*>(QStringLiteral("DeviceControlTargetCombo"));
        auto* create = dialog->findChild<QCheckBox*>(QStringLiteral("DeviceControlCreateControlCheck"));
        auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("DeviceControlDelaySpin"));
        auto* operation = dialog->findChild<QComboBox*>(QStringLiteral("DeviceControlActionCombo"));
        auto* direction = dialog->findChild<QComboBox*>(QStringLiteral("DeviceControlTripDirectionCombo"));
        auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("DeviceControlNameEdit"));
        auto* device = dialog->findChild<QLineEdit*>(QStringLiteral("DeviceControlDeviceIdEdit"));
        auto* control = dialog->findChild<QLineEdit*>(QStringLiteral("DeviceControlControlIdEdit"));
        auto* type = dialog->findChild<QComboBox*>(QStringLiteral("DeviceControlTypeCombo"));
        if (!target || !create || !delay || !operation || !direction || !name || !device || !control || !type) {
            result.driverError = QStringLiteral("A stable production selector is missing.");
            buttons->button(QDialogButtonBox::Cancel)->click();
            return;
        }
        const int targetIndex = target->findData(plan.targetUuid);
        const int actionIndex = operation->findData(plan.activate);
        const int directionIndex = direction->findData(plan.tripDirection);
        const int typeIndex = type->findData(QStringLiteral("TEMPERATURE"));
        if (targetIndex < 0 || actionIndex < 0 || directionIndex < 0 || typeIndex < 0) {
            result.driverError = QStringLiteral("A requested target/action/direction/type was not offered.");
            buttons->button(QDialogButtonBox::Cancel)->click();
            return;
        }
        target->setCurrentIndex(targetIndex);
        create->setChecked(plan.route != 0);
        operation->setCurrentIndex(actionIndex);
        direction->setCurrentIndex(directionIndex);
        type->setCurrentIndex(typeIndex);
        delay->setValue(plan.route == 2 ? 2.5 : 0.0);
        name->setText(plan.deviceId);
        device->setText(plan.deviceId);
        control->setText(plan.controlId);
        for (const auto& pair : {std::pair<const char*, double>{"DeviceControlXSpin", 1.0},
                                {"DeviceControlYSpin", 1.0}, {"DeviceControlZSpin", 1.0},
                                {"DeviceControlSetpointSpin", 55.0}}) {
            auto* spin = dialog->findChild<QDoubleSpinBox*>(QString::fromLatin1(pair.first));
            if (!spin) {
                result.driverError = QStringLiteral("A detector input is missing.");
                buttons->button(QDialogButtonBox::Cancel)->click();
                return;
            }
            spin->setValue(pair.second);
        }
        if (plan.targetUuid.isEmpty())
            result.noTargetControlsDisabled = !create->isEnabled() && !delay->isEnabled() && !operation->isEnabled();
        if (plan.cancel) {
            result.clickedCancel = true;
            buttons->button(QDialogButtonBox::Cancel)->click();
        } else {
            result.clickedOk = true;
            // This invokes DeviceControlWizardDialog::accept and its validation.
            buttons->button(QDialogButtonBox::Ok)->click();
            if (dialog->isVisible()) {
                result.clickedCancel = true;
                buttons->button(QDialogButtonBox::Cancel)->click();
            }
        }
    });
    warningCloser.start(10);
    watchdog.start(15000);
    driver.start(10);
    action->trigger();
    driver.stop();
    warningCloser.stop();
    watchdog.stop();
    QApplication::processEvents();
    return result;
}

FcProjectLoadResult snapshot(MainWindow& window, const QString& path)
{
    if (!window.saveProjectFile(path)) return {};
    return FcProjectSerializer::load(path);
}

QAction* historyAction(MainWindow& window, QKeySequence::StandardKey key)
{
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcut().matches(QKeySequence(key)) == QKeySequence::ExactMatch) return action;
    return nullptr;
}

QString verifyGenerated(const FcProject& project, const Plan& plan, const QString& targetUuid,
                        const std::vector<FcFdsParameter>& targetBefore)
{
    const auto target = byId(project, targetUuid);
    const auto device = byFdsId(project, plan.deviceId);
    const auto control = byFdsId(project, plan.controlId);
    if (!target || !device || device->keyword() != QStringLiteral("DEVC"))
        return QStringLiteral("Generated target or device missing.");
    if (parameter(*device, QStringLiteral("QUANTITY")).value != QStringLiteral("TEMPERATURE") ||
        parameter(*device, QStringLiteral("XYZ")).value != QStringLiteral("1,1,1") ||
        parameter(*device, QStringLiteral("SETPOINT")).value.toDouble() != 55.0 ||
        parameter(*device, QStringLiteral("TRIP_DIRECTION")).value.toInt() != plan.tripDirection)
        return QStringLiteral("Detector values/direction were not generated as selected.");
    if (!parameter(*target, QStringLiteral("INITIAL_STATE")).key.isEmpty())
        return QStringLiteral("INITIAL_STATE is illegally attached to the target.");
    const QString ownerKey = plan.route ? QStringLiteral("CTRL_ID") : QStringLiteral("DEVC_ID");
    const QString otherKey = plan.route ? QStringLiteral("DEVC_ID") : QStringLiteral("CTRL_ID");
    const auto targetReference = parameter(*target, ownerKey);
    const auto owner = plan.route ? control : device;
    if (!owner || targetReference.kind != FcFdsParameterKind::ObjectReferences ||
        targetReference.targetObjectIds != QStringList{owner->id()} ||
        !parameter(*target, otherKey).key.isEmpty())
        return QStringLiteral("Target/controller/device UUID relationship is wrong.");
    const QString expectedState = plan.activate ? QStringLiteral(".FALSE.") : QStringLiteral(".TRUE.");
    if (parameter(*owner, QStringLiteral("INITIAL_STATE")).value != expectedState)
        return QStringLiteral("Initial state is not on the selected state owner.");
    for (const auto& before : targetBefore) {
        if (before.key == QStringLiteral("CTRL_ID") || before.key == QStringLiteral("DEVC_ID") ||
            before.key == QStringLiteral("INITIAL_STATE")) continue;
        if (!equalParameters({before}, {parameter(*target, before.key)}))
            return QStringLiteral("Unrelated target data changed.");
    }
    if (plan.route) {
        const auto deviceState = parameter(*device, QStringLiteral("INITIAL_STATE"));
        if ((!deviceState.key.isEmpty() && deviceState.value != QStringLiteral(".FALSE.")) ||
            control->keyword() != QStringLiteral("CTRL") ||
            parameter(*control, QStringLiteral("INPUT_ID")).kind != FcFdsParameterKind::ObjectReferences ||
            parameter(*control, QStringLiteral("INPUT_ID")).targetObjectIds != QStringList{device->id()})
            return QStringLiteral("CTRL input UUID or detector default state is wrong.");
        if (parameter(*control, QStringLiteral("FUNCTION_TYPE")).value !=
            (plan.route == 2 ? QStringLiteral("TIME_DELAY") : QStringLiteral("ANY")))
            return QStringLiteral("Control function type is wrong.");
        const auto delay = parameter(*control, QStringLiteral("DELAY"));
        if (!parameter(*control, QStringLiteral("CONSTANT")).key.isEmpty() ||
            (plan.route == 2 && (delay.key.isEmpty() || delay.value.toDouble() != 2.5)) ||
            (plan.route == 1 && !delay.key.isEmpty() && delay.value.toDouble() != 0.0))
            return QStringLiteral("Delay is missing, misplaced on CONSTANT, or nonzero for ANY.");
    } else if (control) return QStringLiteral("Direct DEVC route unexpectedly created CTRL.");
    const auto output = FdsWriter::render(project);
    if (!output.success()) return output.errors.join(QLatin1Char('\n'));
    const QString targetLineId = QStringLiteral("ID='M08_TARGET'");
    bool foundTarget = false;
    for (const auto& line : output.text.split(QLatin1Char('\n'))) {
        if (line.contains(targetLineId)) {
            foundTarget = true;
            if (line.contains(QStringLiteral("INITIAL_STATE=")) ||
                !line.contains(ownerKey + QStringLiteral("='") + owner->fdsId() + QLatin1Char('\'')))
                return QStringLiteral("Serialized FDS target line does not match UUID-derived semantics.");
        }
    }
    return foundTarget ? QString{} : QStringLiteral("Exported target not found.");
}
}

int runM08DeviceControlRegression()
{
    QTemporaryDir directory;
    if (!directory.isValid()) return fail(QStringLiteral("Temporary directory missing."));
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();
    int combinations = 0;
    for (const QString& keyword : {QStringLiteral("OBST"), QStringLiteral("VENT")}) {
        for (const bool activate : {false, true}) {
            for (int route = 0; route < 3; ++route) {
                const QString label = QStringLiteral("%1_%2_%3").arg(keyword).arg(activate ? 1 : 0).arg(route);
                auto seed = fixture(keyword);
                if (!FdsWriter::render(*seed.project).success()) return fail(label + QStringLiteral(": invalid baseline fixture."));
                const QString targetUuid = seed.target->id();
                const auto beforeParameters = seed.target->parameters();
                const auto beforeIds = allIds(*seed.project);
                const QString input = directory.filePath(label + QStringLiteral("_before.firecae"));
                if (!FcProjectSerializer::save(*seed.project, input) || !window.openProjectFile(input))
                    return fail(label + QStringLiteral(": could not open baseline."));
                Plan plan{targetUuid, QStringLiteral("M08_DEVC_") + label, QStringLiteral("M08_CTRL_") + label,
                          activate, route, route == 1 ? -1 : 1, false};
                const auto driven = driveWizard(window, plan);
                if (!driven.visited || !driven.clickedOk || !driven.driverError.isEmpty() || !driven.warning.isEmpty())
                    return fail(label + QStringLiteral(": production wizard rejected legal plan: ") + driven.driverError + driven.warning);
                const QString afterPath = directory.filePath(label + QStringLiteral("_after.firecae"));
                auto after = snapshot(window, afterPath);
                if (!after.success()) return fail(label + QStringLiteral(": save/load failed."));
                QString error = verifyGenerated(*after.project, plan, targetUuid, beforeParameters);
                if (!error.isEmpty()) return fail(label + QStringLiteral(": ") + error);
                const auto device = byFdsId(*after.project, plan.deviceId);
                const auto control = byFdsId(*after.project, plan.controlId);
                auto expectedIds = beforeIds;
                expectedIds.insert(device->id());
                if (control) expectedIds.insert(control->id());
                if (allIds(*after.project) != expectedIds)
                    return fail(label + QStringLiteral(": unexpected object creation/deletion."));

                const bool representative = (keyword == QStringLiteral("OBST") && activate && route == 0) ||
                                            (keyword == QStringLiteral("VENT") && !activate && route == 2);
                if (representative) {
                    auto* undo = historyAction(window, QKeySequence::Undo);
                    auto* redo = historyAction(window, QKeySequence::Redo);
                    if (!undo || !redo || !undo->isEnabled()) return fail(label + QStringLiteral(": undo action unavailable."));
                    undo->trigger();
                    QApplication::processEvents();
                    auto undone = snapshot(window, directory.filePath(label + QStringLiteral("_undo.firecae")));
                    if (!undone.success() || !sameModel(*seed.project, *undone.project))
                        return fail(label + QStringLiteral(": undo did not restore original parameters/UUID objects."));
                    if (!redo->isEnabled()) return fail(label + QStringLiteral(": redo action unavailable."));
                    redo->trigger();
                    QApplication::processEvents();
                    auto redone = snapshot(window, directory.filePath(label + QStringLiteral("_redo.firecae")));
                    if (!redone.success() || !sameModel(*after.project, *redone.project))
                        return fail(label + QStringLiteral(": redo regenerated or lost UUIDs/parameters."));
                    Plan cancelled = plan;
                    cancelled.deviceId += QStringLiteral("_CANCELLED");
                    cancelled.controlId += QStringLiteral("_CANCELLED");
                    cancelled.cancel = true;
                    const auto cancelledResult = driveWizard(window, cancelled);
                    auto afterCancel = snapshot(window, directory.filePath(label + QStringLiteral("_cancel.firecae")));
                    if (!cancelledResult.visited || !cancelledResult.clickedCancel || !cancelledResult.driverError.isEmpty() ||
                        !afterCancel.success() || !sameModel(*redone.project, *afterCancel.project))
                        return fail(label + QStringLiteral(": cancelling changed the project."));
                }
                if (!window.openProjectFile(afterPath)) return fail(label + QStringLiteral(": actual window reopen failed."));
                auto reopened = snapshot(window, directory.filePath(label + QStringLiteral("_reopen.firecae")));
                if (!reopened.success() || !sameModel(*after.project, *reopened.project) ||
                    !verifyGenerated(*reopened.project, plan, targetUuid, beforeParameters).isEmpty())
                    return fail(label + QStringLiteral(": UUID/state/delay semantics changed after reopening."));
                const QString exportPath = directory.filePath(label + QStringLiteral(".fds"));
                if (!window.exportCurrentProjectToFds(exportPath)) return fail(label + QStringLiteral(": production export failed."));
                QFile exported(exportPath);
                if (!exported.open(QIODevice::ReadOnly) ||
                    QString::fromUtf8(exported.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) !=
                        FdsWriter::render(*reopened.project).text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")))
                    return fail(label + QStringLiteral(": actual exported bytes disagree with the saved model."));
                ++combinations;
            }
        }
    }
    if (combinations != 12) return fail(QStringLiteral("The full 12-combination matrix was not covered."));

    // No-target ordinary detector creation is a separate positive control.
    auto noTarget = fixture(QStringLiteral("OBST"));
    const QString noTargetPath = directory.filePath(QStringLiteral("no_target_before.firecae"));
    if (!FcProjectSerializer::save(*noTarget.project, noTargetPath) || !window.openProjectFile(noTargetPath))
        return fail(QStringLiteral("No-target baseline could not be loaded."));
    Plan ordinary{{}, QStringLiteral("M08_PLAIN_SENSOR"), QStringLiteral("M08_UNUSED_CTRL"), true, 0, -1, false};
    const auto ordinaryResult = driveWizard(window, ordinary);
    auto ordinaryAfter = snapshot(window, directory.filePath(QStringLiteral("no_target_after.firecae")));
    if (!ordinaryResult.clickedOk || !ordinaryResult.noTargetControlsDisabled ||
        !ordinaryResult.driverError.isEmpty() || !ordinaryResult.warning.isEmpty() || !ordinaryAfter.success())
        return fail(QStringLiteral("Ordinary detector route failed or enabled target-only controls."));
    const auto ordinaryDevice = byFdsId(*ordinaryAfter.project, ordinary.deviceId);
    const auto untouchedTarget = byId(*ordinaryAfter.project, noTarget.target->id());
    if (!ordinaryDevice || byFdsId(*ordinaryAfter.project, ordinary.controlId) || !untouchedTarget ||
        !equalParameters(untouchedTarget->parameters(), noTarget.target->parameters()) ||
        !parameter(*ordinaryDevice, QStringLiteral("INITIAL_STATE")).key.isEmpty() ||
        parameter(*ordinaryDevice, QStringLiteral("TRIP_DIRECTION")).value != QStringLiteral("-1") ||
        !FdsWriter::render(*ordinaryAfter.project).success())
        return fail(QStringLiteral("No-target creation changed target data or ordinary detector semantics."));

    // The same target checker protects direct DEVC, CTRL and writer callers.
    const auto expectRejected = [&](Fixture& seed, const QString& label, const QString& expected,
                                    bool writerMustReject, int route = 0) -> QString {
        const auto targetErrors = FdsWriter::validateControlledTarget(*seed.project, seed.target->id());
        const auto output = FdsWriter::render(*seed.project);
        if (writerMustReject) {
            if (!targetErrors.join(QLatin1Char('\n')).contains(expected) || output.success() ||
                !output.errors.join(QLatin1Char('\n')).contains(expected))
                return label + QStringLiteral(": checker/writer did not identify the prohibited target.");
            const QString denied = directory.filePath(label + QStringLiteral("_denied.fds"));
            QString error;
            if (FdsWriter::writeFile(*seed.project, denied, &error) || QFileInfo::exists(denied))
                return label + QStringLiteral(": invalid target created an export file.");
        } else if (!targetErrors.isEmpty() || !output.success()) {
            return label + QStringLiteral(": legal existing scenario binding must still export.");
        }
        const QString input = directory.filePath(label + QStringLiteral("_before.firecae"));
        if (!FcProjectSerializer::save(*seed.project, input) || !window.openProjectFile(input))
            return label + QStringLiteral(": rejected-target fixture could not be opened.");
        Plan plan{seed.target->id(), QStringLiteral("M08_REJECT_") + label,
                  QStringLiteral("M08_REJECT_CTRL_") + label, true, route, 1, false};
        const auto result = driveWizard(window, plan);
        auto after = snapshot(window, directory.filePath(label + QStringLiteral("_after.firecae")));
        if (!result.visited || !result.clickedOk || !result.driverError.isEmpty() ||
            !result.warning.contains(expected) || !after.success() || !sameModel(*seed.project, *after.project))
            return label + QStringLiteral(": actual wizard OK did not reject without changing data: ") +
                result.driverError + result.warning;
        return {};
    };

    int boundaryIndex = 0;
    for (const QString& boundary : {QStringLiteral("OPEN"), QStringLiteral("MIRROR"),
                                   QStringLiteral("PERIODIC"), QStringLiteral("PERIODIC FLOW ONLY")}) {
        for (const bool uuidReference : {false, true}) {
            auto seed = fixture(QStringLiteral("VENT"));
            if (uuidReference) {
                auto surface = std::make_shared<FcFdsNamelist>(QStringLiteral("Builtin surface identity"),
                    FcObjectType::Surface, QStringLiteral("SURF"), boundary, 202);
                seed.project->document()->surfacesGroup()->addChild(surface);
                replaceParameter(*seed.target, {QStringLiteral("SURF_ID"), FcFdsParameterKind::ObjectReferences, {}, {surface->id()}});
            } else {
                replaceParameter(*seed.target, {QStringLiteral("SURF_ID"), FcFdsParameterKind::String, boundary, {}});
            }
            const QString label = QStringLiteral("BOUNDARY_%1").arg(boundaryIndex++);
            const QString error = expectRejected(seed, label,
                QStringLiteral("cannot be controlled by DEVC_ID or CTRL_ID"), true,
                uuidReference ? 2 : 0);
            if (!error.isEmpty()) return fail(error);
        }
    }

    for (const bool uuidOverride : {false, true}) {
        auto seed = fixture(QStringLiteral("VENT"));
        const QString scenario = seed.project->addScenario(QStringLiteral("Blocked effective surface"));
        FcScenarioParameterOverride overrideValue;
        overrideValue.objectId = seed.target->id();
        overrideValue.parameterKey = QStringLiteral("SURF_ID");
        if (uuidOverride) {
            auto surface = std::make_shared<FcFdsNamelist>(QStringLiteral("Open by UUID"),
                FcObjectType::Surface, QStringLiteral("SURF"), QStringLiteral("OPEN"), 202);
            seed.project->document()->surfacesGroup()->addChild(surface);
            overrideValue.reference = true;
            overrideValue.targetObjectIds = {surface->id()};
        } else overrideValue.value = QStringLiteral("'OPEN'");
        if (!seed.project->setScenarioOverride(scenario, overrideValue) || !seed.project->setActiveScenario(scenario))
            return fail(QStringLiteral("Could not create active SURF_ID override."));
        const QString error = expectRejected(seed, uuidOverride ? QStringLiteral("SURF_OVERRIDE_UUID") : QStringLiteral("SURF_OVERRIDE_RAW"),
            QStringLiteral("cannot be controlled by DEVC_ID or CTRL_ID"), true);
        if (!error.isEmpty()) return fail(error);
    }

    // Existing valid scenario control bindings may export, but rebinding via the wizard must not be masked.
    for (const QString& key : {QStringLiteral("DEVC_ID"), QStringLiteral("CTRL_ID")}) {
        auto seed = fixture(QStringLiteral("VENT"));
        const auto oldDevice = byFdsId(*seed.project, QStringLiteral("M08_OLD_DEVC"));
        QString ownerUuid = oldDevice->id();
        if (key == QStringLiteral("CTRL_ID")) {
            auto oldControl = std::make_shared<FcFdsNamelist>(QStringLiteral("Existing controller"),
                FcObjectType::Control, QStringLiteral("CTRL"), QStringLiteral("M08_OLD_CTRL"), 202);
            oldControl->addStringParameter(QStringLiteral("FUNCTION_TYPE"), QStringLiteral("ANY"));
            oldControl->addReferenceParameter(QStringLiteral("INPUT_ID"), {oldDevice->id()});
            seed.project->document()->controlsGroup()->addChild(oldControl);
            ownerUuid = oldControl->id();
            removeParameter(*seed.target, QStringLiteral("DEVC_ID"));
        }
        const QString scenario = seed.project->addScenario(QStringLiteral("Existing valid binding"));
        FcScenarioParameterOverride entry{seed.target->id(), key, {}, {ownerUuid}, true};
        if (!seed.project->setScenarioOverride(scenario, entry) || !seed.project->setActiveScenario(scenario))
            return fail(QStringLiteral("Could not create active control override."));
        const QString error = expectRejected(seed, QStringLiteral("CONTROL_OVERRIDE_") + key,
            QStringLiteral("overrides target ") + key, false);
        if (!error.isEmpty()) return fail(error);
    }

    // Unrelated/ordinary surface overrides must not over-block the production wizard.
    auto allowed = fixture(QStringLiteral("VENT"));
    const QString allowedScenario = allowed.project->addScenario(QStringLiteral("Ordinary override"));
    if (!allowed.project->setScenarioOverride(allowedScenario,
            {allowed.target->id(), QStringLiteral("SURF_ID"), QStringLiteral("'BURNER'"), {}, false}) ||
        !allowed.project->setScenarioOverride(allowedScenario,
            {allowed.target->id(), QStringLiteral("FYI"), QStringLiteral("'scenario_note'"), {}, false}) ||
        !allowed.project->setActiveScenario(allowedScenario) ||
        !FdsWriter::validateControlledTarget(*allowed.project, allowed.target->id()).isEmpty())
        return fail(QStringLiteral("Ordinary surface/unrelated overrides were rejected."));
    const QString allowedPath = directory.filePath(QStringLiteral("allowed_override.firecae"));
    if (!FcProjectSerializer::save(*allowed.project, allowedPath) || !window.openProjectFile(allowedPath))
        return fail(QStringLiteral("Allowed-override fixture could not open."));
    Plan allowedPlan{allowed.target->id(), QStringLiteral("M08_ALLOWED_DEVC"), QStringLiteral("M08_ALLOWED_CTRL"), false, 1, 1, false};
    const auto allowedResult = driveWizard(window, allowedPlan);
    auto allowedAfter = snapshot(window, directory.filePath(QStringLiteral("allowed_override_after.firecae")));
    if (!allowedResult.clickedOk || !allowedResult.driverError.isEmpty() || !allowedResult.warning.isEmpty() ||
        !allowedAfter.success() || !verifyGenerated(*allowedAfter.project, allowedPlan, allowed.target->id(),
                                                   allowed.target->parameters()).isEmpty())
        return fail(QStringLiteral("Legal overrides no longer permit a new control binding."));

    auto conflicting = fixture(QStringLiteral("VENT"));
    auto ambiguousSurface = std::make_shared<FcFdsNamelist>(QStringLiteral("Ambiguous surface"),
        FcObjectType::Surface, QStringLiteral("SURF"), QStringLiteral("M08_ORDINARY_SURF"), 202);
    ambiguousSurface->addStringParameter(QStringLiteral("ID"), QStringLiteral("OPEN"));
    conflicting.project->document()->surfacesGroup()->addChild(ambiguousSurface);
    replaceParameter(*conflicting.target, {QStringLiteral("SURF_ID"), FcFdsParameterKind::ObjectReferences, {}, {ambiguousSurface->id()}});
    const QString conflictError = expectRejected(conflicting, QStringLiteral("SURF_ID_CONFLICT"),
        QStringLiteral("conflicting raw ID parameter"), true);
    if (!conflictError.isEmpty()) return fail(conflictError);

    auto illegalState = fixture(QStringLiteral("VENT"));
    illegalState.target->addRawParameter(QStringLiteral("INITIAL_STATE"), QStringLiteral(".TRUE."));
    const auto illegalOutput = FdsWriter::render(*illegalState.project);
    if (illegalOutput.success() || !illegalOutput.errors.join(QLatin1Char('\n')).contains(QStringLiteral("INITIAL_STATE")) ||
        !illegalState.target->validate().join(QLatin1Char('\n')).contains(QStringLiteral("INITIAL_STATE")))
        return fail(QStringLiteral("Known-invalid VENT INITIAL_STATE was not rejected by model/writer."));
    for (const QString& version : FdsSchemaRegistry::supportedVersions()) {
        std::vector<FcFdsParameter> controlParameters{
            {QStringLiteral("FUNCTION_TYPE"), FcFdsParameterKind::String, QStringLiteral("TIME_DELAY"), {}},
            {QStringLiteral("INPUT_ID"), FcFdsParameterKind::ObjectReferences, {}, {QStringLiteral("fixture-device-uuid")}},
            {QStringLiteral("DELAY"), FcFdsParameterKind::Raw, QStringLiteral("2.5"), {}}};
        if (!FdsSchemaRegistry::parameter(QStringLiteral("CTRL"), QStringLiteral("DELAY"), version) ||
            !FdsSchemaRegistry::validate(QStringLiteral("CTRL"), QStringLiteral("M08_CTRL"), controlParameters, version).isEmpty())
            return fail(QStringLiteral("Valid DELAY was not exposed/accepted for a supported version."));
        controlParameters.back().value = QStringLiteral("-1");
        if (!FdsSchemaRegistry::validate(QStringLiteral("CTRL"), QStringLiteral("M08_CTRL"), controlParameters, version)
                .join(QLatin1Char('\n')).contains(QStringLiteral("DELAY")))
            return fail(QStringLiteral("Negative DELAY was not rejected."));
        controlParameters.back() = {QStringLiteral("DELAY"), FcFdsParameterKind::ObjectReferences, {}, {QStringLiteral("fixture-device-uuid")}};
        if (!FdsSchemaRegistry::validate(QStringLiteral("CTRL"), QStringLiteral("M08_CTRL"), controlParameters, version)
                .join(QLatin1Char('\n')).contains(QStringLiteral("DELAY")))
            return fail(QStringLiteral("Object-reference DELAY was not rejected as non-numeric."));
    }

    std::cout << "M08 production wizard 12-combination, persistence, undo/redo and ordinary-detector checks passed.\n";
    return 0;
}
