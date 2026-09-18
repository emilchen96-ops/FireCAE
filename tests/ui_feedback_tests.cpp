#include "app/MainWindow.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsWriter.h"
#include "ui/FdsObjectEditorDialog.h"
#include "ui/FdsWorkflowDialogs.h"
#include "ui/GeometryImportWizard.h"
#include "ui/ModelTreeWidget.h"
#include "ui/ObjectReferenceDialog.h"
#include "ui/ScenarioManagerDialog.h"
#include "ui/UiLanguage.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <iostream>
#include <memory>

namespace
{
int failures = 0;
int checks = 0;
void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void renderEvidence(QWidget& widget, const QString& name)
{
    const QString directory = qEnvironmentVariable("FIRECAE_UI_FEEDBACK_EVIDENCE_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    widget.ensurePolished();
    QPixmap image(widget.size());
    image.fill(Qt::transparent);
    widget.render(&image);
    expect(image.save(QDir(directory).filePath(name + QStringLiteral(".png"))),
           "Offscreen evidence image was saved");
}
}

int runUiFeedbackAcceptance()
{
    failures = 0;
    checks = 0;
    // Never touch the user's preferences or recovery records.
    QTemporaryDir sandbox;
    expect(sandbox.isValid(), "Temporary test directory exists");
    if (!sandbox.isValid()) return 1;
#ifdef Q_OS_WIN
    // The offscreen platform does not discover Windows fallback fonts like
    // the interactive Windows plugin does. Register a real Chinese font for
    // faithful test renders without changing application font preferences.
    const int fontId = QFontDatabase::addApplicationFont(
        qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) +
        QStringLiteral("/Fonts/msyh.ttc"));
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    expect(!families.isEmpty(), "Chinese font is available for offscreen evidence");
    if (!families.isEmpty()) QApplication::setFont(QFont(families.first(), 10));
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.UiFeedback.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("UiFeedback"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox.path());
    qputenv("FIRECAE_RECOVERY_DIRECTORY", sandbox.path().toLocal8Bit());
    qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT", "1");
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    qunsetenv("FIRECAE_UI_LANGUAGE");
    UiLanguageManager::setCurrentLanguage(UiLanguage::ChineseSimplified);
    UiLanguageManager::initialize();

    FcProject project(QStringLiteral("同名对象测试"));
    project.setChid(QStringLiteral("ui_feedback"));
    project.setEndTime(30);
    auto mesh = std::make_shared<FcFdsMesh>(
        QStringLiteral("真实网格"), QStringLiteral("DOMAIN"),
        std::array<int, 3>{30, 20, 15}, FcFdsBounds{0, 6, 0, 4, 0, 3});
    project.document()->meshesGroup()->addChild(mesh);
    auto surface = std::make_shared<FcFdsNamelist>(
        QStringLiteral("50kw Surface"), FcObjectType::Surface,
        QStringLiteral("SURF"), QStringLiteral("BURNER"));
    surface->addRawParameter(QStringLiteral("HRRPUA"), QStringLiteral("500"));
    project.document()->surfacesGroup()->addChild(surface);
    auto base = std::make_shared<FcFdsNamelist>(
        QStringLiteral("MESH 001"), FcObjectType::Obstruction,
        QStringLiteral("OBST"), QStringLiteral("BASE"));
    base->addRawParameter(QStringLiteral("XB"), QStringLiteral("2.5,3.5,1.5,2.5,0,0.2"));
    project.document()->geometryGroup()->addChild(base);
    auto ventA = std::make_shared<FcFdsNamelist>(
        QStringLiteral("同名火源"), FcObjectType::Vent, QStringLiteral("VENT"),
        QStringLiteral("FIRE_A"));
    auto ventB = std::make_shared<FcFdsNamelist>(
        QStringLiteral("同名火源"), FcObjectType::Vent, QStringLiteral("VENT"),
        QStringLiteral("FIRE_B"));
    for (const auto& vent : {ventA, ventB}) {
        vent->addRawParameter(QStringLiteral("XB"), QStringLiteral("2.5,3.5,1.5,2.5,0.2,0.2"));
        vent->addReferenceParameter(QStringLiteral("SURF_ID"), {surface->id()});
        project.document()->ventsGroup()->addChild(vent);
    }

