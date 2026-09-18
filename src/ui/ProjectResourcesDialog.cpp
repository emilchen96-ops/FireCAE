#include "ui/ProjectResourcesDialog.h"

#include "core/FcProject.h"
#include "resources/ProjectResourceManager.h"
#include "ui/UiLanguage.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
QString u(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}
}

ProjectResourcesDialog::ProjectResourcesDialog(FcProject* project,
                                               const QString& projectFilePath,
                                               QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_projectFilePath(projectFilePath)
{
    setObjectName(QStringLiteral("ProjectResourcesDialog"));
    setWindowTitle(u("Project Resources"));
    resize(1120, 600);
    auto* layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("ProjectResourceSummaryLabel"));
    layout->addWidget(m_summary);
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("ProjectResourceTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({u("Object UUID"), u("Property"),
        u("Stored path"), u("Resolved path"), u("Status"), u("Package")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* relink = buttons->addButton(u("Locate / Relink..."),
                                      QDialogButtonBox::ActionRole);
    relink->setObjectName(QStringLiteral("ProjectResourceRelinkButton"));
    auto* relative = buttons->addButton(u("Make Paths Relative"),
                                        QDialogButtonBox::ActionRole);
    relative->setObjectName(QStringLiteral("ProjectResourceRelativeButton"));
    relative->setEnabled(!m_projectFilePath.isEmpty());
    connect(relink, &QPushButton::clicked, this, &ProjectResourcesDialog::relinkSelected);
    connect(relative, &QPushButton::clicked, this,
            &ProjectResourcesDialog::makeAllRelative);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refresh();
}

bool ProjectResourcesDialog::projectChanged() const
{
    return m_projectChanged;
}

void ProjectResourcesDialog::refresh()
{
    m_table->setRowCount(0);
    if (!m_project) return;
    const QList<ProjectResourceReference> resources =
        ProjectResourceManager::scan(*m_project, m_projectFilePath);
    int missing = 0;
    for (const ProjectResourceReference& resource : resources) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* uuid = new QTableWidgetItem(resource.objectId);
        uuid->setData(Qt::UserRole, resource.objectId);
        uuid->setData(Qt::UserRole + 1, resource.propertyKey);
        m_table->setItem(row, 0, uuid);
        m_table->setItem(row, 1, new QTableWidgetItem(resource.propertyKey));
        m_table->setItem(row, 2, new QTableWidgetItem(resource.storedPath));
        m_table->setItem(row, 3, new QTableWidgetItem(resource.resolvedPath));
        auto* status = new QTableWidgetItem(resource.exists
                                                 ? u("Available")
                                                 : u("Missing"));
        if (!resource.exists) {
            status->setForeground(Qt::red);
            ++missing;
        }
        m_table->setItem(row, 4, status);
        m_table->setItem(row, 5, new QTableWidgetItem(
            resource.packagedByDefault ? u("Included")
                                       : u("External (results)")));
    }
    m_summary->setText(u("%1 resource reference(s), %2 missing. Large result files remain external by default.")
                           .arg(resources.size())
                           .arg(missing));
}

void ProjectResourcesDialog::relinkSelected()
{
    const int row = m_table->currentRow();
    if (!m_project || row < 0 || !m_table->item(row, 0)) {
        QMessageBox::information(this, u("Project Resources"),
                                 u("Select a resource row first."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, u("Locate Missing Resource"),
        m_table->item(row, 3)->text(), u("All Files (*.*)"));
    if (path.isEmpty()) return;
    if (!ProjectResourceManager::relink(
            *m_project,
            m_table->item(row, 0)->data(Qt::UserRole).toString(),
            m_table->item(row, 0)->data(Qt::UserRole + 1).toString(),
            QFileInfo(path).absoluteFilePath())) {
        QMessageBox::warning(this, u("Project Resources"),
                             u("The selected resource could not be relinked."));
        return;
    }
    m_projectChanged = true;
    refresh();
}

void ProjectResourcesDialog::makeAllRelative()
{
    if (!m_project || m_projectFilePath.isEmpty()) return;
    const int changed = ProjectResourceManager::makePathsRelative(
        *m_project, m_projectFilePath);
    m_projectChanged = m_projectChanged || changed > 0;
    refresh();
}

void ProjectResourcesDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() != QEvent::LanguageChange || !m_table) return;
    const int selectedRow = m_table->currentRow();
    QString selectedId;
    QString selectedProperty;
    if (auto* selected = m_table->item(selectedRow, 0)) {
        selectedId = selected->data(Qt::UserRole).toString();
        selectedProperty = selected->data(Qt::UserRole + 1).toString();
    }
    setWindowTitle(u("Project Resources"));
    m_table->setHorizontalHeaderLabels({u("Object UUID"), u("Property"),
        u("Stored path"), u("Resolved path"), u("Status"), u("Package")});
    if (auto* button = findChild<QPushButton*>(QStringLiteral("ProjectResourceRelinkButton")))
        button->setText(u("Locate / Relink..."));
    if (auto* button = findChild<QPushButton*>(QStringLiteral("ProjectResourceRelativeButton")))
        button->setText(u("Make Paths Relative"));
    refresh();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto* item = m_table->item(row, 0);
        if (item->data(Qt::UserRole).toString() == selectedId &&
            item->data(Qt::UserRole + 1).toString() == selectedProperty) {
            m_table->setCurrentCell(row, 0);
            break;
        }
    }
}
