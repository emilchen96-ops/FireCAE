#include "ui/SimulationRunDialog.h"

#include "fds/FdsSchema.h"
#include "ui/UiLanguage.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace
{
QString t(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

FdsRunMode modeForKind(SolverBackendKind kind)
{
    if (kind == SolverBackendKind::FdsOpenMpCpu) return FdsRunMode::OpenMp;
    if (kind == SolverBackendKind::FdsMpiCpu) return FdsRunMode::Mpi;
    return FdsRunMode::Serial;
}
}

SimulationRunDialog::SimulationRunDialog(const QString& inputFilePath,
                                         const QString& fdsExecutablePath,
                                         FdsRunMode initialMode,
                                         int initialCount,
                                         const QString& expectedSchemaVersion,
                                         QWidget* parent)
    : QDialog(parent)
    , m_inputFilePath(QFileInfo(inputFilePath).absoluteFilePath())
    , m_fdsExecutablePath(fdsExecutablePath)
    , m_expectedSchemaVersion(expectedSchemaVersion.trimmed())
{
    setObjectName(QStringLiteral("SimulationRunDialog"));
    setWindowTitle(t("Run FDS"));
    resize(780, 430);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* input = new QLabel(QFileInfo(m_inputFilePath).fileName(), this);
    input->setObjectName(QStringLiteral("SimulationRunInputLabel"));
    input->setToolTip(m_inputFilePath);
    m_backend = new QComboBox(this);
    m_backend->setObjectName(QStringLiteral("SimulationRunBackendCombo"));
    m_backend->addItem(t("Native FDS — Serial CPU"),
                       static_cast<int>(SolverBackendKind::FdsSerialCpu));
    m_backend->addItem(t("Native FDS — OpenMP CPU"),
                       static_cast<int>(SolverBackendKind::FdsOpenMpCpu));
    m_backend->addItem(t("Native FDS — MPI CPU"),
                       static_cast<int>(SolverBackendKind::FdsMpiCpu));
    const SolverBackendKind initialKind =
        initialMode == FdsRunMode::Mpi
            ? SolverBackendKind::FdsMpiCpu
            : initialMode == FdsRunMode::OpenMp
                  ? SolverBackendKind::FdsOpenMpCpu
                  : SolverBackendKind::FdsSerialCpu;
    m_backend->setCurrentIndex(m_backend->findData(static_cast<int>(initialKind)));
    m_countLabel = new QLabel(this);
    m_count = new QSpinBox(this);
    m_count->setObjectName(QStringLiteral("SimulationRunCpuCountSpin"));
    m_count->setRange(1, qMax(1, QThread::idealThreadCount()));
    m_count->setValue(qBound(1, initialCount, m_count->maximum()));
    m_workingDirectory = new QLabel(this);
    m_workingDirectory->setObjectName(QStringLiteral("SimulationRunWorkingDirectory"));
    m_workingDirectory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_workingDirectory->setWordWrap(true);
    m_environment = new QLabel(this);
    m_environment->setObjectName(QStringLiteral("SimulationRunEnvironmentCheck"));
    m_environment->setWordWrap(true);
    m_solverVersion = new QLabel(this);
    m_solverVersion->setObjectName(QStringLiteral("SimulationRunSolverVersion"));
    m_solverVersion->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_solverVersion->setWordWrap(true);
    form->addRow(t("Input file:"), input);
    form->addRow(t("CPU execution mode:"), m_backend);
    form->addRow(m_countLabel, m_count);
    form->addRow(t("Working directory:"), m_workingDirectory);
    form->addRow(t("FDS version / Schema:"), m_solverVersion);
    form->addRow(t("Environment check:"), m_environment);
    root->addLayout(form);
    auto* commandLabel = new QLabel(t("Command preview (read-only):"), this);
    root->addWidget(commandLabel);
    m_command = new QPlainTextEdit(this);
    m_command->setObjectName(QStringLiteral("SimulationRunCommandPreview"));
    m_command->setReadOnly(true);
    m_command->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_command->setMaximumHeight(105);
    root->addWidget(m_command);
    auto* note = new QLabel(t(
        "FireCAE exports and validates the FDS input before this dialog. Serial uses one CPU thread, OpenMP uses shared-memory CPU threads, and MPI uses multiple CPU processes. No GPU solver is configured."),
        this);
    note->setObjectName(QStringLiteral("SimulationRunModeExplanation"));
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch();
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel,
                                     this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(t("Queue Run"));
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_backend, &QComboBox::currentIndexChanged,
            this, [this]() { updatePreview(); });
    connect(m_count, &QSpinBox::valueChanged,
            this, [this]() { updatePreview(); });
    updatePreview();
}

