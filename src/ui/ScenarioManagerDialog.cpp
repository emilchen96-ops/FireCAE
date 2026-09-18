#include "ui/ScenarioManagerDialog.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "results/FdsResultComparator.h"
#include "results/FdsResultScanner.h"
#include "ui/UiLanguage.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>
#include <functional>

namespace
{
constexpr int kUuidRole = Qt::UserRole;

QString uiText(const char* english)
{
    return UiLanguageManager::text(QString::fromUtf8(english));
}

void appendObject(QTreeWidgetItem* parent, const FcObject::Ptr& object)
{
    if (!parent || !object) return;
    auto* item = new QTreeWidgetItem(parent, {
        object->type() == FcObjectType::Group
            ? UiLanguageManager::text(object->name()) : object->name()});
    item->setData(0, kUuidRole, object->id());
    item->setToolTip(0, QStringLiteral("UUID: %1").arg(object->id()));
    if (object->type() != FcObjectType::Group) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    } else {
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    }
    for (const FcObject::Ptr& child : object->children()) appendObject(item, child);
}

void forEachItem(QTreeWidgetItem* item,
                 const std::function<void(QTreeWidgetItem*)>& visitor)
{
    if (!item) return;
    visitor(item);
    for (int index = 0; index < item->childCount(); ++index) {
        forEachItem(item->child(index), visitor);
    }
}

