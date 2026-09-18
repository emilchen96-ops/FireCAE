#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsExamples.h"
#include "fds/FdsWriter.h"
#include "ui/ScenarioManagerDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
    if (!condition) ++failures;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAEReacceptanceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ScenarioDialog"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    auto project = FdsExamples::createActivateVentsProject();
    const auto mesh = std::dynamic_pointer_cast<FcFdsNamelist>(
        project->document()->meshesGroup()->children().front());
    check(mesh != nullptr, "M04 generic mesh fixture exists");
    if (!mesh) return 1;
    const QString scenarioId = project->activeScenarioId();
    FcScenarioParameterOverride entry;
    entry.objectId = mesh->id();
    entry.parameterKey = QStringLiteral("IJK");
    entry.value = QStringLiteral("24,12,12");
    project->setScenarioOverride(scenarioId, entry);
    const QString secondId = project->addScenario(QStringLiteral("Second"));
    {
        ScenarioManagerDialog dialog(project.get());
        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ScenarioOverrideTable"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        auto* list = dialog.findChild<QListWidget*>();
        check(table && buttons && list && table->rowCount() == 1,
              "M04 existing override is displayed for editing");
        if (!table || !buttons || !list || table->rowCount() != 1) return 1;
        table->item(0, 2)->setText(QStringLiteral("12,12,12"));
        list->setCurrentRow(1);
        list->setCurrentRow(0);
        check(table->item(0, 2)->text() == QStringLiteral("12,12,12"),
              "M04 edited override survives switching scenarios inside dialog");
        buttons->button(QDialogButtonBox::Ok)->click();
        check(dialog.result() == QDialog::Accepted, "M04 valid edited dialog accepts");
    }
    check(project->scenario(scenarioId)->parameterOverrides.front().value ==
              QStringLiteral("12,12,12"),
          "M04 edited value is committed to project on OK");
    check(project->scenario(secondId)->parameterOverrides.isEmpty(),
          "M04 edit does not leak to another scenario");
    const auto output = FdsWriter::render(*project);
    check(output.success() && output.text.contains(QStringLiteral("IJK=12,12,12")),
          "M04 actual solver input contains the edited value");
    // Establish an independent known value; a failed OK assertion must not be
    // misreported as a separate cancellation defect.
    entry.value = QStringLiteral("12,12,12");
    project->setScenarioOverride(scenarioId, entry);
    {
        ScenarioManagerDialog dialog(project.get());
        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ScenarioOverrideTable"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        table->item(0, 2)->setText(QStringLiteral("6,6,6"));
        buttons->button(QDialogButtonBox::Cancel)->click();
    }
    check(project->scenario(scenarioId)->parameterOverrides.front().value ==
              QStringLiteral("12,12,12"),
          "M04 cancel discards local edits without changing saved scenario value");
    std::cout << "Reacceptance scenario UI failures: " << failures << '\n';
    return failures ? 1 : 0;
}
