#pragma once

#include "core/FcScenario.h"

#include <QDialog>

class FcProject;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class ScenarioManagerDialog final : public QDialog
{
public:
    explicit ScenarioManagerDialog(FcProject* project, QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    void loadScenario(int index);
    void storeLoadedScenario();
    void rebuildScenarioList();
    void rebuildObjectStates();
    void rebuildOverrides();
    void addScenario();
    void duplicateScenario();
    void renameScenario();
    void removeScenario();
    void setDefaultScenario();
    void addOverride();
    void removeOverride();
    void compareScenario();
    void createParameterStudy();
    FcScenario* loadedScenario();

    FcProject* m_project = nullptr;
    QVector<FcScenario> m_scenarios;
    QString m_activeId;
    QString m_defaultId;
    int m_loadedIndex = -1;
    bool m_loading = false;
    QListWidget* m_list = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_chid = nullptr;
    QLineEdit* m_outputDirectory = nullptr;
    QComboBox* m_backend = nullptr;
    QSpinBox* m_processes = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_objects = nullptr;
    QTableWidget* m_overrides = nullptr;
};
