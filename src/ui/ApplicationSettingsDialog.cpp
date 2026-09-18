#include "ui/ApplicationSettingsDialog.h"
#include "ui/UiLanguage.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
QString u(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

void selectData(QComboBox* combo, const QString& data)
{
    if (!combo) return;
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QWidget* pathEditor(QLineEdit*& edit,
                    const QString& objectName,
                    bool directory,
                    QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(container);
    edit->setObjectName(objectName);
    auto* browse = new QPushButton(u("Browse..."), container);
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, container,
                     [edit, directory, container]() {
        const QString path = directory
            ? QFileDialog::getExistingDirectory(container,
                                                u("Choose Directory"),
                                                edit->text())
            : QFileDialog::getOpenFileName(container,
                                           u("Choose Executable"),
                                           edit->text(),
                                           QStringLiteral("Executables (*.exe);;All Files (*.*)"));
        if (!path.isEmpty()) edit->setText(path);
    });
    return container;
}
}

ApplicationSettingsDialog::ApplicationSettingsDialog(
    const ApplicationSettings& settings,
    QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ApplicationSettingsDialog"));
    setWindowTitle(u("FireCAE Preferences"));
    resize(720, 560);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("ApplicationSettingsTabs"));
    root->addWidget(tabs, 1);

    auto* generalPage = new QWidget(tabs);
    auto* general = new QFormLayout(generalPage);
    m_language = new QComboBox(generalPage);
    m_language->setObjectName(QStringLiteral("SettingsLanguageCombo"));
    m_language->addItem(u("Follow system"), QStringLiteral("system"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_unit = new QComboBox(generalPage);
    m_unit->setObjectName(QStringLiteral("SettingsUnitCombo"));
    for (const auto& item : {qMakePair(QStringLiteral("Meters (m)"), QStringLiteral("m")),
                             qMakePair(QStringLiteral("Centimeters (cm)"), QStringLiteral("cm")),
                             qMakePair(QStringLiteral("Millimeters (mm)"), QStringLiteral("mm")),
                             qMakePair(QStringLiteral("Feet (ft)"), QStringLiteral("ft")),
                             qMakePair(QStringLiteral("Inches (in)"), QStringLiteral("in"))})
    m_unit->addItem(UiLanguageManager::text(item.first), item.second);
    m_theme = new QComboBox(generalPage);
    m_theme->setObjectName(QStringLiteral("SettingsThemeCombo"));
    m_theme->addItem(u("Follow system"), QStringLiteral("system"));
    m_theme->addItem(u("Light"), QStringLiteral("light"));
    m_theme->addItem(u("Dark"), QStringLiteral("dark"));
    m_backgroundColor = new QLineEdit(generalPage);
    m_backgroundColor->setObjectName(QStringLiteral("SettingsBackgroundColorEdit"));
    auto* colorContainer = new QWidget(generalPage);
    auto* colorLayout = new QHBoxLayout(colorContainer);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    auto* colorButton = new QPushButton(u("Choose..."), colorContainer);
    colorLayout->addWidget(m_backgroundColor, 1);
    colorLayout->addWidget(colorButton);
    connect(colorButton, &QPushButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(QColor(m_backgroundColor->text()), this);
        if (color.isValid()) m_backgroundColor->setText(color.name(QColor::HexRgb));
    });
    general->addRow(u("Language:"), m_language);
    general->addRow(u("Default project unit:"), m_unit);
    general->addRow(u("Theme:"), m_theme);
    general->addRow(u("3D background:"), colorContainer);
    tabs->addTab(generalPage, u("General"));

    auto* applicationsPage = new QWidget(tabs);
    auto* applications = new QFormLayout(applicationsPage);
    applications->addRow(u("FDS executable:"),
                         pathEditor(m_fdsExecutable,
                                    QStringLiteral("SettingsFdsExecutableEdit"),
                                    false,
                                    applicationsPage));
    applications->addRow(u("MPI launcher:"),
                         pathEditor(m_mpiExecutable,
                                    QStringLiteral("SettingsMpiExecutableEdit"),
                                    false,
                                    applicationsPage));
    applications->addRow(u("Smokeview executable:"),
                         pathEditor(m_smokeviewExecutable,
                                    QStringLiteral("SettingsSmokeviewExecutableEdit"),
                                    false,
                                    applicationsPage));
    m_mpiProcessCount = new QSpinBox(applicationsPage);
    m_mpiProcessCount->setObjectName(QStringLiteral("SettingsMpiProcessCountSpin"));
    m_mpiProcessCount->setRange(1, 1024);
    m_autoOpenResults = new QCheckBox(u("Load results when a run completes"),
                                     applicationsPage);
    m_autoOpenResults->setObjectName(QStringLiteral("SettingsAutoOpenResultsCheck"));
    m_saveBeforeRun = new QCheckBox(u("Save the FireCAE project before each run"),
                                    applicationsPage);
    m_saveBeforeRun->setObjectName(QStringLiteral("SettingsSaveBeforeRunCheck"));
    applications->addRow(u("Default CPU/MPI processes:"), m_mpiProcessCount);
    applications->addRow(QString{}, m_autoOpenResults);
    applications->addRow(QString{}, m_saveBeforeRun);
    applications->addRow(u("Default working directory:"),
                         pathEditor(m_defaultWorkingDirectory,
                                    QStringLiteral("SettingsWorkingDirectoryEdit"),
                                    true,
                                    applicationsPage));
    tabs->addTab(applicationsPage, u("Applications"));

    auto* defaultsPage = new QWidget(tabs);
    auto* defaults = new QFormLayout(defaultsPage);
    m_defaultMeshCellSize = new QDoubleSpinBox(defaultsPage);
    m_defaultMeshCellSize->setObjectName(QStringLiteral("SettingsDefaultMeshCellSizeSpin"));
    m_defaultMeshCellSize->setRange(0.001, 1000.0);
    m_defaultMeshCellSize->setDecimals(4);
    m_defaultMeshCellSize->setSuffix(QStringLiteral(" m"));
    m_defaultMaterial = new QLineEdit(defaultsPage);
    m_defaultMaterial->setObjectName(QStringLiteral("SettingsDefaultMaterialEdit"));
    m_defaultColorScheme = new QComboBox(defaultsPage);
    m_defaultColorScheme->setObjectName(QStringLiteral("SettingsDefaultColorSchemeCombo"));
    m_defaultColorScheme->addItem(u("Rainbow"), QStringLiteral("rainbow"));
    m_defaultColorScheme->addItem(u("Viridis"), QStringLiteral("viridis"));
    m_defaultColorScheme->addItem(u("Fire"), QStringLiteral("fire"));
    defaults->addRow(u("Default mesh cell size:"), m_defaultMeshCellSize);
    defaults->addRow(u("Default material FDS ID:"), m_defaultMaterial);
    defaults->addRow(u("Default result color scheme:"), m_defaultColorScheme);
    tabs->addTab(defaultsPage, u("Model Defaults"));

    auto* reliabilityPage = new QWidget(tabs);
    auto* reliability = new QFormLayout(reliabilityPage);
    m_autoSaveEnabled = new QCheckBox(u("Enable automatic recovery snapshots"),
                                     reliabilityPage);
    m_autoSaveEnabled->setObjectName(QStringLiteral("SettingsAutoSaveCheck"));
    m_autoSaveInterval = new QSpinBox(reliabilityPage);
    m_autoSaveInterval->setObjectName(QStringLiteral("SettingsAutoSaveIntervalSpin"));
    m_autoSaveInterval->setRange(1, 120);
    m_autoSaveInterval->setSuffix(QStringLiteral(" min"));
    m_autoSaveMaximum = new QSpinBox(reliabilityPage);
    m_autoSaveMaximum->setObjectName(QStringLiteral("SettingsAutoSaveMaximumSpin"));
    m_autoSaveMaximum->setRange(1, 100);
    m_backupBeforeOpen = new QCheckBox(
        u("Create a rotating backup before opening a project"),
        reliabilityPage);
    m_backupBeforeOpen->setObjectName(QStringLiteral("SettingsBackupBeforeOpenCheck"));
    m_highDpiEnabled = new QCheckBox(
        u("Enable high-DPI scaling (restart required)"), reliabilityPage);
    m_highDpiEnabled->setObjectName(QStringLiteral("SettingsHighDpiCheck"));
    m_largeFileWarning = new QSpinBox(reliabilityPage);
    m_largeFileWarning->setObjectName(QStringLiteral("SettingsLargeFileWarningSpin"));
    m_largeFileWarning->setRange(10, 102400);
    m_largeFileWarning->setSuffix(QStringLiteral(" MB"));
    m_logLevel = new QComboBox(reliabilityPage);
    m_logLevel->setObjectName(QStringLiteral("SettingsLogLevelCombo"));
    for (const QString& level : {QStringLiteral("debug"), QStringLiteral("info"),
                                 QStringLiteral("warning"), QStringLiteral("error")})
        m_logLevel->addItem(level, level);
    m_renderQuality = new QComboBox(reliabilityPage);
    m_renderQuality->setObjectName(QStringLiteral("SettingsRenderQualityCombo"));
    m_renderQuality->addItem(u("Performance"), QStringLiteral("performance"));
    m_renderQuality->addItem(u("Balanced"), QStringLiteral("balanced"));
    m_renderQuality->addItem(u("High"), QStringLiteral("high"));
    reliability->addRow(QString{}, m_autoSaveEnabled);
    reliability->addRow(u("Auto-save interval:"), m_autoSaveInterval);
    reliability->addRow(u("Recovery copies retained:"), m_autoSaveMaximum);
    reliability->addRow(QString{}, m_backupBeforeOpen);
    reliability->addRow(u("Large-file warning threshold:"), m_largeFileWarning);
    reliability->addRow(QString{}, m_highDpiEnabled);
    reliability->addRow(u("Log level:"), m_logLevel);
    reliability->addRow(u("Rendering quality:"), m_renderQuality);
    tabs->addTab(reliabilityPage, u("Reliability & Rendering"));

    auto* note = new QLabel(
        u("Settings use a versioned format. Recovery copies and logs are stored outside formal project files."), this);
    note->setWordWrap(true);
    root->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    selectData(m_language, settings.language);
    selectData(m_unit, settings.defaultUnit);
    selectData(m_theme, settings.theme);
    m_backgroundColor->setText(settings.backgroundColor);
    m_autoSaveEnabled->setChecked(settings.autoSaveEnabled);
    m_autoSaveInterval->setValue(settings.autoSaveIntervalMinutes);
    m_autoSaveMaximum->setValue(settings.autoSaveMaximumFiles);
    m_fdsExecutable->setText(settings.fdsExecutable);
    m_mpiExecutable->setText(settings.mpiExecutable);
    m_smokeviewExecutable->setText(settings.smokeviewExecutable);
    m_mpiProcessCount->setValue(settings.mpiProcessCount);
    m_autoOpenResults->setChecked(settings.autoOpenResults);
    m_backupBeforeOpen->setChecked(settings.backupBeforeOpen);
    m_saveBeforeRun->setChecked(settings.saveBeforeRun);
    m_defaultWorkingDirectory->setText(settings.defaultWorkingDirectory);
    m_defaultMeshCellSize->setValue(settings.defaultMeshCellSize);
    m_defaultMaterial->setText(settings.defaultMaterial);
    selectData(m_defaultColorScheme, settings.defaultColorScheme);
    m_highDpiEnabled->setChecked(settings.highDpiEnabled);
    m_largeFileWarning->setValue(settings.largeFileWarningMegabytes);
    selectData(m_logLevel, settings.logLevel);
    selectData(m_renderQuality, settings.renderQuality);
}