QString firstScenarioSmv(const FcScenario& scenario)
{
    const QString configured = scenario.outputDirectory.trimmed();
    if (configured.isEmpty()) return {};
    const QFileInfo info(configured);
    if (info.isFile() && info.suffix().compare(QStringLiteral("smv"),
                                               Qt::CaseInsensitive) == 0) {
        return info.absoluteFilePath();
    }
    const QDir root(info.absoluteFilePath());
    const QStringList taskRoots = {
        root.filePath(QStringLiteral(".firecae-runs")),
        root.filePath(scenario.id + QStringLiteral("/.firecae-runs"))};
    QDateTime latest;
    QString latestSmv;
    for (const QString& taskRoot : taskRoots) {
        for (const QFileInfo& task : QDir(taskRoot).entryInfoList(
                 QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QFile result(QDir(task.absoluteFilePath()).filePath(QStringLiteral("run-result.json")));
            if (!result.open(QIODevice::ReadOnly)) continue;
            const QJsonObject metadata = QJsonDocument::fromJson(result.readAll()).object();
            if (!metadata.value(QStringLiteral("success")).toBool()) continue;
            const QString smv = metadata.value(QStringLiteral("smvPath")).toString();
            if (!QFileInfo(smv).isFile() || QFileInfo(smv).completeBaseName().compare(
                    scenario.chid, Qt::CaseInsensitive) != 0) continue;
            const QDateTime finished = QDateTime::fromString(
                metadata.value(QStringLiteral("finishedAt")).toString(), Qt::ISODateWithMs);
            if (finished.isValid() && (!latest.isValid() || finished > latest)) {
                latest = finished;
                latestSmv = smv;
            }
        }
    }
    if (!latestSmv.isEmpty()) return latestSmv;
    const QString exact = root.filePath(scenario.chid + QStringLiteral(".smv"));
    return QFileInfo(exact).isFile() ? exact : QString{};
}

class ParameterStudyDialog final : public QDialog
{
public:
    ParameterStudyDialog(const FcProject* project, const FcScenario& base,
                         QWidget* parent = nullptr)
        : QDialog(parent), m_base(base)
    {
        setObjectName(QStringLiteral("ParameterStudyDialog"));
        setWindowTitle(uiText("Parameter Study Matrix"));
        resize(680, 430);
        auto* root = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        m_prefix = new QLineEdit(uiText("Study"), this);
        m_firstObject = new QComboBox(this);
        m_secondObject = new QComboBox(this);
        m_secondObject->addItem(uiText("(none)"), QString{});
        if (project && project->document()) {
            for (const auto& group : project->document()->groups()) {
                std::function<void(const FcObject::Ptr&)> collect =
                    [&](const FcObject::Ptr& object) {
                    if (!object) return;
                    if (object->type() != FcObjectType::Group) {
                        const QString label = QStringLiteral("%1 — %2")
                                                  .arg(object->name(), object->id());
                        m_firstObject->addItem(label, object->id());
                        m_secondObject->addItem(label, object->id());
                    }
                    for (const FcObject::Ptr& child : object->children()) collect(child);
                };
                for (const FcObject::Ptr& child : group->children()) collect(child);
            }
        }
        m_firstParameter = new QLineEdit(QStringLiteral("HRRPUA"), this);
        m_firstValues = new QLineEdit(QStringLiteral("250; 500; 750"), this);
        m_secondParameter = new QLineEdit(this);
        m_secondValues = new QLineEdit(this);
        m_prefix->setObjectName(QStringLiteral("ParameterStudyPrefixEdit"));
        m_firstObject->setObjectName(QStringLiteral("ParameterStudyFirstObjectCombo"));
        m_firstParameter->setObjectName(QStringLiteral("ParameterStudyFirstParameterEdit"));
        m_firstValues->setObjectName(QStringLiteral("ParameterStudyFirstValuesEdit"));
        m_secondObject->setObjectName(QStringLiteral("ParameterStudySecondObjectCombo"));
        m_secondParameter->setObjectName(QStringLiteral("ParameterStudySecondParameterEdit"));
        m_secondValues->setObjectName(QStringLiteral("ParameterStudySecondValuesEdit"));
        m_firstValues->setToolTip(uiText(
            "Use semicolons between complete raw values; commas remain valid inside arrays."));
        m_secondValues->setToolTip(m_firstValues->toolTip());
        form->addRow(uiText("Scenario name prefix:"), m_prefix);
        form->addRow(uiText("First object:"), m_firstObject);
        form->addRow(uiText("First parameter:"), m_firstParameter);
        form->addRow(uiText("First values (; separated):"), m_firstValues);
        form->addRow(uiText("Second object (optional):"), m_secondObject);
        form->addRow(uiText("Second parameter:"), m_secondParameter);
        form->addRow(uiText("Second values (; separated):"), m_secondValues);
        root->addLayout(form);
        m_summary = new QLabel(this);
        m_summary->setWordWrap(true);
        root->addWidget(m_summary);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        const auto refresh = [this]() {
            const int first = values(m_firstValues->text()).size();
            const bool secondEnabled = !m_secondObject->currentData().toString().isEmpty();
            const int second = secondEnabled ? values(m_secondValues->text()).size() : 1;
            m_summary->setText(
                uiText("Cartesian matrix: %1 × %2 = %3 scenario(s). "
                               "Each scenario stores only parameter differences from the base.")
                    .arg(first).arg(second).arg(first * second));
        };
        connect(m_firstValues, &QLineEdit::textChanged, this, refresh);
        connect(m_secondValues, &QLineEdit::textChanged, this, refresh);
        connect(m_secondObject, &QComboBox::currentIndexChanged, this, refresh);
        refresh();
    }

    QVector<FcScenario> scenarios() const
    {
        QVector<FcScenario> result;
        const QStringList first = values(m_firstValues->text());
        QStringList second = values(m_secondValues->text());
        const bool useSecond = !m_secondObject->currentData().toString().isEmpty() &&
                               !m_secondParameter->text().trimmed().isEmpty();
        if (!useSecond) second = {QString{}};
        int index = 1;
        for (const QString& firstValue : first) {
            for (const QString& secondValue : second) {
                FcScenario scenario = m_base;
                scenario.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                scenario.name = QStringLiteral("%1 %2").arg(
                    m_prefix->text().trimmed()).arg(index, 3, 10, QLatin1Char('0'));
                scenario.chid = m_base.chid + QStringLiteral("_p%1").arg(index);
                scenario.parameterOverrides.append(
                    {m_firstObject->currentData().toString(),
                     m_firstParameter->text().trimmed().toUpper(),
                     firstValue, {}, false});
                if (useSecond) {
                    scenario.parameterOverrides.append(
                        {m_secondObject->currentData().toString(),
                         m_secondParameter->text().trimmed().toUpper(),
                         secondValue, {}, false});
                }
                result.append(scenario);
                ++index;
            }
        }
        return result;
    }

private:
    static QStringList values(const QString& text)
    {
        QStringList result;
        for (const QString& value : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString normalized = value.trimmed();
            if (!normalized.isEmpty()) result.append(normalized);
        }
        return result;
    }
    FcScenario m_base;
    QLineEdit* m_prefix = nullptr;
    QComboBox* m_firstObject = nullptr;
    QLineEdit* m_firstParameter = nullptr;
    QLineEdit* m_firstValues = nullptr;
    QComboBox* m_secondObject = nullptr;
    QLineEdit* m_secondParameter = nullptr;
    QLineEdit* m_secondValues = nullptr;
    QLabel* m_summary = nullptr;
};
}