    FireSourceWizardDialog fire(&project);
    auto* host = fire.findChild<QComboBox*>(QStringLiteral("FireSourceHostCombo"));
    auto* scope = fire.findChild<QLabel*>(QStringLiteral("FireSourceHostScopeLabel"));
    auto* currentSurface = fire.findChild<QLabel*>(QStringLiteral("FireSourceHostSurfaceLabel"));
    auto* ramp = fire.findChild<QCheckBox*>(QStringLiteral("FireSourceCreateRampCheck"));
    auto* chart = fire.findChild<QWidget*>(QStringLiteral("FireSourceHrrPreview"));
    auto* summary = fire.findChild<QLabel*>(QStringLiteral("FireSourceSummaryLabel"));
    auto* note = fire.findChild<QLabel*>(QStringLiteral("FireSourcePreviewNoteLabel"));
    auto* x = fire.findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceXSpin"));
    auto* name = fire.findChild<QLineEdit*>(QStringLiteral("FireSourceNameEdit"));
    expect(host && scope && currentSurface && ramp && chart && summary && note && x && name,
           "Fire-source explanation and preview controls exist");
    if (!host || !scope || !currentSurface || !ramp || !chart || !summary || !note || !x || !name)
        return 1;
    const int baseIndex = host->findData(base->id());
    const int ventIndex = host->findData(ventB->id());
    expect(host->findData(mesh->id()) < 0, "A real MESH is not a valid fire host");
    expect(baseIndex > 0 && ventIndex > 0, "OBST and VENT hosts remain available");
    expect(host->findData(ventA->id()) != ventIndex, "Same-name hosts retain independent UUID identity");
    expect(host->itemText(baseIndex).contains(QStringLiteral("OBST")),
           "Misleading MESH 001 name is accompanied by its actual OBST type");
    expect(!host->itemText(ventIndex).contains(ventB->id()) &&
           host->itemData(ventIndex, Qt::ToolTipRole).toString().contains(ventB->id()),
           "UUID is kept in data/tooltip, not the visible label");
    host->setCurrentIndex(ventIndex);
    expect(fire.data().hostObjectId == ventB->id() && !x->isEnabled(),
           "Existing host selection retains its UUID and does not edit coordinates");
    expect(currentSurface->text().contains(surface->name()), "Existing surface assignment is explained");
    expect(scope->text().contains(QStringLiteral("通风口")), "VENT scope is explained in Chinese");
    host->setCurrentIndex(baseIndex);
    expect(scope->text().contains(QStringLiteral("表面")), "OBST assignment scope is explained");
    host->setCurrentIndex(0);
    expect(fire.data().hostObjectId.isEmpty() && x->isEnabled(),
           "No host creates a separately positioned burner");
    ramp->setChecked(false);
    expect(!fire.data().createRamp && !chart->property("customRamp").toBool(),
           "No custom RAMP does not show a growth-decay curve");
    expect(chart->property("timeAxisLabel").toString().contains(QStringLiteral("时间")) &&
           chart->property("hrrAxisLabel").toString().contains(QStringLiteral("热释放率")),
           "Both chart axes are Chinese");
    expect(!summary->text().contains(QStringLiteral("Resolved")) &&
           summary->text().contains(QStringLiteral("热释放率")), "Dynamic HRR summary is Chinese");
    const QString customName = QStringLiteral("Save / 我的火源");
    name->setText(customName);
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);
    QApplication::processEvents();
    fire.retranslateUi();
    expect(chart->property("timeAxisLabel").toString() == QStringLiteral("Time (s)"),
           "Existing fire preview changes back to English");
    expect(name->text() == customName && fire.data().hostObjectId.isEmpty(),
           "Language changes preserve user input and selection");
    UiLanguageManager::setCurrentLanguage(UiLanguage::ChineseSimplified);
    QApplication::processEvents();
    fire.retranslateUi();
    if (auto* tabs = fire.findChild<QTabWidget*>(QStringLiteral("FireSourceWizardTabs"))) {
        tabs->setCurrentIndex(0);
        renderEvidence(fire, QStringLiteral("fire-host-zh"));
        host->setCurrentIndex(baseIndex);
        renderEvidence(fire, QStringLiteral("fire-host-obst-zh"));
        host->setCurrentIndex(0);
        tabs->setCurrentIndex(3);
        renderEvidence(fire, QStringLiteral("fire-preview-zh"));
    }

    const auto owners = collectObjectReferenceOwners(*project.document(), surface->id());
    expect(owners.size() == 2, "All same-name referencing owners are collected");
    ObjectReferenceDialog references(*surface, owners);
    auto* table = references.findChild<QTableWidget*>(QStringLiteral("ObjectReferenceOwnersTable"));
    auto* explanation = references.findChild<QLabel*>(QStringLiteral("ObjectReferenceExplanation"));
    auto* details = references.findChild<QPlainTextEdit*>(QStringLiteral("ObjectReferenceTechnicalDetails"));
    auto* toggle = references.findChild<QToolButton*>(QStringLiteral("ObjectReferenceDetailsToggle"));
    auto* locate = references.findChild<QPushButton*>(QStringLiteral("ObjectReferenceLocateButton"));
    expect(table && explanation && details && toggle && locate, "Reference dialog controls exist");
    if (!table || !explanation || !details || !toggle || !locate) return 1;
    expect(table->columnCount() == 5 && table->item(0, 2)->text() != table->item(1, 2)->text(),
           "Same-name referencing objects show distinct FDS IDs");
    expect(explanation->text().contains(QStringLiteral("删除")) &&
           !explanation->text().contains(surface->id()), "Plain explanation is Chinese and hides UUID");
    expect(details->isHidden() && details->toPlainText().contains(surface->id()),
           "Technical details retain UUID but start collapsed");
    for (int row = 0; row < table->rowCount(); ++row) {
        table->selectRow(row);
        expect(references.selectedOwnerUuid() == table->item(row, 0)->data(Qt::UserRole).toString(),
               "Each selectable row navigates using UUID");
    }
    renderEvidence(references, QStringLiteral("reference-conflict-zh"));
    toggle->setChecked(true);
    expect(!details->isHidden(), "Technical details can be expanded without altering references");
    expect(collectObjectReferenceOwners(*project.document(), surface->id()).size() == 2,
           "Reference inspection does not remove links");

    FdsObjectEditorDialog editor(&project);
    auto* keyword = editor.findChild<QComboBox*>(QStringLiteral("FdsKeywordCombo"));
    auto* objectName = editor.findChild<QLineEdit*>(QStringLiteral("FdsObjectNameEdit"));
    expect(keyword && objectName, "Generic object editor controls exist");
    if (keyword && objectName) {
        keyword->setCurrentText(QStringLiteral("OBST"));
        expect(objectName->text() == QStringLiteral("OBST 001"),
               "Untouched default name follows record type changes");
        objectName->setText(QStringLiteral("我的基座"));
        keyword->setCurrentText(QStringLiteral("VENT"));
        expect(objectName->text() == QStringLiteral("我的基座"),
               "User-defined names are never automatically renamed");
    }
    // Creating an OPEN vent must work without inventing a project UUID for
    // FDS's built-in surface; ordinary references remain strict UUID references.
    {
        FcProject openProject(QStringLiteral("OPEN boundary regression"));
        openProject.setChid(QStringLiteral("open_boundary_regression"));
        openProject.setEndTime(1);
        openProject.document()->meshesGroup()->addChild(std::make_shared<FcFdsMesh>(
            QStringLiteral("Domain"), QStringLiteral("DOMAIN"),
            std::array<int, 3>{12, 8, 6}, FcFdsBounds{0, 6, 0, 4, 0, 3}));
        auto realSurface = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Real surface"), FcObjectType::Surface,
            QStringLiteral("SURF"), QStringLiteral("REAL_SURFACE"));
        openProject.document()->surfacesGroup()->addChild(realSurface);
        const auto attemptAccept = [](FdsObjectEditorDialog& dialog, QString& warning) {
            QTimer warningCloser;
            QObject::connect(&warningCloser, &QTimer::timeout, [&]() {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    warning = message->text();
                    message->reject();
                }
            });
            auto* box = dialog.findChild<QDialogButtonBox*>(QString{}, Qt::FindDirectChildrenOnly);
            if (!box) return false;
            warningCloser.start(10);
            box->button(QDialogButtonBox::Ok)->click();
            warningCloser.stop();
            return dialog.result() == QDialog::Accepted;
        };
        FdsObjectEditorDialog openEditor(&openProject, QStringLiteral("VENT"));
        auto* parameters = openEditor.findChild<QTableWidget*>(QStringLiteral("FdsParameterTable"));
        auto* pages = openEditor.findChild<QTabWidget*>(QStringLiteral("FdsEditorTabs"));
        int xbRow = -1;
        if (parameters) {
            for (int row = 0; row < parameters->rowCount(); ++row)
                if (parameters->item(row, 0)->text() == QStringLiteral("XB")) xbRow = row;
        }
        expect(parameters && pages && xbRow >= 0, "Default VENT exposes its XB field");
        if (!parameters || !pages || xbRow < 0) return 1;
        pages->setCurrentIndex(3);
        parameters->item(xbRow, 2)->setText(QStringLiteral("0,6,0,4,3,3"));
        for (int page : {1, 2, 3, 1}) {
            pages->setCurrentIndex(page);
            QApplication::processEvents();
        }
        auto* openLabel = openEditor.findChild<QLabel*>(QStringLiteral("SchemaReferenceValue_SURF_ID"));
        expect(openLabel && openLabel->text() == QStringLiteral("OPEN"),
               "Built-in OPEN is visible in the basic surface field");
        auto openData = openEditor.editorData();
        int surfaceIndex = -1;
        for (int index = 0; index < static_cast<int>(openData.parameters.size()); ++index)
            if (openData.parameters[index].key == QStringLiteral("SURF_ID")) surfaceIndex = index;
        expect(surfaceIndex >= 0 && openData.parameters[surfaceIndex].kind == FcFdsParameterKind::String &&
                   openData.parameters[surfaceIndex].value == QStringLiteral("OPEN") &&
                   openData.parameters[surfaceIndex].targetObjectIds.isEmpty(),
               "Only editing XB and switching pages preserves literal OPEN");
        if (surfaceIndex < 0) return 1;
        QString warning;
        const bool accepted = attemptAccept(openEditor, warning);
        expect(accepted, "A default OPEN vent accepts after only XB is edited");
        if (accepted) {
            auto openVent = std::make_shared<FcFdsNamelist>(
                openData.name, FcObjectType::Vent, openData.keyword, openData.fdsId);
            openVent->setParameters(openData.parameters);
            openProject.document()->ventsGroup()->addChild(openVent);
            const auto rendered = FdsWriter::render(openProject);
            expect(rendered.success() && rendered.text.contains(QStringLiteral("SURF_ID='OPEN'")) &&
                       rendered.text.contains(QStringLiteral("XB=0,6,0,4,3,3")),
                   "Accepted OPEN boundary exports its literal surface and exact coordinates");
        }
        openData.parameters[surfaceIndex] = {QStringLiteral("SURF_ID"),
            FcFdsParameterKind::ObjectReferences, {}, {realSurface->id()}};
        FdsObjectEditorDialog referenceEditor(&openProject, QStringLiteral("VENT"));
        referenceEditor.setEditorData(openData);
        auto* referencePages = referenceEditor.findChild<QTabWidget*>(QStringLiteral("FdsEditorTabs"));
        for (int page : {1, 3, 2, 3}) referencePages->setCurrentIndex(page);
        const auto referenceData = referenceEditor.editorData();
        expect(referenceData.parameters[surfaceIndex].kind == FcFdsParameterKind::ObjectReferences &&
                   referenceData.parameters[surfaceIndex].targetObjectIds == QStringList{realSurface->id()},
               "A real SURF reference retains its exact UUID across editor pages");
        warning.clear();
        expect(attemptAccept(referenceEditor, warning), "A real SURF UUID reference still accepts");
        openData.parameters[surfaceIndex].targetObjectIds.clear();
        openData.parameters[surfaceIndex].value = QStringLiteral("OPEN");
        FdsObjectEditorDialog emptyReferenceEditor(&openProject, QStringLiteral("VENT"));
        emptyReferenceEditor.setEditorData(openData);
        warning.clear();
        expect(!attemptAccept(emptyReferenceEditor, warning) &&
                   warning.contains(UiLanguageManager::text(QStringLiteral("Reference parameters must select at least one object."))),
               "Even OPEN text cannot bypass the UUID requirement in reference mode");
    }

    GeometryImportWizard importer;
    auto* units = importer.findChild<QComboBox*>(QStringLiteral("GeometryImportUnitCombo"));
    expect(units && units->itemText(0) == UiLanguageManager::text(QStringLiteral("Auto")) &&
           importer.options().sourceUnit == QStringLiteral("Auto"),
           "Translated Auto label preserves the importer unit code");
    ScenarioManagerDialog scenarios(&project);
    auto* backend = scenarios.findChild<QComboBox*>(QStringLiteral("ScenarioBackendCombo"));
    expect(backend && backend->itemData(0).toString() == QStringLiteral("fds.serial.cpu") &&
           backend->itemText(0).contains(QStringLiteral("原生")),
           "Translated scenario backend label preserves the backend identifier");

    // Exercise the actual MainWindow deletion controller without a visible
    // native window. All dialog events use the isolated offscreen platform.
    const QString projectFile = sandbox.filePath(QStringLiteral("references.firecae"));
    QString error;
    expect(FcProjectSerializer::save(project, projectFile, &error), "Reference test project saves");
    MainWindow window;
    expect(window.openProjectFile(projectFile), "Reference test project opens in controller");
    auto* treeWidget = window.findChild<ModelTreeWidget*>();
    auto* deleteAction = window.findChild<QAction*>(QStringLiteral("DeleteObjectAction"));
    if (!deleteAction) deleteAction = window.findChild<QAction*>(QStringLiteral("DeleteAction"));
    expect(treeWidget && deleteAction, "Model-tree delete action exists");
    if (treeWidget && deleteAction) {
        treeWidget->selectObjectById(surface->id(), true);
        bool referencePrompt = false;
        QTimer closeGuard;
        closeGuard.setInterval(30);
        QObject::connect(&closeGuard, &QTimer::timeout, &window, [&]() {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->objectName() != QStringLiteral("ObjectReferenceDialog") || !top->isVisible())
                    continue;
                auto* dialog = static_cast<ObjectReferenceDialog*>(top);
                if (dialog == &references) continue;
                referencePrompt = true;
                auto* rows = dialog->findChild<QTableWidget*>(QStringLiteral("ObjectReferenceOwnersTable"));
                for (int row = 0; rows && row < rows->rowCount(); ++row) {
                    if (rows->item(row, 0)->data(Qt::UserRole).toString() == ventB->id()) {
                        rows->selectRow(row);
                        dialog->findChild<QPushButton*>(QStringLiteral("ObjectReferenceLocateButton"))->click();
                        return;
                    }
                }
                dialog->reject();
            }
        });
        closeGuard.start();
        deleteAction->trigger();
        closeGuard.stop();
        expect(referencePrompt, "Deleting a referenced object opens the new explanation dialog");
        auto* tree = treeWidget->findChild<QTreeWidget*>();
        expect(tree && tree->currentItem() &&
               tree->currentItem()->data(0, Qt::UserRole).toString() == ventB->id(),
               "Locate selected same-name owner reaches the exact tree UUID");
        const QString after = sandbox.filePath(QStringLiteral("after.firecae"));
        expect(window.saveProjectFile(after), "Controller state saves after blocked deletion");
        const auto loaded = FcProjectSerializer::load(after);
        expect(loaded.success() && loaded.project->document()->findObject(surface->id()) &&
               collectObjectReferenceOwners(*loaded.project->document(), surface->id()).size() == 2,
               "Blocked deletion preserves the object and every reference");
    }

    // A freshly loaded example is clean. Make a real controller edit before
    // testing protection of unsaved changes, rather than assuming it is dirty.
    if (treeWidget) treeWidget->duplicateObjectRequested(base->id());
    expect(window.windowTitle().contains(QLatin1Char('*')),
           "Duplicating an object marks the controller project modified");
    qunsetenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED");
    auto* newAction = window.findChild<QAction*>(QStringLiteral("NewProjectAction"));
    if (!newAction) newAction = window.findChild<QAction*>(QStringLiteral("NewAction"));
    bool sawUnsaved = false;
    QTimer unsavedGuard;
    unsavedGuard.setInterval(30);
    QObject::connect(&unsavedGuard, &QTimer::timeout, &window, [&]() {
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* prompt = qobject_cast<QMessageBox*>(top);
            if (!prompt || prompt->objectName() != QStringLiteral("UnsavedProjectMessageBox") ||
                !prompt->isVisible()) continue;
            sawUnsaved = true;
            expect(prompt->text().contains(QStringLiteral("未保存")) &&
                   !prompt->text().contains(QStringLiteral("The current project")),
                   "Actual unsaved-project dialog body is Chinese");
            expect(prompt->button(QMessageBox::Save)->text().contains(QStringLiteral("保存")) &&
                   prompt->button(QMessageBox::Cancel)->text().contains(QStringLiteral("取消")),
                   "Actual unsaved-project standard buttons are Chinese");
            renderEvidence(*prompt, QStringLiteral("unsaved-project-zh"));
            prompt->done(QMessageBox::Cancel);
        }
    });
    expect(newAction != nullptr, "New-project action exists");
    if (newAction) {
        unsavedGuard.start();
        newAction->trigger();
        unsavedGuard.stop();
        expect(sawUnsaved, "New-project operation protects unsaved changes");
    }
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    std::cout << "UI feedback regression: " << checks << " checks, " << failures << " failures.\n";
    return failures ? 1 : 0;
}
