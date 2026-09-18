#pragma once

#include <QDialog>

class FcProject;
class QLabel;
class QTableWidget;

class ProjectResourcesDialog final : public QDialog
{
public:
    ProjectResourcesDialog(FcProject* project,
                           const QString& projectFilePath,
                           QWidget* parent = nullptr);

    bool projectChanged() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh();
    void relinkSelected();
    void makeAllRelative();

    FcProject* m_project = nullptr;
    QString m_projectFilePath;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    bool m_projectChanged = false;
};