ScenarioManagerDialog::ScenarioManagerDialog(FcProject* project, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("ScenarioManagerDialog"));
    setWindowTitle(uiText("Scenario and Batch Case Manager"));
    resize(1120, 760);
    if (m_project) {
        m_scenarios = m_project->scenarios();
        m_activeId = m_project->activeScenarioId();
        m_defaultId = m_project->defaultScenarioId();
    }
    auto* root = new QVBoxLayout(this);
    auto* content = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ScenarioList"));
    left->addWidget(new QLabel(uiText("Scenarios"), this));
    left->addWidget(m_list, 1);
    auto* add = new QPushButton(uiText("New"), this);
    auto* duplicate = new QPushButton(uiText("Duplicate"), this);
    auto* rename = new QPushButton(uiText("Rename"), this);
    auto* remove = new QPushButton(uiText("Delete"), this);
    auto* makeDefault = new QPushButton(uiText("Set Default"), this);
    auto* compare = new QPushButton(uiText("Compare"), this);
    compare->setObjectName(QStringLiteral("ScenarioCompareButton"));
    auto* parameterStudy = new QPushButton(uiText("Parameter Study..."), this);
    add->setObjectName(QStringLiteral("ScenarioNewButton"));
    duplicate->setObjectName(QStringLiteral("ScenarioDuplicateButton"));
    rename->setObjectName(QStringLiteral("ScenarioRenameButton"));
    remove->setObjectName(QStringLiteral("ScenarioDeleteButton"));
    makeDefault->setObjectName(QStringLiteral("ScenarioSetDefaultButton"));
    parameterStudy->setObjectName(QStringLiteral("ScenarioParameterStudyButton"));
    for (QPushButton* button : {add, duplicate, rename, remove, makeDefault,
                                compare, parameterStudy}) {
        left->addWidget(button);
    }
    content->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    auto* settings = new QFormLayout;
    m_name = new QLineEdit(this);
    m_chid = new QLineEdit(this);
    m_outputDirectory = new QLineEdit(this);
    m_backend = new QComboBox(this);
    m_backend->addItem(uiText("Native FDS — Serial CPU"),
                       QStringLiteral("fds.serial.cpu"));
    m_backend->addItem(uiText("Native FDS — OpenMP CPU"),
                       QStringLiteral("fds.openmp.cpu"));
    m_backend->addItem(uiText("Native FDS — MPI CPU"),
                       QStringLiteral("fds.mpi.cpu"));
    m_processes = new QSpinBox(this);
    m_processes->setRange(1, 1024);
    m_name->setObjectName(QStringLiteral("ScenarioNameEdit"));
    m_chid->setObjectName(QStringLiteral("ScenarioChidEdit"));
    m_outputDirectory->setObjectName(QStringLiteral("ScenarioOutputDirectoryEdit"));
    m_backend->setObjectName(QStringLiteral("ScenarioBackendCombo"));
    m_processes->setObjectName(QStringLiteral("ScenarioProcessCountSpin"));
    settings->addRow(uiText("Name:"), m_name);
    settings->addRow(uiText("CHID:"), m_chid);
    settings->addRow(uiText("Output directory:"), m_outputDirectory);
    settings->addRow(uiText("Solver:"), m_backend);
    settings->addRow(uiText("Processes:"), m_processes);
    right->addLayout(settings);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    right->addWidget(m_status);
    right->addWidget(new QLabel(
        uiText("Enabled objects (unchecked objects are disabled only in this scenario)"),
        this));
    m_objects = new QTreeWidget(this);
    m_objects->setObjectName(QStringLiteral("ScenarioObjectTree"));
    m_objects->setHeaderHidden(true);
    if (m_project && m_project->document()) {
        for (const auto& group : m_project->document()->groups()) {
            auto* groupItem = new QTreeWidgetItem(m_objects, {UiLanguageManager::text(group->name())});
            groupItem->setData(0, kUuidRole, group->id());
            groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsUserCheckable);
            for (const FcObject::Ptr& child : group->children()) appendObject(groupItem, child);
        }
    }
    right->addWidget(m_objects, 2);
    right->addWidget(new QLabel(uiText("Parameter overrides"), this));
    m_overrides = new QTableWidget(this);
    m_overrides->setObjectName(QStringLiteral("ScenarioOverrideTable"));
    m_overrides->setColumnCount(3);
    m_overrides->setHorizontalHeaderLabels(
        {uiText("Object UUID"), uiText("Parameter"), uiText("Value")});
    m_overrides->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_overrides->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_overrides, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        FcScenario* scenario = loadedScenario();
        if (m_loading || !scenario || !item || item->column() != 2 ||
            item->row() < 0 || item->row() >= scenario->parameterOverrides.size()) return;
        auto& entry = scenario->parameterOverrides[item->row()];
        if (entry.reference) {
            entry.targetObjectIds = item->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString& id : entry.targetObjectIds) id = id.trimmed();
        } else {
            entry.value = item->text().trimmed();
        }
    });
    right->addWidget(m_overrides, 1);
    auto* overrideTools = new QHBoxLayout;
    auto* addOverrideButton = new QPushButton(uiText("Add Override..."), this);
    auto* removeOverrideButton = new QPushButton(uiText("Remove Override"), this);
    addOverrideButton->setObjectName(QStringLiteral("ScenarioAddOverrideButton"));
    removeOverrideButton->setObjectName(QStringLiteral("ScenarioRemoveOverrideButton"));
    overrideTools->addWidget(addOverrideButton);
    overrideTools->addWidget(removeOverrideButton);
    overrideTools->addStretch();
    right->addLayout(overrideTools);
    content->addLayout(right, 3);
    root->addLayout(content, 1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int row) { loadScenario(row); });
    connect(m_objects, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int) {
        FcScenario* scenario = loadedScenario();
        if (m_loading || !scenario || !item) return;
        const QString id = item->data(0, kUuidRole).toString();
        if (id.isEmpty() || !(item->flags() & Qt::ItemIsUserCheckable)) return;
        if (item->checkState(0) == Qt::Checked) scenario->disabledObjectIds.remove(id);
        else scenario->disabledObjectIds.insert(id);
        m_status->setText(uiText("%1 disabled object(s), %2 override(s)")
                              .arg(scenario->disabledObjectIds.size())
                              .arg(scenario->parameterOverrides.size()));
    });
    connect(add, &QPushButton::clicked, this, &ScenarioManagerDialog::addScenario);
    connect(duplicate, &QPushButton::clicked,
            this, &ScenarioManagerDialog::duplicateScenario);
    connect(rename, &QPushButton::clicked, this, &ScenarioManagerDialog::renameScenario);
    connect(remove, &QPushButton::clicked, this, &ScenarioManagerDialog::removeScenario);
    connect(makeDefault, &QPushButton::clicked,
            this, &ScenarioManagerDialog::setDefaultScenario);
    connect(compare, &QPushButton::clicked, this, &ScenarioManagerDialog::compareScenario);
    connect(parameterStudy, &QPushButton::clicked,
            this, &ScenarioManagerDialog::createParameterStudy);
    connect(addOverrideButton, &QPushButton::clicked,
            this, &ScenarioManagerDialog::addOverride);
    connect(removeOverrideButton, &QPushButton::clicked,
            this, &ScenarioManagerDialog::removeOverride);
    connect(buttons, &QDialogButtonBox::accepted, this, &ScenarioManagerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuildScenarioList();
    if (!m_scenarios.isEmpty()) {
        int activeIndex = 0;
        for (int index = 0; index < m_scenarios.size(); ++index) {
            if (m_scenarios[index].id == m_activeId) { activeIndex = index; break; }
        }
        m_list->setCurrentRow(activeIndex);
        if (m_loadedIndex != activeIndex) loadScenario(activeIndex);
    }
}

