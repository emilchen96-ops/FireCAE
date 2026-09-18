#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class FcProject;
class QLabel;
class QListWidget;
class QPushButton;

enum class TutorialCheckKind
{
    BlankProject,
    ProjectConfigured,
    KeywordMinimums,
    GeometryImported,
    ScenarioMinimum,
    ModelValid,
    ProjectSaved,
    FdsExported,
    ResultsLoaded,
    Manual
};

struct TutorialStepDefinition
{
    QString title;
    QString menuPath;
    QString actionObjectName;
    QString instructions;
    QString parameters;
    QString physicalMeaning;
    QString rationale;
    QString expected;
    QString commonError;
    TutorialCheckKind checkKind = TutorialCheckKind::Manual;
    QMap<QString, int> keywordMinimums;
    int minimumCount = 0;
};

struct TutorialDefinition
{
    QString id;
    QString title;
    QString purpose;
    QString completedEffect;
    QString prerequisites;
    QString finalProjectHint;
    QVector<TutorialStepDefinition> steps;
};

class TutorialGuideWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialGuideWidget(QWidget* parent = nullptr);

    static QVector<TutorialDefinition> catalog();
    static const TutorialDefinition* findTutorial(const QString& id);

    void startTutorial(const QString& id, bool restart = false);
    void setContext(const FcProject* project,
                    const QString& projectFilePath,
                    const QString& fdsFilePath,
                    bool resultsLoaded);
    QString activeTutorialId() const;
    int currentStepIndex() const;
    int stepCount() const;
    bool currentStepComplete(QString* detail = nullptr) const;

signals:
    void openActionRequested(const QString& actionObjectName);
    void stepTargetChanged(const QString& actionObjectName,
                           const QString& menuPath);
    void closeRequested();

private:
    void rebuildTutorialList();
    void showCurrentStep();
    void saveProgress() const;
    void changeStep(int delta);

    const TutorialDefinition* m_tutorial = nullptr;
    const FcProject* m_project = nullptr;
    QString m_projectFilePath;
    QString m_fdsFilePath;
    bool m_resultsLoaded = false;
    int m_stepIndex = 0;
    QLabel* m_title = nullptr;
    QLabel* m_overview = nullptr;
    QListWidget* m_steps = nullptr;
    QLabel* m_stepTitle = nullptr;
    QLabel* m_stepDetails = nullptr;
    QLabel* m_checkStatus = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_openTool = nullptr;
    QPushButton* m_check = nullptr;
    QPushButton* m_next = nullptr;
    QPushButton* m_restart = nullptr;
    QPushButton* m_exit = nullptr;
};
