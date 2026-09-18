#include "ui/StartPageWidget.h"

#include "ui/UiLanguage.h"
#include "ui/TutorialGuideWidget.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
QString t(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}
}

StartPageWidget::StartPageWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StartPageWidget"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(42, 30, 42, 30);
    root->setSpacing(18);
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 12);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_version = new QLabel(this);
    root->addWidget(m_title);
    root->addWidget(m_version);

    auto* actionRow = new QHBoxLayout;
    m_newProject = new QPushButton(this);
    m_newProject->setObjectName(QStringLiteral("StartPageNewButton"));
    m_openProject = new QPushButton(this);
    m_openProject->setObjectName(QStringLiteral("StartPageOpenButton"));
    actionRow->addWidget(m_newProject);
    actionRow->addWidget(m_openProject);
    actionRow->addStretch(1);
    root->addLayout(actionRow);

    auto* columns = new QGridLayout;
    auto* recentBox = new QGroupBox(this);
    recentBox->setObjectName(QStringLiteral("StartPageRecentGroup"));
    auto* recentLayout = new QVBoxLayout(recentBox);
    m_recentProjects = new QListWidget(recentBox);
    m_recentProjects->setObjectName(QStringLiteral("StartPageRecentList"));
    recentLayout->addWidget(m_recentProjects);
    auto* tutorialBox = new QGroupBox(this);
    tutorialBox->setObjectName(QStringLiteral("StartPageTutorialGroup"));
    auto* tutorialLayout = new QVBoxLayout(tutorialBox);
    m_tutorials = new QListWidget(tutorialBox);
    m_tutorials->setObjectName(QStringLiteral("StartPageTutorialList"));
    for (const TutorialDefinition& tutorial : TutorialGuideWidget::catalog()) {
        auto* item = new QListWidgetItem(tutorial.title, m_tutorials);
        item->setData(Qt::UserRole, tutorial.id);
        item->setToolTip(tutorial.purpose);
    }
    tutorialLayout->addWidget(m_tutorials);
    columns->addWidget(recentBox, 0, 0);
    columns->addWidget(tutorialBox, 0, 1);
    columns->setColumnStretch(0, 1);
    columns->setColumnStretch(1, 1);
    root->addLayout(columns, 1);

    auto* recoveryBox = new QGroupBox(this);
    recoveryBox->setObjectName(QStringLiteral("StartPageRecoveryGroup"));
    auto* recoveryLayout = new QVBoxLayout(recoveryBox);
    m_recoveries = new QListWidget(recoveryBox);
    m_recoveries->setObjectName(QStringLiteral("StartPageRecoveryList"));
    recoveryLayout->addWidget(m_recoveries);
    auto* recoveryButtons = new QHBoxLayout;
    m_recover = new QPushButton(recoveryBox);
    m_recover->setObjectName(QStringLiteral("StartPageRecoverButton"));
    m_discardRecovery = new QPushButton(recoveryBox);
    m_discardRecovery->setObjectName(QStringLiteral("StartPageDiscardRecoveryButton"));
    recoveryButtons->addWidget(m_recover);
    recoveryButtons->addWidget(m_discardRecovery);
    recoveryButtons->addStretch(1);
    recoveryLayout->addLayout(recoveryButtons);
    root->addWidget(recoveryBox);
    m_runtimeStatus = new QLabel(this);
    m_runtimeStatus->setObjectName(QStringLiteral("StartPageRuntimeStatus"));
    m_runtimeStatus->setWordWrap(true);
    root->addWidget(m_runtimeStatus);

    connect(m_newProject, &QPushButton::clicked, this,
            &StartPageWidget::newProjectRequested);
    connect(m_openProject, &QPushButton::clicked, this,
            &StartPageWidget::openProjectRequested);
    connect(m_recentProjects, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
        if (item) emit recentProjectRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_tutorials, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
        if (item) emit tutorialRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_recover, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = m_recoveries->currentItem())
            emit recoveryRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_discardRecovery, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = m_recoveries->currentItem())
            emit discardRecoveryRequested(item->data(Qt::UserRole).toString());
    });
    retranslateUi();
}

void StartPageWidget::setRecentProjects(const QStringList& paths)
{
    m_recentProjects->clear();
    for (const QString& path : paths) {
        const QFileInfo info(path);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2%3")
                .arg(info.completeBaseName(), path,
                     info.exists() ? QString{} : QStringLiteral("  [missing]")),
            m_recentProjects);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
}

void StartPageWidget::setRecoveryEntries(const QStringList& displayNames,
                                         const QStringList& snapshotPaths)
{
    m_recoveries->clear();
    const int count = qMin(displayNames.size(), snapshotPaths.size());
    for (int index = 0; index < count; ++index) {
        auto* item = new QListWidgetItem(displayNames[index], m_recoveries);
        item->setData(Qt::UserRole, snapshotPaths[index]);
        item->setToolTip(snapshotPaths[index]);
    }
    m_recover->setEnabled(count > 0);
    m_discardRecovery->setEnabled(count > 0);
}

void StartPageWidget::setRuntimeStatus(const QString& fdsStatus,
                                       const QString& smokeviewStatus)
{
    m_fdsStatus = fdsStatus;
    m_smokeviewStatus = smokeviewStatus;
    retranslateUi();
}

void StartPageWidget::retranslateUi()
{
    m_title->setText(t("FireCAE Start Page"));
    m_version->setText(t("Version %1 — FDS preprocessing, solving and postprocessing")
                           .arg(QCoreApplication::applicationVersion()));
    m_newProject->setText(t("New Project"));
    m_openProject->setText(t("Open Project..."));
    if (auto* box = qobject_cast<QGroupBox*>(m_recentProjects->parentWidget()))
        box->setTitle(t("Recent Projects"));
    if (auto* box = qobject_cast<QGroupBox*>(m_tutorials->parentWidget()))
        box->setTitle(t("Tutorials and Examples"));
    if (auto* box = qobject_cast<QGroupBox*>(m_recoveries->parentWidget()))
        box->setTitle(t("Recovery Copies"));
    m_recover->setText(t("Recover Selected"));
    m_discardRecovery->setText(t("Discard Selected"));
    m_runtimeStatus->setText(
        t("Solver status: FDS — %1    Smokeview — %2")
            .arg(m_fdsStatus.isEmpty() ? t("not configured") : m_fdsStatus,
                 m_smokeviewStatus.isEmpty() ? t("not configured") : m_smokeviewStatus));
}