FcScenario* ScenarioManagerDialog::loadedScenario()
{
    return m_loadedIndex >= 0 && m_loadedIndex < m_scenarios.size()
               ? &m_scenarios[m_loadedIndex] : nullptr;
}

void ScenarioManagerDialog::storeLoadedScenario()
{
    FcScenario* scenario = loadedScenario();
    if (!scenario || m_loading) return;
    scenario->name = m_name->text().trimmed();
    scenario->chid = m_chid->text().trimmed();
    scenario->outputDirectory = m_outputDirectory->text().trimmed();
    scenario->solverBackendId = m_backend->currentData().toString();
    scenario->processCount = m_processes->value();
}

void ScenarioManagerDialog::loadScenario(int index)
{
    storeLoadedScenario();
    m_loadedIndex = index;
    FcScenario* scenario = loadedScenario();
    m_loading = true;
    if (scenario) {
        m_name->setText(scenario->name);
        m_chid->setText(scenario->chid);
        m_outputDirectory->setText(scenario->outputDirectory);
        const int backendIndex = m_backend->findData(scenario->solverBackendId);
        m_backend->setCurrentIndex(backendIndex < 0 ? 0 : backendIndex);
        m_processes->setValue(scenario->processCount);
    }
    m_loading = false;
    rebuildObjectStates();
    rebuildOverrides();
}

