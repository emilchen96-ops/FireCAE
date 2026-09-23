#pragma once

#include <QWidget>
#include <QPointer>

class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QLabel;
class QToolButton;
class SimulationTaskManager;

class SimulationTaskCenterWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SimulationTaskCenterWidget(QWidget* parent = nullptr);

    void setManager(SimulationTaskManager* manager);
    QString selectedTaskId() const;
    bool selectTaskById(const QString& taskId);
    bool detailsExpanded() const;
    void setDetailsExpanded(bool expanded);
    void retranslateUi();

signals:
    void openResultsRequested(const QString& smvFilePath);

private:
    void refresh();
    void refreshDetails();

    QPointer<SimulationTaskManager> m_manager;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPlainTextEdit* m_standardOutput = nullptr;
    QPlainTextEdit* m_standardError = nullptr;
    QPlainTextEdit* m_commandPreview = nullptr;
    QLabel* m_environmentLabel = nullptr;
    QLabel* m_commandLabel = nullptr;
    QLabel* m_environmentCheck = nullptr;
    QTabWidget* m_outputTabs = nullptr;
    QWidget* m_details = nullptr;
    QToolButton* m_detailsToggle = nullptr;
    QLabel* m_failureSummary = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_retry = nullptr;
    QPushButton* m_openFolder = nullptr;
    QPushButton* m_openResults = nullptr;
};
