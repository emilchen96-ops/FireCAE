// Object-type draft, schema validation, and focused-editor regression tests.
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsExamples.h"
#include "fds/FdsImporter.h"
#include "fds/FdsSchema.h"
#include "fds/FdsWriter.h"
#include "ui/FdsObjectEditorDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <memory>

namespace
{
int failure(const char* message)
{
    std::cerr << "M07: " << message << '\n';
    return 1;
}

void settle()
{
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void reveal(QWidget* widget, FdsObjectEditorDialog& dialog)
{
    for (QTabWidget* tabs : dialog.findChildren<QTabWidget*>()) {
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->widget(index) == widget || tabs->widget(index)->isAncestorOf(widget)) {
                tabs->setCurrentIndex(index);
                break;
            }
        }
    }
}

FcFdsParameter field(const std::vector<FcFdsParameter>& parameters, const QString& key)
{
    for (const auto& parameter : parameters)
        if (parameter.key.compare(key, Qt::CaseInsensitive) == 0) return parameter;
    return {};
}

bool same(const std::vector<FcFdsParameter>& left, const std::vector<FcFdsParameter>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].key != right[i].key || left[i].kind != right[i].kind ||
            left[i].value != right[i].value || left[i].targetObjectIds != right[i].targetObjectIds)
            return false;
    }
    return true;
}

bool errorsName(const QStringList& errors, const QString& keyword, const QString& key)
{
    for (const QString& error : errors)
        if (error.contains(keyword, Qt::CaseInsensitive) && error.contains(key, Qt::CaseInsensitive))
            return true;
    return false;
}

bool changeType(FdsObjectEditorDialog& dialog, const QString& keyword, bool processEvents = true)
{
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("FdsKeywordCombo"));
    if (!combo || !combo->isEnabled()) return false;
    // Exercise the production currentTextChanged connection. Do not call setEditorData.
    combo->setCurrentText(keyword);
    if (processEvents) settle();
    return dialog.editorData().keyword == keyword.trimmed().toUpper();
}

int rowFor(QTableWidget* table, const QString& key)
{
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0) && table->item(row, 0)->text() == key) return row;
    return -1;
}

bool editAdvanced(FdsObjectEditorDialog& dialog, const QString& key, const QString& value)
{
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("FdsParameterTable"));
    auto* add = dialog.findChild<QPushButton*>(QStringLiteral("AddFdsParameterButton"));
    if (!table || !add) return false;
    int row = rowFor(table, key);
    if (row < 0) {
        add->click();
        row = table->rowCount() - 1;
        table->item(row, 0)->setText(key);
    }
    table->item(row, 2)->setText(value);
    return field(dialog.editorData().parameters, key).value == value;
}

bool acceptThroughButton(FdsObjectEditorDialog& dialog)
{
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    if (!buttons || !buttons->button(QDialogButtonBox::Ok)) return false;
    bool warning = false;
    QTimer modalGuard;
    QObject::connect(&modalGuard, &QTimer::timeout, &dialog, [&warning]() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            warning = true;
            box->reject();
        }
    });
    modalGuard.start(10);
    buttons->button(QDialogButtonBox::Ok)->click();
    modalGuard.stop();
    return !warning && dialog.result() == QDialog::Accepted;
}

bool selectSurface(FdsObjectEditorDialog& dialog, const QString& uuid)
{
    auto* choose = dialog.findChild<QPushButton*>(QStringLiteral("SchemaChoose_SURF_ID"));
    if (!choose) return false;
    bool selected = false;
    QTimer modalDriver;
    QObject::connect(&modalDriver, &QTimer::timeout, &dialog, [&]() {
        auto* chooser = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!chooser) return;
        auto* list = chooser->findChild<QListWidget*>();
        auto* buttons = chooser->findChild<QDialogButtonBox*>();
        if (chooser->objectName() != QStringLiteral("FdsSchemaReferenceDialog") || !list || !buttons) {
            chooser->reject();
            return;
        }
        list->clearSelection();
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == uuid) {
                list->item(row)->setSelected(true);
                selected = true;
            }
        }
        buttons->button(selected ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    modalDriver.start(10);
    choose->click();
    modalDriver.stop();
    return selected;
}
}

