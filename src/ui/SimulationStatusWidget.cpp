#include "ui/SimulationStatusWidget.h"
#include "simulation/SimulationTaskManager.h"
#include "ui/UiLanguage.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QToolButton>

namespace {
QString u(const char* text) { return UiLanguageManager::text(QString::fromUtf8(text)); }
bool active(SimulationTaskState state)
{
    return state == SimulationTaskState::Waiting || state == SimulationTaskState::Preparing ||
           state == SimulationTaskState::Running || state == SimulationTaskState::Stopping;
}
}

SimulationStatusWidget::SimulationStatusWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("SimulationStatusWidget"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    m_details = new QToolButton(this);
    m_details->setObjectName(QStringLiteral("SimulationStatusDetailsButton"));
    m_details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_details->setAutoRaise(true);
    m_stop = new QToolButton(this);
    m_stop->setObjectName(QStringLiteral("SimulationStatusStopButton"));
    m_stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    layout->addWidget(m_details);
    layout->addWidget(m_stop);
    connect(m_details, &QToolButton::clicked, this, [this]() {
        emit detailsRequested(m_detailsTaskId);
    });
    connect(m_stop, &QToolButton::clicked, this, [this]() {
        // cancel emits synchronous updates, which rebuild and clear member IDs.
        const QString taskId = m_stopTaskId;
        if (m_manager && !taskId.isEmpty()) m_manager->cancel(taskId);
    });
    refresh();
}

void SimulationStatusWidget::setManager(SimulationTaskManager* manager)
{
    if (m_manager) disconnect(m_manager, nullptr, this, nullptr);
    m_manager = manager;
    if (m_manager) {
        connect(m_manager, &SimulationTaskManager::taskAdded, this, [this]() { refresh(); });
        connect(m_manager, &SimulationTaskManager::taskUpdated, this, [this]() { refresh(); });
        connect(m_manager, &QObject::destroyed, this, [this]() {
            m_manager.clear();
            refresh();
        });
    }
    refresh();
}

void SimulationStatusWidget::retranslateUi() { refresh(); }

void SimulationStatusWidget::refresh()
{
    const auto tasks = m_manager ? m_manager->tasks() : QVector<SimulationTaskRecord>{};
    m_detailsTaskId.clear();
    m_stopTaskId.clear();
    setVisible(!tasks.isEmpty());
    m_stop->setEnabled(false);
    m_stop->setVisible(false);
    if (tasks.isEmpty()) return;

    const SimulationTaskRecord* displayed = &tasks.last();
    const SimulationTaskRecord* latestFailure = nullptr;
    int activeCount = 0;
    int failures = 0;
    for (const auto& task : tasks) {
        if (active(task.state)) {
            if (activeCount++ == 0) displayed = &task;
            if (m_stopTaskId.isEmpty() && task.state != SimulationTaskState::Stopping)
                m_stopTaskId = task.taskId;
        }
        if (task.state == SimulationTaskState::Failed) {
            ++failures;
            latestFailure = &task;
        }
    }
    QString text = QStringLiteral("FDS: %1").arg(
        UiLanguageManager::text(simulationTaskStateName(displayed->state)));
    if (displayed->state == SimulationTaskState::Running && displayed->endTime > 0)
        text += QStringLiteral(" %1%").arg(qBound(0, qRound(displayed->progress * 100), 100));
    if (activeCount > 1) text += u(" (%1 active)").arg(activeCount);
    // Failed tasks stay discoverable even after a different job completes.
    if (failures) text += u(" | Failed: %1").arg(failures);
    m_details->setText(text);
    m_details->setIcon(style()->standardIcon(failures ? QStyle::SP_MessageBoxWarning
                                                    : QStyle::SP_FileDialogDetailedView));
    m_detailsTaskId = latestFailure ? latestFailure->taskId : displayed->taskId;
    m_details->setAccessibleName(u("Show simulation details"));
    const auto* detail = latestFailure ? latestFailure : displayed;
    m_details->setToolTip(u("Show simulation details") + QStringLiteral("\n%1 / %2\n%3")
        .arg(detail->projectName, detail->chid, detail->errorMessage));
    m_stop->setVisible(activeCount > 0);
    m_stop->setEnabled(!m_stopTaskId.isEmpty());
    m_stop->setAccessibleName(u("Stop active task"));
    const auto* stopTask = m_manager ? m_manager->task(m_stopTaskId) : nullptr;
    m_stop->setToolTip(u("Stop active task") + (stopTask
        ? QStringLiteral("\n%1 / %2").arg(stopTask->projectName, stopTask->chid) : QString{}));
}