SolverBackendKind SimulationRunDialog::selectedBackendKind() const
{
    return static_cast<SolverBackendKind>(m_backend->currentData().toInt());
}

FdsRunRequest SimulationRunDialog::request() const
{
    FdsRunRequest result;
    result.inputFilePath = m_inputFilePath;
    result.executablePath = m_fdsExecutablePath;
    result.mode = modeForKind(selectedBackendKind());
    result.processCount = result.mode == FdsRunMode::Mpi ? m_count->value() : 1;
    result.threadCount = result.mode == FdsRunMode::OpenMp ? m_count->value() : 1;
    return result;
}

SolverLaunchPlan SimulationRunDialog::launchPlan() const
{
    const FdsRunRequest runRequest = request();
    SolverLaunchContext context;
    context.inputFilePath = runRequest.inputFilePath;
    context.fdsExecutablePath = runRequest.executablePath;
    context.bfdsExecutablePath =
        SolverBackendRegistry::configuredBfdsExecutable();
    context.processCount = runRequest.processCount;
    context.threadCount = runRequest.threadCount;
    return SolverBackendRegistry::create(selectedBackendKind())
        ->createLaunchPlan(context);
}

void SimulationRunDialog::updatePreview()
{
    const SolverBackendKind kind = selectedBackendKind();
    const bool serial = kind == SolverBackendKind::FdsSerialCpu;
    const bool openMp = kind == SolverBackendKind::FdsOpenMpCpu;
    m_countLabel->setText(openMp ? t("OpenMP CPU threads:")
                                 : kind == SolverBackendKind::FdsMpiCpu
                                       ? t("MPI CPU processes:")
                                       : t("CPU threads:"));
    m_count->setEnabled(!serial);
    m_count->setMinimum(kind == SolverBackendKind::FdsMpiCpu ? 2 : 1);
    if (serial) m_count->setValue(1);
    const SolverLaunchPlan plan = launchPlan();
    m_workingDirectory->setText(plan.workingDirectory);
    m_command->setPlainText(plan.commandLine);
    if (plan.valid()) {
        const QString executable = QFileInfo(plan.solverExecutable).absoluteFilePath();
        if (m_probedExecutable.compare(executable, Qt::CaseInsensitive) != 0) {
            m_probedExecutable = executable;
            m_detectedSolverVersion = FdsRunner::probeVersion(
                plan, &m_versionProbeError, 5000);
        }
        const QString compatibleSchema =
            FdsSchemaRegistry::compatibleVersionForRevision(
                m_detectedSolverVersion);
        const bool schemaMismatch = !m_expectedSchemaVersion.isEmpty() &&
                                    !compatibleSchema.isEmpty() &&
                                    m_expectedSchemaVersion.compare(
                                        compatibleSchema,
                                        Qt::CaseInsensitive) != 0;
        if (!m_detectedSolverVersion.isEmpty()) {
            m_solverVersion->setText(
                m_expectedSchemaVersion.isEmpty()
                    ? t("Solver %1").arg(m_detectedSolverVersion)
                    : t("Solver %1; project Schema %2")
                          .arg(m_detectedSolverVersion,
                               m_expectedSchemaVersion));
        } else {
            m_solverVersion->setText(
                t("Unknown (%1)").arg(m_versionProbeError));
        }
        m_solverVersion->setStyleSheet(
            schemaMismatch ? QStringLiteral("color: #b45309; font-weight: 600;")
                           : QString{});
        const QString ompThreads = plan.environment.value(
            QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
        QString environmentText =
            t("Ready. Solver: %1; OMP_NUM_THREADS=%2")
                .arg(QFileInfo(plan.solverExecutable).fileName(), ompThreads);
        if (schemaMismatch) {
            environmentText += t(
                " Warning: project Schema %1 does not match solver-compatible Schema %2.")
                                   .arg(m_expectedSchemaVersion,
                                        compatibleSchema);
        }
        m_environment->setText(environmentText);
    } else {
        m_solverVersion->clear();
        m_environment->setText(t("Unavailable: %1").arg(plan.errorMessage));
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(plan.valid());
}