void ScenarioManagerDialog::rebuildScenarioList()
{
    const QSignalBlocker blocker(m_list);
    const int previous = m_list->currentRow();
    m_list->clear();
    for (const FcScenario& scenario : m_scenarios) {
        QString suffix;
        if (scenario.id == m_defaultId) suffix += uiText(" [Default]");
        if (scenario.id == m_activeId) suffix += uiText(" [Active]");
        auto* item = new QListWidgetItem(scenario.name + suffix, m_list);
        item->setData(kUuidRole, scenario.id);
    }
    if (!m_scenarios.isEmpty()) m_list->setCurrentRow(qBound(0, previous, m_scenarios.size() - 1));
}

void ScenarioManagerDialog::rebuildObjectStates()
{
    FcScenario* scenario = loadedScenario();
    if (!scenario) return;
    m_loading = true;
    for (int index = 0; index < m_objects->topLevelItemCount(); ++index) {
        forEachItem(m_objects->topLevelItem(index), [scenario](QTreeWidgetItem* item) {
            if (!(item->flags() & Qt::ItemIsUserCheckable)) return;
            item->setCheckState(
                0, scenario->disabledObjectIds.contains(
                       item->data(0, kUuidRole).toString()) ? Qt::Unchecked : Qt::Checked);
        });
    }
    m_loading = false;
    m_status->setText(uiText("%1 disabled object(s), %2 override(s)")
                          .arg(scenario->disabledObjectIds.size())
                          .arg(scenario->parameterOverrides.size()));
}

void ScenarioManagerDialog::rebuildOverrides()
{
    const QSignalBlocker blocker(m_overrides);
    const FcScenario* scenario = loadedScenario();
    m_overrides->setRowCount(scenario ? scenario->parameterOverrides.size() : 0);
    if (!scenario) return;
    for (int row = 0; row < scenario->parameterOverrides.size(); ++row) {
        const FcScenarioParameterOverride& entry = scenario->parameterOverrides[row];
        m_overrides->setItem(row, 0, new QTableWidgetItem(entry.objectId));
        m_overrides->setItem(row, 1, new QTableWidgetItem(entry.parameterKey));
        m_overrides->item(row, 0)->setFlags(m_overrides->item(row, 0)->flags() & ~Qt::ItemIsEditable);
        m_overrides->item(row, 1)->setFlags(m_overrides->item(row, 1)->flags() & ~Qt::ItemIsEditable);
        m_overrides->setItem(row, 2, new QTableWidgetItem(
            entry.reference ? entry.targetObjectIds.join(QLatin1Char(',')) : entry.value));
    }
}

void ScenarioManagerDialog::addScenario()
{
    storeLoadedScenario();
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, uiText("New Scenario"), uiText("Name:"),
        QLineEdit::Normal, uiText("Scenario %1").arg(m_scenarios.size() + 1),
        &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    const QString baseChid = m_project ? m_project->chid() : QStringLiteral("case");
    FcScenario scenario = FcScenario::create(
        name, baseChid + QStringLiteral("_%1").arg(m_scenarios.size() + 1));
    m_scenarios.append(scenario);
    m_loadedIndex = -1;
    rebuildScenarioList();
    m_list->setCurrentRow(m_scenarios.size() - 1);
}

