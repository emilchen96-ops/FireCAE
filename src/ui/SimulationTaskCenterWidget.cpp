#include "ui/SimulationTaskCenterWidget.h"

#include "simulation/SimulationTaskManager.h"
#include "ui/UiLanguage.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString u(const char* text) { return UiLanguageManager::text(QString::fromUtf8(text)); }
}

SimulationTaskCenterWidget::SimulationTaskCenterWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("SimulationTaskCenterWidget"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("SimulationTaskTable"));
    m_table->setColumnCount(11);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(m_table, 2);
    auto* details = new QWidget(this);
    auto* detailsForm = new QFormLayout(details);
    detailsForm->setContentsMargins(0, 0, 0, 0);
    m_environmentCheck = new QLabel(details);
    m_environmentCheck->setObjectName(QStringLiteral("SimulationTaskEnvironmentCheck"));
    m_environmentCheck->setWordWrap(true);
    m_commandPreview = new QPlainTextEdit(details);
    m_commandPreview->setObjectName(QStringLiteral("SimulationTaskCommandPreview"));
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setMaximumHeight(58);
    m_environmentLabel = new QLabel(details);
    m_environmentLabel->setObjectName(QStringLiteral("SimulationTaskEnvironmentLabel"));
    m_commandLabel = new QLabel(details);
    m_commandLabel->setObjectName(QStringLiteral("SimulationTaskCommandLabel"));
    detailsForm->addRow(m_environmentLabel, m_environmentCheck);
    detailsForm->addRow(m_commandLabel, m_commandPreview);
    root->addWidget(details);
    m_outputTabs = new QTabWidget(this);
    m_outputTabs->setObjectName(QStringLiteral("SimulationTaskOutputTabs"));
    m_log = new QPlainTextEdit(m_outputTabs);
    m_log->setObjectName(QStringLiteral("SimulationTaskLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(10000);
    m_standardOutput = new QPlainTextEdit(m_outputTabs);
    m_standardOutput->setObjectName(QStringLiteral("SimulationTaskStandardOutput"));
    m_standardOutput->setReadOnly(true);
    m_standardOutput->setMaximumBlockCount(10000);
    m_standardOutput->hide();
    m_standardError = new QPlainTextEdit(m_outputTabs);
    m_standardError->setObjectName(QStringLiteral("SimulationTaskStandardError"));
    m_standardError->setReadOnly(true);
    m_standardError->setMaximumBlockCount(10000);
    m_standardError->hide();
    // Keep raw stdout/stderr widgets populated for diagnostics and automated
    // support collection, while presenting users with one chronological log.
    m_outputTabs->addTab(m_log, u("Run Log"));
    root->addWidget(m_outputTabs, 1);
    auto* tools = new QHBoxLayout;
    m_cancel = new QPushButton(this);
    m_cancel->setObjectName(QStringLiteral("SimulationTaskCancelButton"));
    m_retry = new QPushButton(this);
    m_retry->setObjectName(QStringLiteral("SimulationTaskRetryButton"));
    m_openFolder = new QPushButton(this);
    m_openFolder->setObjectName(QStringLiteral("SimulationTaskOpenFolderButton"));
    m_openResults = new QPushButton(this);
    m_openResults->setObjectName(QStringLiteral("SimulationTaskOpenResultsButton"));
    tools->addWidget(m_cancel); tools->addWidget(m_retry); tools->addWidget(m_openFolder);
    tools->addWidget(m_openResults);
    tools->addStretch(); root->addLayout(tools);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this]() { refreshDetails(); });
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->cancel(selectedTaskId());
    });
    connect(m_retry, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->retry(selectedTaskId());
    });
    connect(m_openFolder, &QPushButton::clicked, this, [this]() {
        if (!m_manager) return;
        if (const SimulationTaskRecord* task = m_manager->task(selectedTaskId())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(task->outputDirectory));
        }
    });
    connect(m_openResults, &QPushButton::clicked, this, [this]() {
        if (!m_manager) return;
        if (const SimulationTaskRecord* task = m_manager->task(selectedTaskId())) {
            if (QFileInfo::exists(task->summary.smvFilePath))
                emit openResultsRequested(task->summary.smvFilePath);
        }
    });
    retranslateUi();
    refresh();
}

void SimulationTaskCenterWidget::setManager(SimulationTaskManager* manager)
{
    if (m_manager) disconnect(m_manager, nullptr, this, nullptr);
    m_manager = manager;
    if (m_manager) {
        // The manager may be a sibling destroyed before this widget. Clear the
        // task selection while our controls are still alive, so their teardown
        // signals cannot query a manager whose task records have been freed.
        connect(m_manager.data(), &QObject::destroyed, this, [this]() {
            m_manager.clear();
            refresh();
        });
        connect(m_manager, &SimulationTaskManager::taskAdded,
                this, [this](const QString&) { refresh(); });
        connect(m_manager, &SimulationTaskManager::taskUpdated,
                this, [this](const QString&) { refresh(); });
        connect(m_manager, &SimulationTaskManager::taskOutput,
                this, [this](const QString& id, const QString&) {
                    if (id == selectedTaskId()) refreshDetails();
                });
    }
    refresh();
}

