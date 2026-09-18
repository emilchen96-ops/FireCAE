#pragma once

#include "core/FcDocument.h"
#include "fds/FcFdsModel.h"

#include <QDialog>
#include <QHash>

#include <array>
#include <vector>

class FcProject;
enum class FcDisplayUnit;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class FdsSchemaEditorWidget;

struct FdsObjectEditorData
{
    QString name;
    QString keyword;
    QString fdsId;
    FcDocumentGroup group = FcDocumentGroup::Configuration;
    std::vector<FcFdsParameter> parameters;
};

class FdsObjectEditorDialog final : public QDialog
{
public:
    explicit FdsObjectEditorDialog(const FcProject* project,
                                   const QString& initialKeyword = {},
                                   const FcFdsNamelist* object = nullptr,
                                   QWidget* parent = nullptr);

    FdsObjectEditorData editorData() const;
    void setEditorData(const FdsObjectEditorData& editorDataValue);

private:
    void addParameterRow(const FcFdsParameter& parameter = {});
    void chooseReferencesForCurrentRow();
    void addRecommendedParameter();
    void moveCurrentParameterRow(int offset);
    void updateEditorAssist(const QString& keyword);
    void switchParameterDraft(const QString& keyword);
    void validateAndAccept();
    void updateReferenceButton();
    void syncMeshControlsFromParameters();
    void syncMeshParametersFromControls();
    void updateMeshSummary();
    int parameterRow(const QString& key) const;
    void setRawParameterValue(const QString& key, const QString& value);

    const FcProject* m_project = nullptr;
    const FcFdsNamelist* m_object = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_keywordCombo = nullptr;
    QLineEdit* m_fdsIdEdit = nullptr;
    QComboBox* m_groupCombo = nullptr;
    QLabel* m_specializedHint = nullptr;
    QComboBox* m_parameterPresetCombo = nullptr;
    QTableWidget* m_parameterTable = nullptr;
    QPushButton* m_chooseReferenceButton = nullptr;
    QTabWidget* m_editorTabs = nullptr;
    QWidget* m_meshEditorPage = nullptr;
    QWidget* m_meshPreview = nullptr;
    FdsSchemaEditorWidget* m_basicSchemaEditor = nullptr;
    FdsSchemaEditorWidget* m_schemaEditor = nullptr;
    QComboBox* m_meshResolutionMode = nullptr;
    QStackedWidget* m_meshResolutionStack = nullptr;
    std::array<QDoubleSpinBox*, 3> m_meshOriginSpins{};
    std::array<QDoubleSpinBox*, 3> m_meshSizeSpins{};
    std::array<QSpinBox*, 3> m_meshCellSpins{};
    std::array<QDoubleSpinBox*, 3> m_meshTargetCellSizeSpins{};
    std::array<QLabel*, 3> m_meshEndLabels{};
    QLabel* m_meshSummaryLabel = nullptr;
    bool m_updatingMeshEditor = false;
    QString m_defaultObjectName;
    QString m_parameterKeyword;
    QHash<QString, std::vector<FcFdsParameter>> m_parameterDrafts;
};

class FdsProjectDialog final : public QDialog
{
public:
    explicit FdsProjectDialog(const FcProject* project, QWidget* parent = nullptr);

    QString projectName() const;
    QString chid() const;
    double endTime() const;
    QString fdsVersion() const;
    FcDisplayUnit displayUnit() const;

private:
    void validateAndAccept();

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_chidEdit = nullptr;
    QDoubleSpinBox* m_endTimeSpin = nullptr;
    QComboBox* m_fdsVersionCombo = nullptr;
    QComboBox* m_displayUnitCombo = nullptr;
};