void ScenarioManagerDialog::duplicateScenario()
{
    storeLoadedScenario();
    FcScenario* source = loadedScenario();
    if (!source) return;
    FcScenario copy = *source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name += uiText(" Copy");
    copy.chid += QStringLiteral("_copy");
    m_scenarios.append(copy);
    m_loadedIndex = -1;
    rebuildScenarioList();
    m_list->setCurrentRow(m_scenarios.size() - 1);
}

void ScenarioManagerDialog::renameScenario()
{
    FcScenario* scenario = loadedScenario();
    if (!scenario) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, uiText("Rename Scenario"), uiText("Name:"),
        QLineEdit::Normal, scenario->name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    scenario->name = name;
    m_name->setText(name);
    rebuildScenarioList();
}

void ScenarioManagerDialog::removeScenario()
{
    if (m_scenarios.size() <= 1 || !loadedScenario()) return;
    const QString removedId = loadedScenario()->id;
    m_scenarios.removeAt(m_loadedIndex);
    if (m_activeId == removedId) m_activeId = m_scenarios.front().id;
    if (m_defaultId == removedId) m_defaultId = m_scenarios.front().id;
    m_loadedIndex = -1;
    rebuildScenarioList();
    m_list->setCurrentRow(0);
}

void ScenarioManagerDialog::setDefaultScenario()
{
    storeLoadedScenario();
    if (!loadedScenario()) return;
    m_defaultId = loadedScenario()->id;
    rebuildScenarioList();
}

