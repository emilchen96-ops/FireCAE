#pragma once

#include "simulation/FdsRunner.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

enum class SimulationTaskState
{
    Waiting,
    Preparing,
    Running,
    Stopping,
    Completed,
    Failed,
    Cancelled
};

struct SimulationTaskRecord
{
    QString taskId;
    QString projectName;
    QString sceneName;
    QString chid;
    FdsRunRequest request;
    SimulationTaskState state = SimulationTaskState::Waiting;
    QString backendName;
    QString outputDirectory;
    QString commandLine;
    QString environmentCheck;
    QString log;
    QString standardOutput;
    QString standardError;
    QString errorMessage;
    QDateTime queuedAt;
    QDateTime startedAt;
    QDateTime finishedAt;
    qint64 elapsedMilliseconds = 0;
    qint64 estimatedRemainingMilliseconds = -1;
    double simulationTime = 0.0;
    double endTime = 0.0;
    double progress = 0.0;
    FdsRunSummary summary;
};

QString simulationTaskStateName(SimulationTaskState state);

class SimulationTaskManager final : public QObject
{
    Q_OBJECT

public:
    explicit SimulationTaskManager(QObject* parent = nullptr);
    ~SimulationTaskManager() override;

    QString enqueue(const FdsRunRequest& request,
                    const QString& projectName,
                    const QString& sceneName,
                    double endTime,
                    QString* errorMessage = nullptr);
    bool cancel(const QString& taskId);
    QString retry(const QString& taskId, QString* errorMessage = nullptr);
    void setMaximumConcurrentTasks(int count);
    int maximumConcurrentTasks() const;
    bool hasActiveTasks() const;
    QString firstActiveTaskId() const;
    QVector<SimulationTaskRecord> tasks() const;
    const SimulationTaskRecord* task(const QString& taskId) const;

signals:
    void taskAdded(const QString& taskId);
    void taskUpdated(const QString& taskId);
    void taskOutput(const QString& taskId, const QString& output);
    void taskFinished(const QString& taskId, const FdsRunSummary& summary);

private:
    void startQueuedTasks();
    void startTask(const QString& taskId);
    void consumeOutput(const QString& taskId, const QString& output,
                       bool standardError);
    int activeCount() const;

    QVector<SimulationTaskRecord> m_tasks;
    QHash<QString, FdsRunner*> m_runners;
    int m_maximumConcurrentTasks = 1;
};