ApplicationSettings ApplicationSettingsDialog::settings() const
{
    ApplicationSettings result;
    result.language = m_language->currentData().toString();
    result.defaultUnit = m_unit->currentData().toString();
    result.theme = m_theme->currentData().toString();
    result.backgroundColor = QColor(m_backgroundColor->text()).isValid()
                                 ? QColor(m_backgroundColor->text()).name(QColor::HexRgb)
                                 : QStringLiteral("#687181");
    result.autoSaveEnabled = m_autoSaveEnabled->isChecked();
    result.autoSaveIntervalMinutes = m_autoSaveInterval->value();
    result.autoSaveMaximumFiles = m_autoSaveMaximum->value();
    result.fdsExecutable = m_fdsExecutable->text().trimmed();
    result.mpiExecutable = m_mpiExecutable->text().trimmed();
    result.smokeviewExecutable = m_smokeviewExecutable->text().trimmed();
    result.mpiProcessCount = m_mpiProcessCount->value();
    result.autoOpenResults = m_autoOpenResults->isChecked();
    result.backupBeforeOpen = m_backupBeforeOpen->isChecked();
    result.saveBeforeRun = m_saveBeforeRun->isChecked();
    result.defaultWorkingDirectory = m_defaultWorkingDirectory->text().trimmed();
    result.defaultMeshCellSize = m_defaultMeshCellSize->value();
    result.defaultMaterial = m_defaultMaterial->text().trimmed();
    result.defaultColorScheme = m_defaultColorScheme->currentData().toString();
    result.highDpiEnabled = m_highDpiEnabled->isChecked();
    result.largeFileWarningMegabytes = m_largeFileWarning->value();
    result.logLevel = m_logLevel->currentData().toString();
    result.renderQuality = m_renderQuality->currentData().toString();
    return result;
}
