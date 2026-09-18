#pragma once

#include "import/GeometryImportService.h"
#include "import/IfcImportService.h"

#include <QWizard>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
template <typename T> class QFutureWatcher;

class GeometryImportWizard final : public QWizard
{
public:
    explicit GeometryImportWizard(const QString& initialFilePath = {},
                                  QWidget* parent = nullptr);
    ~GeometryImportWizard() override;

    QString filePath() const;
    GeometryImportOptions options() const;
    const GeometryImportResult& importResult() const;
    const IfcImportResult& ifcImportResult() const;
    void reject() override;

protected:
    void initializePage(int id) override;
    bool validateCurrentPage() override;

private:
    void browse();
    void refreshFileStatus();
    void prepareIfcOptions();
    void buildPreview();
    IfcImportOptions ifcOptions() const;
    void updateImportProgress(int percent, const QString& stage);
    void finishGeometryPreview();
    void finishIfcPreview();
    void cancelImport();

    QLineEdit* m_filePath = nullptr;
    QLabel* m_formatStatus = nullptr;
    QComboBox* m_unit = nullptr;
    QComboBox* m_axis = nullptr;
    QDoubleSpinBox* m_scale = nullptr;
    QDoubleSpinBox* m_originX = nullptr;
    QDoubleSpinBox* m_originY = nullptr;
    QDoubleSpinBox* m_originZ = nullptr;
    QDoubleSpinBox* m_linearDeflection = nullptr;
    QDoubleSpinBox* m_angularDeflection = nullptr;
    QCheckBox* m_merge = nullptr;
    QCheckBox* m_materials = nullptr;
    QCheckBox* m_textures = nullptr;
    QGroupBox* m_ifcOptionsGroup = nullptr;
    QLabel* m_ifcPreflightSummary = nullptr;
    QTreeWidget* m_ifcTypeTree = nullptr;
    QComboBox* m_ifcMergeStrategy = nullptr;
    QComboBox* m_ifcSimplification = nullptr;
    QComboBox* m_ifcConversionRoute = nullptr;
    QCheckBox* m_ifcPropertySets = nullptr;
    QCheckBox* m_ifcVisible = nullptr;
    QPlainTextEdit* m_preview = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressStage = nullptr;
    QPushButton* m_cancelImport = nullptr;
    GeometryImportResult m_result;
    IfcImportResult m_ifcResult;
    IfcPreflightReport m_ifcPreflight;
    QFutureWatcher<GeometryImportResult>* m_geometryWatcher = nullptr;
    QFutureWatcher<IfcImportResult>* m_ifcWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    bool m_importRunning = false;
    bool m_rejectWhenFinished = false;
};