QString SimulationTaskCenterWidget::selectedTaskId() const
{
    const int row = m_table->currentRow();
    const QTableWidgetItem* item = row >= 0 ? m_table->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void SimulationTaskCenterWidget::retranslateUi()
{
    m_environmentLabel->setText(u("Environment:"));
    m_commandLabel->setText(u("Command preview:"));
    m_table->setHorizontalHeaderLabels({u("State"), u("Project / CHID"), u("Scene"),
                                        u("Solver"), u("Processes"), u("Threads"),
                                        u("Started"), u("FDS Time"), u("Elapsed"),
                                        u("Remaining"), u("Output Directory")});
    m_cancel->setText(u("Cancel Task"));
    m_retry->setText(u("Retry Task"));
    m_openFolder->setText(u("Open Output Folder"));
    m_openResults->setText(u("Open Results"));
    if (m_outputTabs) {
        m_outputTabs->setTabText(0, u("Run Log"));
    }
    // Update only language-dependent cells. Rebuilding the table or refreshing
    // the log here would move selection/cursor/scroll positions while a task
    // is running. UUID lookup retains the business identity of every row.
    if (m_manager) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* item = m_table->item(row, 0);
            if (!item) continue;
            if (const auto* task = m_manager->task(item->data(Qt::UserRole).toString())) {
                item->setText(UiLanguageManager::text(simulationTaskStateName(task->state)));
            }
        }
    }
}

void SimulationTaskCenterWidget::refresh()
{
    const QString previous = selectedTaskId();
    const QVector<SimulationTaskRecord> tasks = m_manager
                                                    ? m_manager->tasks()
                                                    : QVector<SimulationTaskRecord>{};
    m_table->setRowCount(tasks.size());
    for (int row = 0; row < tasks.size(); ++row) {
        const SimulationTaskRecord& task = tasks[row];
        const QStringList values = {
            UiLanguageManager::text(simulationTaskStateName(task.state)),
            QStringLiteral("%1 / %2").arg(task.projectName, task.chid),
            task.sceneName, task.backendName,
            QString::number(task.request.processCount),
            QString::number(task.request.threadCount),
            task.startedAt.isValid()
                ? task.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("—"),
            task.endTime > 0.0
                ? QStringLiteral("%1 / %2 s (%3%)")
                      .arg(task.simulationTime, 0, 'g', 7)
                      .arg(task.endTime, 0, 'g', 7)
                      .arg(task.progress * 100.0, 0, 'f', 1)
                : QStringLiteral("%1 s").arg(task.simulationTime, 0, 'g', 7),
            QStringLiteral("%1 s").arg(task.elapsedMilliseconds / 1000.0, 0, 'f', 1),
            task.estimatedRemainingMilliseconds >= 0
                ? QStringLiteral("%1 s").arg(
                      task.estimatedRemainingMilliseconds / 1000.0, 0, 'f', 1)
                : QStringLiteral("—"),
            QDir::toNativeSeparators(task.outputDirectory)};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (column == 0) item->setData(Qt::UserRole, task.taskId);
            m_table->setItem(row, column, item);
        }
        if (task.taskId == previous) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0) {
        m_table->selectRow(m_table->rowCount() - 1);
    }
    refreshDetails();
}

void SimulationTaskCenterWidget::refreshDetails()
{
    const SimulationTaskRecord* task = m_manager
                                           ? m_manager->task(selectedTaskId())
                                           : nullptr;
    if (!task) {
        m_log->clear();
        m_standardOutput->clear();
        m_standardError->clear();
        m_commandPreview->clear();
        m_environmentCheck->clear();
        m_cancel->setEnabled(false); m_retry->setEnabled(false);
        m_openFolder->setEnabled(false); m_openResults->setEnabled(false); return;
    }
    m_log->setPlainText(task->log.isEmpty() ? task->errorMessage : task->log);
    m_standardOutput->setPlainText(task->standardOutput);
    m_standardError->setPlainText(
        task->standardError.isEmpty() ? task->errorMessage : task->standardError);
    m_commandPreview->setPlainText(task->commandLine);
    m_environmentCheck->setText(task->environmentCheck);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    const bool active = task->state == SimulationTaskState::Waiting ||
                        task->state == SimulationTaskState::Preparing ||
                        task->state == SimulationTaskState::Running;
    const bool terminal = task->state == SimulationTaskState::Completed ||
                          task->state == SimulationTaskState::Failed ||
                          task->state == SimulationTaskState::Cancelled;
    m_cancel->setEnabled(active);
    m_retry->setEnabled(terminal);
    m_openFolder->setEnabled(!task->outputDirectory.isEmpty());
    m_openResults->setEnabled(task->state == SimulationTaskState::Completed &&
                              QFileInfo::exists(task->summary.smvFilePath));
}
