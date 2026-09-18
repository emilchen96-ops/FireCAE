#pragma once

#include <QWidget>
#include <QStringList>

class QLabel;
class QListWidget;
class QPushButton;

class StartPageWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit StartPageWidget(QWidget* parent = nullptr);

    void setRecentProjects(const QStringList& paths);
    void setRecoveryEntries(const QStringList& displayNames,
                            const QStringList& snapshotPaths);
    void setRuntimeStatus(const QString& fdsStatus,
                          const QString& smokeviewStatus);
    void retranslateUi();

signals:
    void newProjectRequested();
    void openProjectRequested();
    void recentProjectRequested(const QString& filePath);
    void tutorialRequested(const QString& tutorialId);
    void recoveryRequested(const QString& snapshotPath);
    void discardRecoveryRequested(const QString& snapshotPath);

private:
    QLabel* m_title = nullptr;
    QLabel* m_version = nullptr;
    QLabel* m_runtimeStatus = nullptr;
    QPushButton* m_newProject = nullptr;
    QPushButton* m_openProject = nullptr;
    QListWidget* m_recentProjects = nullptr;
    QListWidget* m_tutorials = nullptr;
    QListWidget* m_recoveries = nullptr;
    QPushButton* m_recover = nullptr;
    QPushButton* m_discardRecovery = nullptr;
    QString m_fdsStatus;
    QString m_smokeviewStatus;
};