int runM07ObjectTypeSwitchRegression()
{
    auto project = FdsExamples::createSimpleTestProject();
    if (!project || !FdsWriter::render(*project).success())
        return failure("The unchanged project fixture must render before testing.");

    // Regression trigger: default MESH -> OBST using only the real combo signal.
    FdsObjectEditorDialog fresh(project.get());
    fresh.show();
    settle();
    if (field(fresh.editorData().parameters, QStringLiteral("IJK")).value != QStringLiteral("20,20,12") ||
        !changeType(fresh, QStringLiteral("OBST")))
        return failure("Could not establish the original MESH-to-OBST path.");
    auto* xmax = fresh.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_XB_1"));
    if (!xmax) return failure("OBST Basic XB field is missing.");
    xmax->setValue(0.8);
    const auto freshData = fresh.editorData();
    if (!field(freshData.parameters, QStringLiteral("IJK")).key.isEmpty() ||
        !field(freshData.parameters, QStringLiteral("INITIAL_STATE")).key.isEmpty() ||
        field(freshData.parameters, QStringLiteral("XB")).value != QStringLiteral("0,0.8,0,1,0,1") ||
        !acceptThroughButton(fresh))
        return failure("A switched OBST retained foreign defaults or failed the real OK validation.");
    auto accepted = std::make_shared<FcFdsNamelist>(QStringLiteral("M07 OBST"),
        FcObjectType::Unknown, freshData.keyword, QStringLiteral("M07_OBST"), 100);
    accepted->setParameters(freshData.parameters);
    project->document()->geometryGroup()->addChild(accepted);
    QTemporaryDir directory;
    const QString saved = directory.filePath(QStringLiteral("m07.firecae"));
    if (!directory.isValid() || !FcProjectSerializer::save(*project, saved))
        return failure("The newly accepted OBST could not be saved.");
    auto loaded = FcProjectSerializer::load(saved);
    const auto reopened = loaded.success() ? std::dynamic_pointer_cast<FcFdsNamelist>(
        loaded.project->document()->findObject(accepted->id())) : nullptr;
    if (!reopened || !same(reopened->parameters(), freshData.parameters))
        return failure("The switched object's exact parameters did not survive reopen.");
    const auto written = FdsWriter::render(*loaded.project);
    if (!written.success()) return failure("The switched OBST failed model/writer validation.");
    bool foundObst = false;
    for (const QString& line : written.text.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("ID='M07_OBST'"))) {
            foundObst = line.startsWith(QStringLiteral("&OBST"));
            if (line.contains(QStringLiteral("IJK=")) || line.contains(QStringLiteral("INITIAL_STATE=")))
                return failure("Writer emitted a foreign field after save/reopen.");
        }
    }
    if (!foundObst) return failure("The accepted OBST was not exported.");

    // Each type owns all of its edits, including unknown fields and real UUID references.
    auto surface = std::make_shared<FcFdsNamelist>(QStringLiteral("M07 Surface"),
        FcObjectType::Unknown, QStringLiteral("SURF"), QStringLiteral("M07_SURF"), 101);
    surface->addStringParameter(QStringLiteral("COLOR"), QStringLiteral("BLUE"));
    project->document()->surfacesGroup()->addChild(surface);
    FdsObjectEditorDialog drafts(project.get());
    drafts.show();
    settle();
    if (!editAdvanced(drafts, QStringLiteral("IJK"), QStringLiteral("30,20,15")) ||
        !editAdvanced(drafts, QStringLiteral("MY_USER_FIELD"), QStringLiteral("123")))
        return failure("Could not edit the MESH draft through Advanced rows.");
    const auto meshDraft = drafts.editorData().parameters;
    if (!changeType(drafts, QStringLiteral("OBST")) || !selectSurface(drafts, surface->id()) ||
        !editAdvanced(drafts, QStringLiteral("FYI"), QStringLiteral("obst_note")))
        return failure("Could not create a distinct OBST draft with a real chosen UUID.");
    const auto obstDraft = drafts.editorData().parameters;
    const auto reference = field(obstDraft, QStringLiteral("SURF_ID"));
    if (reference.kind != FcFdsParameterKind::ObjectReferences ||
        reference.targetObjectIds != QStringList{surface->id()} ||
        !field(obstDraft, QStringLiteral("MY_USER_FIELD")).key.isEmpty())
        return failure("The new type inherited a user field or lost its UUID reference.");
    if (!changeType(drafts, QStringLiteral("MESH")) || !same(drafts.editorData().parameters, meshDraft) ||
        !changeType(drafts, QStringLiteral("OBST")) || !same(drafts.editorData().parameters, obstDraft))
        return failure("Bidirectional draft restoration lost values, ordering, kinds, or UUIDs.");

    // setEditorData is tested only as the explicit full-load API, never as the switch trigger.
    FdsObjectEditorData replacement = drafts.editorData();
    replacement.parameters = {{QStringLiteral("XB"), FcFdsParameterKind::Raw, QStringLiteral("0,2,0,2,0,2"), {}},
        {QStringLiteral("FYI"), FcFdsParameterKind::String, QStringLiteral("fresh_load"), {}}};
    drafts.setEditorData(replacement);
    settle();
    if (!changeType(drafts, QStringLiteral("MESH")) ||
        !field(drafts.editorData().parameters, QStringLiteral("MY_USER_FIELD")).key.isEmpty() ||
        !changeType(drafts, QStringLiteral("OBST")) || !same(drafts.editorData().parameters, replacement.parameters))
        return failure("Explicit full load did not invalidate stale drafts and preserve new parameters.");

    // Imported legal-but-unmodeled FYI and an unknown future record stay lossless.
    const QString prefix = QStringLiteral("&HEAD CHID='m07' /\n&MESH IJK=4,4,4, XB=0,2,0,2,0,2 /\n");
    const QString suffix = QStringLiteral("&TIME T_END=1 /\n&TAIL /\n");
    auto imported = FdsImporter{}.importText(prefix + QStringLiteral(
        "&OBST ID='IMPORT_OBST', XB=0,1,0,1,0,1, FYI='keep_note' /\n"
        "&FUTURE_RECORD NOVEL_VALUE='keep_future' /\n") + suffix);
    if (!imported.success()) return failure("Unknown-parameter fixture failed to import.");
    const auto preserved = FdsWriter::render(*imported.project);
    if (!preserved.success() || !preserved.text.contains(QStringLiteral("FYI='keep_note'")) ||
        !preserved.text.contains(QStringLiteral("&FUTURE_RECORD")) ||
        !preserved.text.contains(QStringLiteral("NOVEL_VALUE='keep_future'")))
        return failure("Known-invalid checks damaged unrelated unknown-field/record round trips.");

    // All advertised schema versions: targeted invalid pairs fail, valid owners retain fields.
    for (const QString& version : FdsSchemaRegistry::supportedVersions()) {
        for (const QString& keyword : {QStringLiteral("OBST"), QStringLiteral("HOLE")}) {
            const auto defaults = FdsSchemaRegistry::defaultParameters(keyword, version);
            if (!field(defaults, QStringLiteral("INITIAL_STATE")).key.isEmpty())
                return failure("A geometry schema still creates INITIAL_STATE by default.");
            for (const QString& key : {QStringLiteral("IJK"), QStringLiteral("INITIAL_STATE")}) {
                auto poisoned = defaults;
                poisoned.push_back({key, FcFdsParameterKind::Raw,
                    key == QStringLiteral("IJK") ? QStringLiteral("20,20,12") : QStringLiteral(".TRUE."), {}});
                if (!errorsName(FdsSchemaRegistry::validate(keyword, {}, poisoned, version), keyword, key))
                    return failure("Schema accepted a known-invalid geometry parameter.");
                FcFdsNamelist model(QStringLiteral("poisoned"), FcObjectType::Unknown, keyword);
                model.setParameters(poisoned);
                if (!errorsName(model.validate(), keyword, key))
                    return failure("FcFdsNamelist validation accepted a known-invalid parameter.");
            }
        }
        if (!FdsSchemaRegistry::parameter(QStringLiteral("MESH"), QStringLiteral("IJK"), version) ||
            !FdsSchemaRegistry::parameter(QStringLiteral("DEVC"), QStringLiteral("INITIAL_STATE"), version) ||
            !FdsSchemaRegistry::parameter(QStringLiteral("CTRL"), QStringLiteral("INITIAL_STATE"), version))
            return failure("The targeted fix removed valid fields from their proper record types.");
    }
    auto red = FdsImporter{}.importText(prefix + QStringLiteral(
        "&OBST XB=0,1,0,1,0,1, IJK=20,20,12 /\n") + suffix);
    if (!red.success()) return failure("The old polluted input must remain importable for inspection.");
    const auto rejected = FdsWriter::render(*red.project);
    const QString deniedOutput = directory.filePath(QStringLiteral("must-not-exist.fds"));
    QString error;
    if (rejected.success() || !errorsName(rejected.errors, QStringLiteral("OBST"), QStringLiteral("IJK")) ||
        FdsWriter::writeFile(*red.project, deniedOutput, &error) || QFileInfo::exists(deniedOutput))
        return failure("Writer must reject old pollution before creating an output file.");

    // Focused schema text is not yet in the raw table when a programmatic type change arrives.
    FdsObjectEditorDialog focused(project.get(), QStringLiteral("OBST"));
    focused.show();
    settle();
    auto* tabs = focused.findChild<QTabWidget*>(QStringLiteral("FdsEditorTabs"));
    auto* basic = focused.findChild<QWidget*>(QStringLiteral("FdsBasicSchemaEditor"));
    if (!tabs || !basic) return failure("Focused edit fixture lacks Basic tab.");
    tabs->setCurrentWidget(basic);
    settle();
    QPointer<QLineEdit> staleColor = focused.findChild<QLineEdit*>(QStringLiteral("Schema_COLOR"));
    if (!staleColor) return failure("Focused edit fixture lacks COLOR field.");
    reveal(staleColor, focused);
    settle();
    if (!staleColor) return failure("COLOR fixture was rebuilt while revealing its tab.");
    staleColor->setFocus();
    settle();
    if (!staleColor->hasFocus()) return failure("Focused edit fixture did not acquire focus.");
    staleColor->setText(QStringLiteral("BLUE"));
    staleColor->setModified(true);
    if (!changeType(focused, QStringLiteral("MESH"), false)) return failure("Focused type switch failed.");
    // Deliberately deliver the old editor's commit before deleteLater is processed.
    if (staleColor) QMetaObject::invokeMethod(staleColor, "editingFinished", Qt::DirectConnection);
    settle();
    if (!field(focused.editorData().parameters, QStringLiteral("COLOR")).key.isEmpty() ||
        !changeType(focused, QStringLiteral("OBST")) ||
        field(focused.editorData().parameters, QStringLiteral("COLOR")).value != QStringLiteral("BLUE"))
        return failure("Uncommitted schema text was lost or an obsolete editor polluted the new type.");

    // Empty or case-only keyword edits rebuild assistance too; commit first.
    QPointer<QLineEdit> caseColor = focused.findChild<QLineEdit*>(QStringLiteral("Schema_COLOR"));
    if (!caseColor) return failure("Case-change COLOR field missing.");
    reveal(caseColor, focused);
    settle();
    if (!caseColor) return failure("Case-change COLOR fixture was rebuilt while revealing its tab.");
    caseColor->setFocus();
    settle();
    if (!caseColor->hasFocus()) return failure("Case-change focus not established.");
    caseColor->setText(QStringLiteral("GREEN"));
    caseColor->setModified(true);
    if (!changeType(focused, QString{}, false) || !changeType(focused, QStringLiteral("obst")) ||
        field(focused.editorData().parameters, QStringLiteral("COLOR")).value != QStringLiteral("GREEN"))
        return failure("An empty/case-only keyword edit lost a pending value.");

    // Pending numeric keyboard input must still be interpreted as a MESH edit.
    if (!changeType(focused, QStringLiteral("MESH"))) return failure("Advanced focus setup failed.");
    auto* meshCount = focused.findChild<QSpinBox*>(QStringLiteral("MeshCellCountXSpin"));
    auto* pendingNumber = meshCount ? meshCount->findChild<QLineEdit*>() : nullptr;
    if (!meshCount || !pendingNumber) return failure("MESH numeric editor missing.");
    // Production uses deferred numeric commits too; make the fixture explicit.
    meshCount->setKeyboardTracking(false);
    reveal(meshCount, focused);
    pendingNumber->setFocus();
    settle();
    QWidget* meshFocus = QApplication::focusWidget();
    if (meshFocus != meshCount && (!meshFocus || !meshCount->isAncestorOf(meshFocus)))
        return failure("MESH numeric focus not established.");
    pendingNumber->setText(QStringLiteral("37"));
    pendingNumber->setModified(true);
    if (meshCount->value() == 37) return failure("Numeric fixture was already committed before the switch.");
    if (!changeType(focused, QStringLiteral("OBST")) ||
        !field(focused.editorData().parameters, QStringLiteral("IJK")).key.isEmpty() ||
        !changeType(focused, QStringLiteral("MESH")) ||
        field(focused.editorData().parameters, QStringLiteral("IJK")).value.section(QLatin1Char(','), 0, 0) != QStringLiteral("37"))
        return failure("Pending MESH numeric input was lost or committed to the next type.");

    // Advanced delegate focus must also commit into the departing type's draft.
    auto* table = focused.findChild<QTableWidget*>(QStringLiteral("FdsParameterTable"));
    if (!table) return failure("Advanced table missing.");
    tabs->setCurrentIndex(tabs->count() - 1);
    table->setCurrentCell(rowFor(table, QStringLiteral("IJK")), 2);
    table->editItem(table->currentItem());
    settle();
    auto* delegate = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    if (!delegate || !table->isAncestorOf(delegate)) return failure("Advanced delegate focus was not established.");
    delegate->setText(QStringLiteral("7,8,9"));
    delegate->setModified(true);
    if (!changeType(focused, QStringLiteral("OBST")) || !changeType(focused, QStringLiteral("MESH")) ||
        field(focused.editorData().parameters, QStringLiteral("IJK")).value != QStringLiteral("7,8,9"))
        return failure("An uncommitted Advanced value was lost across a type switch.");

    // A saved empty optional-record draft is distinct from a never-visited type.
    if (!changeType(focused, QStringLiteral("MISC"))) return failure("Empty draft setup failed.");
    auto* remove = focused.findChild<QPushButton*>(QStringLiteral("RemoveFdsParameterButton"));
    if (!remove) return failure("Advanced remove button missing.");
    while (table->rowCount() > 0) {
        const int before = table->rowCount();
        table->setCurrentCell(0, 0);
        remove->click();
        if (table->rowCount() >= before) return failure("Could not clear optional-record rows.");
    }
    if (!changeType(focused, QStringLiteral("MESH")) || !changeType(focused, QStringLiteral("MISC")) ||
        !focused.editorData().parameters.empty() || !acceptThroughButton(focused))
        return failure("Restoring an empty draft introduced an invalid empty-key row.");

    FdsObjectEditorDialog untouched(project.get(), QStringLiteral("OBST"));
    auto omission = untouched.editorData();
    omission.parameters = {{QStringLiteral("XB"), FcFdsParameterKind::Raw, QStringLiteral("0,1,0,1,0,1"), {}}};
    untouched.setEditorData(omission);
    untouched.show();
    settle();
    QPointer<QLineEdit> defaultOnly = untouched.findChild<QLineEdit*>(QStringLiteral("Schema_COLOR"));
    if (!defaultOnly) return failure("Untouched-default COLOR field missing.");
    reveal(defaultOnly, untouched);
    settle();
    if (!defaultOnly) return failure("Untouched-default fixture was rebuilt while revealing its tab.");
    defaultOnly->setFocus();
    settle();
    if (!defaultOnly->hasFocus() || !changeType(untouched, QStringLiteral("MESH")) ||
        !changeType(untouched, QStringLiteral("OBST")) ||
        !field(untouched.editorData().parameters, QStringLiteral("COLOR")).key.isEmpty())
        return failure("Merely focusing an untouched default added a parameter to an omitted field.");

    // Explicit replacement owns the new table: old pending UI values must be discarded.
    FdsObjectEditorDialog explicitLoad(project.get(), QStringLiteral("OBST"));
    explicitLoad.show();
    settle();
    QPointer<QLineEdit> departingColor = explicitLoad.findChild<QLineEdit*>(QStringLiteral("Schema_COLOR"));
    if (!departingColor) return failure("Explicit-load COLOR fixture missing.");
    reveal(departingColor, explicitLoad);
    settle();
    if (!departingColor) return failure("Explicit-load fixture was rebuilt while revealing its tab.");
    departingColor->setFocus();
    settle();
    if (!departingColor->hasFocus()) return failure("Explicit-load focus was not established.");
    departingColor->setText(QStringLiteral("RED"));
    departingColor->setModified(true);
    FdsObjectEditorData loadedDevice;
    loadedDevice.name = QStringLiteral("M07 explicit device");
    loadedDevice.keyword = QStringLiteral("DEVC");
    loadedDevice.fdsId = QStringLiteral("M07_EXPLICIT_DEVC");
    loadedDevice.group = FcDocumentGroup::Devices;
    loadedDevice.parameters = FdsSchemaRegistry::defaultParameters(loadedDevice.keyword);
    explicitLoad.setEditorData(loadedDevice);
    if (departingColor) QMetaObject::invokeMethod(departingColor, "editingFinished", Qt::DirectConnection);
    settle();
    if (!same(explicitLoad.editorData().parameters, loadedDevice.parameters) ||
        !changeType(explicitLoad, QStringLiteral("MESH")) ||
        !changeType(explicitLoad, QStringLiteral("DEVC")) ||
        !same(explicitLoad.editorData().parameters, loadedDevice.parameters))
        return failure("Old pending/retired editor signals contaminated an explicit full load.");

    std::cout << "M07 object type, draft, focus, compatibility and validation regressions passed.\n";
    return 0;
}