void ScenarioManagerDialog::addOverride()
{
    FcScenario* scenario = loadedScenario();
    if (!scenario || !m_project || !m_project->document()) return;
    QStringList labels;
    QStringList ids;
    for (const auto& group : m_project->document()->groups()) {
        std::function<void(const FcObject::Ptr&)> collect = [&](const FcObject::Ptr& object) {
            if (!object) return;
            if (object->type() != FcObjectType::Group) {
                labels.append(QStringLiteral("%1 — %2").arg(object->name(), object->id()));
                ids.append(object->id());
            }
            for (const FcObject::Ptr& child : object->children()) collect(child);
        };
        for (const FcObject::Ptr& child : group->children()) collect(child);
    }
    if (labels.isEmpty()) return;
    bool accepted = false;
    const QString label = QInputDialog::getItem(
        this, uiText("Parameter Override"), uiText("Object:"),
        labels, 0, false, &accepted);
    if (!accepted) return;
    const int objectIndex = labels.indexOf(label);
    const QString parameter = QInputDialog::getText(
        this, uiText("Parameter Override"), uiText("FDS parameter:"),
        QLineEdit::Normal, {}, &accepted).trimmed().toUpper();
    if (!accepted || parameter.isEmpty()) return;
    const QString value = QInputDialog::getText(
        this, uiText("Parameter Override"), uiText("Raw value:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || value.isEmpty()) return;
    FcScenarioParameterOverride entry;
    entry.objectId = ids.value(objectIndex);
    entry.parameterKey = parameter;
    entry.value = value;
    bool replaced = false;
    for (FcScenarioParameterOverride& existing : scenario->parameterOverrides) {
        if (existing.objectId == entry.objectId &&
            existing.parameterKey.compare(entry.parameterKey, Qt::CaseInsensitive) == 0) {
            existing = entry; replaced = true; break;
        }
    }
    if (!replaced) scenario->parameterOverrides.append(entry);
    rebuildOverrides();
    rebuildObjectStates();
}

void ScenarioManagerDialog::removeOverride()
{
    FcScenario* scenario = loadedScenario();
    const int row = m_overrides->currentRow();
    if (!scenario || row < 0 || row >= scenario->parameterOverrides.size()) return;
    scenario->parameterOverrides.removeAt(row);
    rebuildOverrides();
    rebuildObjectStates();
}

void ScenarioManagerDialog::compareScenario()
{
    storeLoadedScenario();
    const FcScenario* scenario = loadedScenario();
    const auto base = std::find_if(m_scenarios.cbegin(), m_scenarios.cend(),
                                   [this](const FcScenario& value) {
                                       return value.id == m_defaultId;
                                   });
    if (!scenario || base == m_scenarios.cend()) return;
    QString report =
        uiText("%1 compared with %2\nCHID: %3 → %4\nDisabled objects: %5 → %6\n"
                       "Parameter overrides: %7 → %8\nSolver: %9 (%10 process(es))")
            .arg(scenario->name, base->name, base->chid, scenario->chid)
            .arg(base->disabledObjectIds.size()).arg(scenario->disabledObjectIds.size())
            .arg(base->parameterOverrides.size()).arg(scenario->parameterOverrides.size())
            .arg(scenario->solverBackendId).arg(scenario->processCount);

    const QString referenceSmv = firstScenarioSmv(*base);
    const QString candidateSmv = firstScenarioSmv(*scenario);
    report += uiText("\n\nResult comparison:\n");
    if (referenceSmv.isEmpty() || candidateSmv.isEmpty()) {
        report += uiText(
            "Unavailable. Run both scenarios or set each output directory to a folder "
            "containing its .smv and CSV result files.\nReference: %1\nCandidate: %2")
                      .arg(referenceSmv.isEmpty() ? uiText("not found")
                                                  : QDir::toNativeSeparators(referenceSmv),
                           candidateSmv.isEmpty() ? uiText("not found")
                                                  : QDir::toNativeSeparators(candidateSmv));
    } else {
        const FdsResultComparison comparison =
            FdsResultComparator::compareSmvFiles(referenceSmv, candidateSmv);
        if (!comparison.success()) {
            report += uiText("Could not compare physical CSV results: %1")
                          .arg(comparison.errorMessage);
        } else {
            const int passedQuantities = static_cast<int>(std::count_if(
                comparison.quantities.cbegin(), comparison.quantities.cend(),
                [](const FdsCsvQuantityComparison& quantity) {
                    return quantity.withinTolerance;
                }));
            report += uiText(
                "%1\nMatched CSV files: %2\nCompared quantities: %3 (%4 within tolerance)\n"
                "Relative tolerance: %5%\nReference provenance: %6\nCandidate provenance: %7")
                          .arg(comparison.passed() ? uiText("PASS")
                                                   : uiText("DIFFERENCES FOUND"))
                          .arg(comparison.matchedFileCount)
                          .arg(comparison.quantities.size())
                          .arg(passedQuantities)
                          .arg(comparison.relativeTolerance * 100.0, 0, 'g', 4)
                          .arg(QDir::toNativeSeparators(referenceSmv),
                               QDir::toNativeSeparators(candidateSmv));
            if (!comparison.warnings.isEmpty()) {
                report += uiText("\nWarnings:\n- %1")
                              .arg(comparison.warnings.join(QStringLiteral("\n- ")));
            }
        }
    }
    QMessageBox::information(this, uiText("Scenario Difference"), report);
}

void ScenarioManagerDialog::createParameterStudy()
{
    storeLoadedScenario();
    const FcScenario* base = loadedScenario();
    if (!base) return;
    ParameterStudyDialog dialog(m_project, *base, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QVector<FcScenario> generated = dialog.scenarios();
    if (generated.isEmpty()) {
        QMessageBox::warning(this, uiText("Parameter Study"),
                             uiText("Enter at least one value."));
        return;
    }
    for (const FcScenario& scenario : generated) m_scenarios.append(scenario);
    m_loadedIndex = -1;
    rebuildScenarioList();
    m_list->setCurrentRow(m_scenarios.size() - generated.size());
}

void ScenarioManagerDialog::accept()
{
    storeLoadedScenario();
    if (m_scenarios.isEmpty()) return;
    for (const FcScenario& scenario : m_scenarios) {
        if (!scenario.isValid() || scenario.chid.trimmed().isEmpty()) {
            QMessageBox::warning(this, uiText("Scenario Validation"),
                                 uiText("Every scenario needs a name, UUID, and CHID."));
            return;
        }
    }
    if (loadedScenario()) m_activeId = loadedScenario()->id;
    if (m_project) m_project->setScenarios(m_scenarios, m_activeId, m_defaultId);
    QDialog::accept();
}
