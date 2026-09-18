#include "ui/GeometryImportWizard.h"
#include "ui/UiLanguage.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWizardPage>
#include <QtConcurrent/QtConcurrentRun>

namespace
{
QString uiText(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

enum PageId { FilePage = 0, CoordinatesPage, QualityPage, PreviewPage };

QDoubleSpinBox* coordinateSpin(QWidget* parent)
{
    auto* value = new QDoubleSpinBox(parent);
    value->setRange(-1.0e9, 1.0e9);
    value->setDecimals(6);
    value->setSuffix(QStringLiteral(" m"));
    return value;
}
}

GeometryImportWizard::GeometryImportWizard(const QString& initialFilePath,
                                           QWidget* parent)
    : QWizard(parent)
{
    setObjectName(QStringLiteral("GeometryImportWizard"));
    setWindowTitle(uiText("Import IFC/CAD/BIM Geometry"));
    setOption(QWizard::NoBackButtonOnStartPage);
    resize(760, 560);

    auto* filePage = new QWizardPage(this);
    filePage->setTitle(uiText("Select geometry file"));
    filePage->setSubTitle(uiText("FireCAE detects the format from the extension and reports unavailable plug-ins explicitly."));
    auto* fileLayout = new QVBoxLayout(filePage);
    auto* fileRow = new QHBoxLayout();
    m_filePath = new QLineEdit(initialFilePath, filePage);
    m_filePath->setObjectName(QStringLiteral("GeometryImportFileEdit"));
    auto* browseButton = new QPushButton(uiText("Browse..."), filePage);
    browseButton->setObjectName(QStringLiteral("GeometryImportBrowseButton"));
    fileRow->addWidget(m_filePath, 1);
    fileRow->addWidget(browseButton);
    fileLayout->addLayout(fileRow);
    m_formatStatus = new QLabel(filePage);
    m_formatStatus->setObjectName(QStringLiteral("GeometryImportFormatStatus"));
    m_formatStatus->setWordWrap(true);
    fileLayout->addWidget(m_formatStatus);
    fileLayout->addStretch();
    addPage(filePage);
    connect(browseButton, &QPushButton::clicked, this, [this]() { browse(); });
    connect(m_filePath, &QLineEdit::textChanged, this, [this]() { refreshFileStatus(); });

    auto* coordinatePage = new QWizardPage(this);
    coordinatePage->setTitle(uiText("Units and coordinates"));
    coordinatePage->setSubTitle(uiText("All imported geometry is stored in metres with Z as the FireCAE vertical axis."));
    auto* coordinateForm = new QFormLayout(coordinatePage);
    m_unit = new QComboBox(coordinatePage);
    m_unit->setObjectName(QStringLiteral("GeometryImportUnitCombo"));
    m_unit->addItem(uiText("Auto"), QStringLiteral("Auto"));
    for (const QString& unit : {QStringLiteral("m"), QStringLiteral("mm"),
                                QStringLiteral("cm"), QStringLiteral("ft"),
                                QStringLiteral("in")}) {
        m_unit->addItem(unit, unit);
    }
    m_axis = new QComboBox(coordinatePage);
    m_axis->setObjectName(QStringLiteral("GeometryImportAxisCombo"));
    m_axis->addItems({uiText("Z up (FireCAE/FDS)"), uiText("Y up (rotate to Z)")});
    m_scale = new QDoubleSpinBox(coordinatePage);
    m_scale->setObjectName(QStringLiteral("GeometryImportScaleSpin"));
    m_scale->setRange(1.0e-9, 1.0e9);
    m_scale->setDecimals(9);
    m_scale->setValue(1.0);
    m_originX = coordinateSpin(coordinatePage);
    m_originY = coordinateSpin(coordinatePage);
    m_originZ = coordinateSpin(coordinatePage);
    m_originX->setObjectName(QStringLiteral("GeometryImportOriginXSpin"));
    m_originY->setObjectName(QStringLiteral("GeometryImportOriginYSpin"));
    m_originZ->setObjectName(QStringLiteral("GeometryImportOriginZSpin"));
    coordinateForm->addRow(uiText("Source unit:"), m_unit);
    coordinateForm->addRow(uiText("Source up axis:"), m_axis);
    coordinateForm->addRow(uiText("Additional scale:"), m_scale);
    coordinateForm->addRow(uiText("Origin X:"), m_originX);
    coordinateForm->addRow(uiText("Origin Y:"), m_originY);
    coordinateForm->addRow(uiText("Origin Z:"), m_originZ);
    addPage(coordinatePage);

    auto* qualityPage = new QWizardPage(this);
    qualityPage->setTitle(uiText("Geometry quality and structure"));
    auto* qualityForm = new QFormLayout(qualityPage);
    m_linearDeflection = new QDoubleSpinBox(qualityPage);
    m_linearDeflection->setObjectName(QStringLiteral("GeometryImportLinearDeflectionSpin"));
    m_linearDeflection->setRange(0.000001, 1000.0);
    m_linearDeflection->setDecimals(6);
    m_linearDeflection->setValue(0.01);
    m_linearDeflection->setSuffix(QStringLiteral(" m"));
    m_angularDeflection = new QDoubleSpinBox(qualityPage);
    m_angularDeflection->setObjectName(QStringLiteral("GeometryImportAngularDeflectionSpin"));
    m_angularDeflection->setRange(1.0, 90.0);
    m_angularDeflection->setValue(20.0);
    m_angularDeflection->setSuffix(QStringLiteral("°"));
    m_merge = new QCheckBox(uiText("Merge roots into one import object"), qualityPage);
    m_merge->setObjectName(QStringLiteral("GeometryImportMergeCheck"));
    m_merge->setChecked(true);
    m_materials = new QCheckBox(uiText("Preserve source material metadata when available"), qualityPage);
    m_materials->setObjectName(QStringLiteral("GeometryImportMaterialsCheck"));
    m_materials->setChecked(true);
    m_textures = new QCheckBox(uiText("Inspect texture metadata (embedded texture rendering is limited)"), qualityPage);
    m_textures->setObjectName(QStringLiteral("GeometryImportTexturesCheck"));
    m_textures->setChecked(true);
    qualityForm->addRow(uiText("Triangulation linear tolerance:"), m_linearDeflection);
    qualityForm->addRow(uiText("Triangulation angular tolerance:"), m_angularDeflection);
    qualityForm->addRow(m_merge);
    qualityForm->addRow(m_materials);
    qualityForm->addRow(m_textures);
    auto* note = new QLabel(uiText("Mesh simplification is currently set to None. FireCAE will not silently decimate engineering geometry."),
        qualityPage);
    note->setWordWrap(true);
    qualityForm->addRow(uiText("Simplification:"), note);

    m_ifcOptionsGroup = new QGroupBox(uiText("IFC structure and FDS strategy"),
                                      qualityPage);
    m_ifcOptionsGroup->setObjectName(QStringLiteral("IfcImportOptionsGroup"));
    auto* ifcForm = new QFormLayout(m_ifcOptionsGroup);
    m_ifcPreflightSummary = new QLabel(m_ifcOptionsGroup);
    m_ifcPreflightSummary->setObjectName(QStringLiteral("IfcImportPreflightSummary"));
    m_ifcPreflightSummary->setWordWrap(true);
    m_ifcTypeTree = new QTreeWidget(m_ifcOptionsGroup);
    m_ifcTypeTree->setObjectName(QStringLiteral("IfcImportTypeTree"));
    m_ifcTypeTree->setHeaderLabels(
        {uiText("Import"), uiText("IFC class"),
         uiText("Count")});
    m_ifcTypeTree->setRootIsDecorated(false);
    m_ifcTypeTree->setMinimumHeight(150);
    m_ifcMergeStrategy = new QComboBox(m_ifcOptionsGroup);
    m_ifcMergeStrategy->setObjectName(QStringLiteral("IfcImportMergeStrategyCombo"));
    m_ifcMergeStrategy->addItem(uiText("Preserve hierarchy / per component"),
                                QStringLiteral("PRESERVE_HIERARCHY"));
    m_ifcMergeStrategy->addItem(uiText("Merge display geometry by storey"),
                                QStringLiteral("MERGE_BY_STOREY"));
    m_ifcMergeStrategy->addItem(uiText("Merge all display geometry"),
                                QStringLiteral("MERGE_ALL"));
    m_ifcSimplification = new QComboBox(m_ifcOptionsGroup);
    m_ifcSimplification->setObjectName(QStringLiteral("IfcImportSimplificationCombo"));
    m_ifcSimplification->addItem(uiText("None — preserve converted geometry"),
                                 QStringLiteral("NONE"));
    m_ifcSimplification->addItem(uiText("Bounding boxes — coarse FDS preparation"),
                                 QStringLiteral("BOUNDING_BOX"));
    m_ifcConversionRoute = new QComboBox(m_ifcOptionsGroup);
    m_ifcConversionRoute->setObjectName(QStringLiteral("IfcImportConversionRouteCombo"));
    m_ifcConversionRoute->addItem(uiText("Reference geometry only"),
                                  QStringLiteral("REFERENCE"));
    m_ifcConversionRoute->addItem(uiText("Prepare as FDS OBST"),
                                  QStringLiteral("OBST"));
    m_ifcConversionRoute->addItem(uiText("Prepare as FDS HOLE"),
                                  QStringLiteral("HOLE"));
    m_ifcConversionRoute->addItem(uiText("Prepare as native FDS GEOM"),
                                  QStringLiteral("GEOM"));
    m_ifcPropertySets = new QCheckBox(
        uiText("Preserve Property Set summary and relationships"),
        m_ifcOptionsGroup);
    m_ifcPropertySets->setObjectName(QStringLiteral("IfcImportPropertySetsCheck"));
    m_ifcPropertySets->setChecked(true);
    m_ifcVisible = new QCheckBox(uiText("Show imported components initially"),
                                 m_ifcOptionsGroup);
    m_ifcVisible->setObjectName(QStringLiteral("IfcImportVisibleCheck"));
    m_ifcVisible->setChecked(true);
    ifcForm->addRow(m_ifcPreflightSummary);
    ifcForm->addRow(uiText("Component type filter:"), m_ifcTypeTree);
    ifcForm->addRow(uiText("Merge strategy:"), m_ifcMergeStrategy);
    ifcForm->addRow(uiText("Simplification:"), m_ifcSimplification);
    ifcForm->addRow(uiText("FDS conversion strategy:"),
                    m_ifcConversionRoute);
    ifcForm->addRow(m_ifcPropertySets);
    ifcForm->addRow(m_ifcVisible);
    qualityForm->addRow(m_ifcOptionsGroup);
    addPage(qualityPage);

    auto* previewPage = new QWizardPage(this);
    previewPage->setTitle(uiText("Import preview and quality report"));
    auto* previewLayout = new QVBoxLayout(previewPage);
    auto* progressRow = new QHBoxLayout;
    m_progressStage = new QLabel(uiText("Ready to build preview."), previewPage);
    m_progressStage->setObjectName(QStringLiteral("GeometryImportProgressStage"));
    m_progress = new QProgressBar(previewPage);
    m_progress->setObjectName(QStringLiteral("GeometryImportProgressBar"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_cancelImport = new QPushButton(uiText("Cancel Import"), previewPage);
    m_cancelImport->setObjectName(QStringLiteral("GeometryImportCancelButton"));
    m_cancelImport->setEnabled(false);
    progressRow->addWidget(m_progressStage, 1);
    progressRow->addWidget(m_progress);
    progressRow->addWidget(m_cancelImport);
    previewLayout->addLayout(progressRow);
    m_preview = new QPlainTextEdit(previewPage);
    m_preview->setObjectName(QStringLiteral("GeometryImportPreviewReport"));
    m_preview->setReadOnly(true);
    previewLayout->addWidget(m_preview);
    addPage(previewPage);

    m_geometryWatcher = new QFutureWatcher<GeometryImportResult>(this);
    m_ifcWatcher = new QFutureWatcher<IfcImportResult>(this);
    connect(m_geometryWatcher, &QFutureWatcher<GeometryImportResult>::finished,
            this, [this]() { finishGeometryPreview(); });
    connect(m_ifcWatcher, &QFutureWatcher<IfcImportResult>::finished,
            this, [this]() { finishIfcPreview(); });
    connect(m_cancelImport, &QPushButton::clicked,
            this, [this]() { cancelImport(); });

    refreshFileStatus();
}

GeometryImportWizard::~GeometryImportWizard()
{
    if (m_cancelRequested) m_cancelRequested->store(true);
    if (m_geometryWatcher && m_geometryWatcher->isRunning())
        m_geometryWatcher->waitForFinished();
    if (m_ifcWatcher && m_ifcWatcher->isRunning())
        m_ifcWatcher->waitForFinished();
}

QString GeometryImportWizard::filePath() const
{
    return QFileInfo(m_filePath->text().trimmed()).absoluteFilePath();
}

GeometryImportOptions GeometryImportWizard::options() const
{
    GeometryImportOptions value;
    value.sourceUnit = m_unit->currentData().toString();
    value.customScale = m_scale->value();
    value.upAxis = m_axis->currentIndex() == 0 ? GeometryUpAxis::ZUp : GeometryUpAxis::YUp;
    value.originX = m_originX->value();
    value.originY = m_originY->value();
    value.originZ = m_originZ->value();
    value.linearDeflection = m_linearDeflection->value();
    value.angularDeflectionDegrees = m_angularDeflection->value();
    value.mergeObjects = m_merge->isChecked();
    value.preserveMaterials = m_materials->isChecked();
    value.preserveTextures = m_textures->isChecked();
    return value;
}

const GeometryImportResult& GeometryImportWizard::importResult() const
{
    return m_result;
}

const IfcImportResult& GeometryImportWizard::ifcImportResult() const
{
    return m_ifcResult;
}

void GeometryImportWizard::initializePage(int id)
{
    QWizard::initializePage(id);
    if (id == QualityPage) prepareIfcOptions();
    if (id == PreviewPage) buildPreview();
}

bool GeometryImportWizard::validateCurrentPage()
{
    if (currentId() == FilePage) {
        const QFileInfo source(filePath());
        if (!source.exists() || !source.isFile()) {
            QMessageBox::warning(this, uiText("Import Geometry"),
                                 uiText("Choose an existing geometry file."));
            return false;
        }
        const GeometryImportFormat format = GeometryImportService::detectFormat(source.fileName());
        if (format != GeometryImportFormat::Ifc && !GeometryImportService::isSupported(format)) {
            QMessageBox::warning(this, uiText("Import Geometry"),
                                 GeometryImportService::unavailableReason(format));
            return false;
        }
    }
    if (currentId() == PreviewPage) {
        if (m_importRunning) {
            QMessageBox::information(this, uiText("Import Geometry"),
                                     uiText("Wait for the import preview or cancel it."));
            return false;
        }
        const bool isIfc = GeometryImportService::detectFormat(filePath()) ==
                           GeometryImportFormat::Ifc;
        const QString error = isIfc ? m_ifcResult.errorMessage : m_result.errorMessage;
        const bool success = isIfc ? m_ifcResult.success() : m_result.success();
        if (!success) {
            QMessageBox::critical(this, uiText("Import Geometry"),
                                  error.isEmpty()
                                      ? uiText("The import preview is incomplete.")
                                      : error);
            return false;
        }
    }
    return QWizard::validateCurrentPage();
}

void GeometryImportWizard::reject()
{
    if (m_importRunning) {
        m_rejectWhenFinished = true;
        cancelImport();
        return;
    }
    QWizard::reject();
}

void GeometryImportWizard::browse()
{
    const QString selected = QFileDialog::getOpenFileName(
        this, uiText("Select IFC/CAD/BIM Geometry"), m_filePath->text(),
        GeometryImportService::openFileFilter());
    if (!selected.isEmpty()) m_filePath->setText(selected);
}

void GeometryImportWizard::refreshFileStatus()
{
    const GeometryImportFormat format = GeometryImportService::detectFormat(m_filePath->text());
    const bool isIfc = format == GeometryImportFormat::Ifc;
    QString status = uiText("Detected format: %1").arg(
        GeometryImportService::formatName(format));
    if (format == GeometryImportFormat::Ifc)
        status += uiText(" — handled by the isolated IfcOpenShell pipeline.");
    else if (!GeometryImportService::isSupported(format))
        status += QStringLiteral(" — %1").arg(GeometryImportService::unavailableReason(format));
    else
        status += uiText(" — OpenCascade importer available.");
    m_formatStatus->setText(status);
    if (m_ifcOptionsGroup) m_ifcOptionsGroup->setVisible(isIfc);
    if (m_unit) m_unit->setEnabled(!isIfc);
}

void GeometryImportWizard::prepareIfcOptions()
{
    const GeometryImportFormat format = GeometryImportService::detectFormat(filePath());
    const bool isIfc = format == GeometryImportFormat::Ifc;
    m_ifcOptionsGroup->setVisible(isIfc);
    if (!isIfc) return;
    const QString source = filePath();
    if (m_ifcPreflight.sourceFile == source && m_ifcPreflight.success() &&
        m_ifcTypeTree->topLevelItemCount() > 0) {
        return;
    }
    m_ifcPreflight = IfcImportService::inspectFile(source);
    m_ifcTypeTree->clear();
    if (!m_ifcPreflight.success()) {
        m_ifcPreflightSummary->setText(m_ifcPreflight.errorMessage);
        return;
    }
    m_ifcPreflightSummary->setText(
        uiText("Schema %1; source unit %2; %3 products; %4 storeys; "
                       "%5 spaces; %6 material relations; %7 property sets.")
            .arg(m_ifcPreflight.schema, m_ifcPreflight.lengthUnit)
            .arg(m_ifcPreflight.productCount)
            .arg(m_ifcPreflight.storeyCount)
            .arg(m_ifcPreflight.spaceCount)
            .arg(m_ifcPreflight.materialRelationshipCount)
            .arg(m_ifcPreflight.propertySetCount));
    for (const QString& ifcClass : m_ifcPreflight.productClasses) {
        auto* item = new QTreeWidgetItem(m_ifcTypeTree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setText(1, ifcClass);
        item->setText(2, QString::number(m_ifcPreflight.classCounts.value(ifcClass)));
    }
    for (int column = 0; column < m_ifcTypeTree->columnCount(); ++column)
        m_ifcTypeTree->resizeColumnToContents(column);
}

IfcImportOptions GeometryImportWizard::ifcOptions() const
{
    IfcImportOptions value;
    value.additionalScale = m_scale->value();
    value.sourceYAxisUp = m_axis->currentIndex() != 0;
    value.originX = m_originX->value();
    value.originY = m_originY->value();
    value.originZ = m_originZ->value();
    for (int index = 0; index < m_ifcTypeTree->topLevelItemCount(); ++index) {
        const QTreeWidgetItem* item = m_ifcTypeTree->topLevelItem(index);
        if (item && item->checkState(0) == Qt::Checked)
            value.includedClasses.append(item->text(1));
    }
    value.mergeStrategy = m_ifcMergeStrategy->currentData().toString();
    value.simplification = m_ifcSimplification->currentData().toString();
    value.conversionRoute = m_ifcConversionRoute->currentData().toString();
    value.preserveMaterials = m_materials->isChecked();
    value.preservePropertySets = m_ifcPropertySets->isChecked();
    value.initiallyVisible = m_ifcVisible->isChecked();
    return value;
}

void GeometryImportWizard::buildPreview()
{
    if (m_importRunning) return;
    const GeometryImportFormat format = GeometryImportService::detectFormat(filePath());
    m_result = {};
    m_ifcResult = {};
    m_preview->clear();
    m_progress->setValue(0);
    m_progressStage->setText(uiText("Starting background import..."));
    m_cancelImport->setEnabled(true);
    m_importRunning = true;
    m_rejectWhenFinished = false;
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    if (button(QWizard::FinishButton)) button(QWizard::FinishButton)->setEnabled(false);
    if (button(QWizard::BackButton)) button(QWizard::BackButton)->setEnabled(false);

    QPointer<GeometryImportWizard> guarded(this);
    const auto progressCallback = [guarded](int percent, const QString& stage) {
        if (!guarded) return;
        QMetaObject::invokeMethod(
            guarded.data(), [guarded, percent, stage]() {
                if (guarded) guarded->updateImportProgress(percent, stage);
            }, Qt::QueuedConnection);
    };
    const std::shared_ptr<std::atomic_bool> cancelFlag = m_cancelRequested;
    const auto cancellationCheck = [cancelFlag]() {
        return cancelFlag && cancelFlag->load();
    };
    const QString source = filePath();
    if (format == GeometryImportFormat::Ifc) {
        prepareIfcOptions();
        const IfcImportOptions selectedOptions = ifcOptions();
        m_ifcWatcher->setFuture(QtConcurrent::run(
            [source, selectedOptions, progressCallback, cancellationCheck]() {
                return IfcImportService().importFile(
                    source, selectedOptions, progressCallback, cancellationCheck);
            }));
    } else {
        const GeometryImportOptions selectedOptions = options();
        m_geometryWatcher->setFuture(QtConcurrent::run(
            [source, selectedOptions, progressCallback, cancellationCheck]() {
                return GeometryImportService().importFile(
                    source, selectedOptions, progressCallback, cancellationCheck);
            }));
    }
}

void GeometryImportWizard::updateImportProgress(int percent, const QString& stage)
{
    if (!m_importRunning) return;
    m_progress->setValue(qBound(0, percent, 100));
    m_progressStage->setText(UiLanguageManager::text(stage));
}

void GeometryImportWizard::finishGeometryPreview()
{
    m_result = m_geometryWatcher->result();
    m_importRunning = false;
    m_cancelImport->setEnabled(false);
    if (button(QWizard::BackButton)) button(QWizard::BackButton)->setEnabled(true);
    if (button(QWizard::FinishButton))
        button(QWizard::FinishButton)->setEnabled(m_result.success());
    if (!m_result.success()) {
        m_preview->setPlainText(uiText("Import preview failed:\n%1").arg(m_result.errorMessage));
        m_progressStage->setText(m_result.cancelled
                                     ? uiText("Import cancelled.")
                                     : uiText("Import failed."));
        if (m_rejectWhenFinished) QWizard::reject();
        return;
    }
    const GeometryQualityReport& q = m_result.quality;
    QStringList lines{
        uiText("File: %1").arg(filePath()),
        uiText("Format: %1").arg(GeometryImportService::formatName(m_result.format)),
        uiText("Solids / shells / faces / vertices: %1 / %2 / %3 / %4")
            .arg(q.solids).arg(q.shells).arg(q.faces).arg(q.vertices),
        uiText("Triangles: %1").arg(q.triangles),
        uiText("Materials / textures detected: %1 / %2")
            .arg(q.materialCount).arg(q.textureCount),
        uiText("Duplicate vertices / faces: %1 / %2")
            .arg(q.duplicateVertices).arg(q.duplicateFaces),
        uiText("Degenerate triangles: %1").arg(q.degenerateTriangles),
        uiText("Boundary / non-manifold edges: %1 / %2")
            .arg(q.boundaryEdges).arg(q.nonManifoldEdges),
        uiText("Bounds [m]: X %1 .. %2, Y %3 .. %4, Z %5 .. %6")
            .arg(q.minimumX, 0, 'g', 8).arg(q.maximumX, 0, 'g', 8)
            .arg(q.minimumY, 0, 'g', 8).arg(q.maximumY, 0, 'g', 8)
            .arg(q.minimumZ, 0, 'g', 8).arg(q.maximumZ, 0, 'g', 8),
        uiText("Topology valid: %1").arg(q.validTopology ? uiText("Yes") : uiText("No")),
        uiText("Closed: %1").arg(q.closed ? uiText("Yes") : uiText("No")),
        uiText("Source size: %1 bytes").arg(q.sourceBytes),
        uiText("Import time: %1 ms").arg(q.elapsedMilliseconds)
    };
    if (!m_result.warnings.isEmpty()) {
        lines.append(QStringLiteral(""));
        lines.append(uiText("Warnings:"));
        for (const QString& warning : m_result.warnings)
            lines.append(QStringLiteral("- %1").arg(warning));
    }
    m_preview->setPlainText(lines.join(QLatin1Char('\n')));
    m_progress->setValue(100);
    m_progressStage->setText(uiText("Preview ready."));
    if (m_rejectWhenFinished) QWizard::reject();
}

void GeometryImportWizard::finishIfcPreview()
{
    m_ifcResult = m_ifcWatcher->result();
    m_importRunning = false;
    m_cancelImport->setEnabled(false);
    if (button(QWizard::BackButton)) button(QWizard::BackButton)->setEnabled(true);
    if (button(QWizard::FinishButton))
        button(QWizard::FinishButton)->setEnabled(m_ifcResult.success());
    if (!m_ifcResult.success()) {
        m_preview->setPlainText(
            uiText("IFC import preview failed:\n%1")
                .arg(m_ifcResult.errorMessage));
        m_progressStage->setText(m_ifcResult.cancelled
                                     ? uiText("Import cancelled.")
                                     : uiText("Import failed."));
        if (m_rejectWhenFinished) QWizard::reject();
        return;
    }
    const IfcPreflightReport& p = m_ifcResult.preflight;
    const IfcImportOptions selectedOptions = ifcOptions();
    QStringList lines{
        uiText("File: %1").arg(filePath()),
        uiText("Format / importer: IFC / isolated IfcOpenShell worker"),
        uiText("Schema / detected source unit: %1 / %2")
            .arg(p.schema, p.lengthUnit),
        uiText("Entities / products / geometry objects: %1 / %2 / %3")
            .arg(p.entityCount).arg(p.productCount)
            .arg(m_ifcResult.geometryObjectCount),
        uiText("Storeys / spaces: %1 / %2")
            .arg(p.storeyCount).arg(p.spaceCount),
        uiText("Material relations / Property Sets: %1 / %2")
            .arg(p.materialRelationshipCount).arg(p.propertySetCount),
        uiText("Filtered geometry objects: %1")
            .arg(m_ifcResult.filteredObjectCount),
        uiText("Merge / simplification: %1 / %2")
            .arg(selectedOptions.mergeStrategy, selectedOptions.simplification),
        uiText("FDS conversion route: %1")
            .arg(selectedOptions.conversionRoute),
        uiText("Selected IFC product classes: %1")
            .arg(selectedOptions.includedClasses.isEmpty()
                     ? uiText("All")
                     : selectedOptions.includedClasses.join(QStringLiteral(", ")))
    };
    if (!m_ifcResult.failedComponents.isEmpty()) {
        lines.append(QStringLiteral(""));
        lines.append(uiText("Failed or unmatched component GlobalIds (%1):")
                         .arg(m_ifcResult.failedComponents.size()));
        const int limit = qMin(50, static_cast<int>(m_ifcResult.failedComponents.size()));
        for (int index = 0; index < limit; ++index)
            lines.append(QStringLiteral("- %1").arg(m_ifcResult.failedComponents.at(index)));
        if (m_ifcResult.failedComponents.size() > limit)
            lines.append(uiText("- ... %1 more")
                             .arg(m_ifcResult.failedComponents.size() - limit));
    }
    if (!m_ifcResult.warnings.isEmpty()) {
        lines.append(QStringLiteral(""));
        lines.append(uiText("Warnings:"));
        for (const QString& warning : m_ifcResult.warnings)
            lines.append(QStringLiteral("- %1").arg(warning));
    }
    m_preview->setPlainText(lines.join(QLatin1Char('\n')));
    m_progress->setValue(100);
    m_progressStage->setText(uiText("IFC preview ready."));
    if (m_rejectWhenFinished) QWizard::reject();
}

void GeometryImportWizard::cancelImport()
{
    if (!m_importRunning || !m_cancelRequested) return;
    m_cancelRequested->store(true);
    m_cancelImport->setEnabled(false);
    m_progressStage->setText(uiText("Cancellation requested..."));
}
