#pragma once

#include <QPointer>
#include <QWidget>

class SimulationTaskManager;
class QToolButton;

// A compact, always reachable monitor; hiding details never owns task lifetime.
class SimulationStatusWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SimulationStatusWidget(QWidget* parent = nullptr);
    void setManager(SimulationTaskManager* manager);
    void retranslateUi();

signals:
    void detailsRequested(const QString& taskId);

private:
    void refresh();
    QPointer<SimulationTaskManager> m_manager;
    QToolButton* m_details = nullptr;
    QToolButton* m_stop = nullptr;
    QString m_detailsTaskId;
    QString m_stopTaskId;
};
