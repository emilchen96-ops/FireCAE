#include "ui/FdsSchemaEditorWidget.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "ui/UiLanguage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
QString trText(const QString& english) { return UiLanguageManager::text(english); }

QString friendlyParameterName(const QString& key)
{
    static const QHash<QString, QString> names = {
        {"DENSITY", "Density"}, {"CONDUCTIVITY", "Thermal conductivity"},
        {"SPECIFIC_HEAT", "Specific heat"}, {"EMISSIVITY", "Emissivity"},
        {"N_REACTIONS", "Number of pyrolysis reactions"},
        {"HEAT_OF_REACTION", "Heat of reaction"},
        {"HEAT_OF_COMBUSTION", "Heat of combustion"},
        {"REFERENCE_TEMPERATURE", "Reference temperature"},
        {"HEATING_RATE", "Heating rate"}, {"MOISTURE_FRACTION", "Moisture fraction"},
        {"ABSORPTION_COEFFICIENT", "Absorption coefficient"},
        {"MATL_ID", "Material layers"}, {"THICKNESS", "Layer thicknesses"},
        {"BACKING", "Back-face boundary"},
        {"HRRPUA", "Heat release rate per unit area"},
        {"MLRPUA", "Mass loss rate per unit area"},
        {"VEL", "Normal flow velocity"}, {"VOLUME_FLUX", "Volume flow per unit area"},
        {"TMP_FRONT", "Front-face temperature"}, {"COLOR", "Display color"},
        {"FUEL", "Fuel"}, {"SOOT_YIELD", "Soot yield"},
        {"CO_YIELD", "Carbon monoxide yield"},
        {"RADIATIVE_FRACTION", "Radiative fraction"},
        {"QUANTITY", "Measured quantity"}, {"XYZ", "Point location"},
        {"XB", "Region bounds"}, {"PROP_ID", "Detector / nozzle property"},
        {"PART_ID", "Particle type"}, {"SETPOINT", "Activation threshold"},
        {"TRIP_DIRECTION", "Trigger direction"}, {"INITIAL_STATE", "Initial state"},
        {"FUNCTION_TYPE", "Logic operation"}, {"INPUT_ID", "Trigger inputs"},
        {"RAMP_ID", "Time curve"}, {"CONSTANT", "Constants / threshold values"},
        {"TYPE_ID", "Component type"}, {"NODE_ID", "Connected nodes"},
        {"DUCT_ID", "Connected ducts"}, {"VENT_ID", "Connected HVAC vents"},
        {"FAN_ID", "Fan"}, {"AIRCOIL_ID", "Heating / cooling coil"},
        {"AREA", "Flow area"}, {"LENGTH", "Duct length"},
        {"VOLUME_FLOW", "Volume flow rate"}, {"LOSS", "Loss coefficients"},
        {"PBX", "X plane coordinate"}, {"PBY", "Y plane coordinate"},
        {"PBZ", "Z plane coordinate"}, {"VECTOR", "Include vector components"},
        {"SPEC_ID", "Species"}, {"SURF_ID", "Surface"},
        {"CTRL_ID", "Control trigger"}, {"DEVC_ID", "Device trigger"}};
    return trText(names.value(key, key));
}

bool isBasicParameter(const QString& keyword, const QString& key)
{
    static const QHash<QString, QSet<QString>> fields = {
        {"MATL", {"DENSITY", "CONDUCTIVITY", "SPECIFIC_HEAT", "EMISSIVITY"}},
        {"SURF", {"MATL_ID", "THICKNESS", "HRRPUA", "VEL", "TMP_FRONT", "COLOR",
                  "PART_ID", "SPEC_ID"}},
        {"REAC", {"FUEL", "SOOT_YIELD", "CO_YIELD", "RADIATIVE_FRACTION"}},
        {"DEVC", {"QUANTITY", "XYZ", "PROP_ID", "SETPOINT", "TRIP_DIRECTION"}},
        {"CTRL", {"FUNCTION_TYPE", "INPUT_ID", "CONSTANT", "INITIAL_STATE"}},
        {"VENT", {"XB", "MB", "SURF_ID", "CTRL_ID", "DEVC_ID", "INITIAL_STATE"}},
        {"OBST", {"XB", "SURF_ID", "CTRL_ID", "DEVC_ID", "INITIAL_STATE", "COLOR"}},
        {"HVAC", {"TYPE_ID", "NODE_ID", "DUCT_ID", "VENT_ID", "AREA", "LENGTH", "VOLUME_FLOW"}},
        {"SLCF", {"QUANTITY", "PBX", "PBY", "PBZ", "VECTOR"}},
        {"BNDF", {"QUANTITY"}}, {"ISOF", {"QUANTITY"}},
        {"PL3D", {"QUANTITY", "VECTOR"}},
        {"SM3D", {"QUANTITY", "SPEC_ID"}},
        {"PROF", {"QUANTITY", "XYZ"}},
        {"PART", {"SPEC_ID", "SURF_ID"}}, {"PROP", {"QUANTITY"}}};
    return fields.value(keyword).contains(key);
}

QString friendlyCategory(const QString& category)
{
    static const QHash<QString, QString> names = {
        {"Thermal", "Thermal properties"}, {"Pyrolysis", "Pyrolysis and reactions"},
        {"Layers", "Material layers"}, {"Fire", "Fire and heat release"},
        {"Measurement", "Measurement"}, {"Location", "Location and region"},
        {"Activation", "Activation"}, {"Logic", "Trigger logic"},
        {"Topology", "Network connections"}, {"Flow", "Flow and pressure loss"}};
    return trText(names.value(category, category));
}

QString parameterBase(const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    const int suffix = normalized.indexOf(QLatin1Char('('));
    return normalized.left(suffix < 0 ? normalized.size() : suffix);
}

QString keywordForObject(const FcObject::Ptr& object)
{
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        return namelist->keyword();
    }
    if (!object) return {};
    switch (object->type()) {
    case FcObjectType::Mesh: return QStringLiteral("MESH");
    case FcObjectType::MeshMultiplier: return QStringLiteral("MULT");
    case FcObjectType::Species: return QStringLiteral("SPEC");
    case FcObjectType::Material: return QStringLiteral("MATL");
    case FcObjectType::Surface: return QStringLiteral("SURF");
    case FcObjectType::Reaction: return QStringLiteral("REAC");
    case FcObjectType::Obstruction: return QStringLiteral("OBST");
    case FcObjectType::Vent: return QStringLiteral("VENT");
    case FcObjectType::Particle: return QStringLiteral("PART");
    case FcObjectType::Property: return QStringLiteral("PROP");
    case FcObjectType::Table: return QStringLiteral("TABL");
    case FcObjectType::Ramp: return QStringLiteral("RAMP");
    case FcObjectType::Device: return QStringLiteral("DEVC");
    case FcObjectType::Control: return QStringLiteral("CTRL");
    case FcObjectType::HVAC: return QStringLiteral("HVAC");
    default: return {};
    }
}

void collectObjects(const FcObject::Ptr& root, std::vector<FcObject::Ptr>& result)
{
    if (!root) return;
    if (!keywordForObject(root).isEmpty()) result.push_back(root);
    for (const FcObject::Ptr& child : root->children()) collectObjects(child, result);
}

QString fdsIdForObject(const FcObject::Ptr& object)
{
    const auto fds = std::dynamic_pointer_cast<FcFdsObject>(object);
    return fds ? fds->fdsId() : QString{};
}
}

FdsSchemaEditorWidget::FdsSchemaEditorWidget(const FcProject* project,
                                             const FcFdsNamelist* currentObject,
                                             FdsSchemaEditorMode mode,
                                             QWidget* parent)
    : QWidget(parent), m_project(project), m_currentObject(currentObject), m_mode(mode)
{
    setObjectName(m_mode == FdsSchemaEditorMode::Basic
                      ? QStringLiteral("FdsBasicSchemaEditor")
                      : QStringLiteral("FdsProfessionalSchemaEditor"));
    auto* root = new QVBoxLayout(this);
    m_schemaLabel = new QLabel(this);
    m_schemaLabel->setObjectName(QStringLiteral("FdsSchemaVersionLabel"));
    m_schemaLabel->setWordWrap(true);
    root->addWidget(m_schemaLabel);
    m_categoryTabs = new QTabWidget(this);
    m_categoryTabs->setObjectName(QStringLiteral("FdsSchemaCategoryTabs"));
    root->addWidget(m_categoryTabs, 1);
}

QString FdsSchemaEditorWidget::currentValue(
    const QString& key, const std::vector<FcFdsParameter>& parameters) const
{
    for (const FcFdsParameter& parameter : parameters) {
        if (parameterBase(parameter.key) == key) return parameter.value;
    }
    return {};
}

QStringList FdsSchemaEditorWidget::currentReferences(
    const QString& key, const std::vector<FcFdsParameter>& parameters) const
{
    for (const FcFdsParameter& parameter : parameters) {
        if (parameterBase(parameter.key) == key) return parameter.targetObjectIds;
    }
    return {};
}

void FdsSchemaEditorWidget::setNamelist(
    const QString& keyword, const std::vector<FcFdsParameter>& parameters,
    const QString& version)
{
    // Retired pages survive until deleteLater is processed. Their callbacks
    // must not write into a newly loaded record or parameter draft.
    const quint64 generation = ++m_editorGeneration;
    m_keyword = keyword.trimmed().toUpper();
    while (m_categoryTabs->count() > 0) {
        QWidget* page = m_categoryTabs->widget(0);
        m_categoryTabs->removeTab(0);
        page->deleteLater();
    }
    const FdsNamelistSchema* schema = FdsSchemaRegistry::namelist(m_keyword, version);
    if (!schema) {
        m_schemaLabel->setText(trText(QStringLiteral(
            "This namelist is not modeled by the selected schema. Its parameters remain lossless in Advanced FDS Parameters.")));
        return;
    }
    m_schemaLabel->setText(m_mode == FdsSchemaEditorMode::Basic
        ? trText(QStringLiteral("Use these readable fields for the normal workflow. References are stored by UUID and converted to FDS IDs only during export."))
        : trText(QStringLiteral("FDS %1 schema — %2. Less common engineering parameters are shown here; unknown fields remain lossless on the Advanced page."))
              .arg(version, schema->title));

    struct Page { QWidget* scrollContents = nullptr; QFormLayout* form = nullptr; };
    QMap<QString, Page> pages;
    for (const FdsParameterSchema& definition : schema->parameters) {
        const bool basic = isBasicParameter(m_keyword, definition.name);
        if (m_mode == FdsSchemaEditorMode::Basic && !basic) continue;
        if (m_mode == FdsSchemaEditorMode::Professional && basic) continue;
        if (m_keyword == QStringLiteral("MESH") &&
            (definition.name == QStringLiteral("IJK") ||
             definition.name == QStringLiteral("XB"))) continue;
        if (!pages.contains(definition.category)) {
            auto* scroll = new QScrollArea(m_categoryTabs);
            scroll->setWidgetResizable(true);
            auto* contents = new QWidget(scroll);
            auto* form = new QFormLayout(contents);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            scroll->setWidget(contents);
            m_categoryTabs->addTab(scroll, friendlyCategory(definition.category));
            pages.insert(definition.category, {contents, form});
        }
        QFormLayout* form = pages[definition.category].form;
        const QString current = currentValue(definition.name, parameters);
        const QString initial = current.isEmpty() ? definition.defaultValue : current;
        QString labelText = friendlyParameterName(definition.name) +
                            (definition.required ? QStringLiteral(" *") : QString{});
        if (!definition.unit.isEmpty()) labelText += QStringLiteral(" [%1]").arg(definition.unit);
        auto* label = new QLabel(labelText, pages[definition.category].scrollContents);
        QString tooltip = QStringLiteral("FDS: %1\n%2")
                              .arg(definition.name,
                                   FdsSchemaRegistry::valueTypeName(definition.type));
        if (definition.arrayLength > 0) tooltip += QStringLiteral(" × %1").arg(definition.arrayLength);
        if (definition.minimum) tooltip += QStringLiteral("; min=%1").arg(*definition.minimum);
        if (definition.maximum) tooltip += QStringLiteral("; max=%1").arg(*definition.maximum);
        if (!definition.enumValues.isEmpty()) tooltip += QStringLiteral("; %1").arg(definition.enumValues.join(QStringLiteral(", ")));
        if (!definition.editorHint.isEmpty()) tooltip += QStringLiteral("\n") + definition.editorHint;
        label->setToolTip(tooltip);

        if (definition.type == FdsSchemaValueType::Boolean) {
            auto* editor = new QCheckBox(pages[definition.category].scrollContents);
            const QString upper = initial.toUpper();
            editor->setChecked(upper == QStringLiteral("T") || upper == QStringLiteral("TRUE") || upper == QStringLiteral(".TRUE."));
            editor->setObjectName(QStringLiteral("Schema_%1").arg(definition.name));
            connect(editor, &QCheckBox::toggled, this, [this, definition, generation](bool checked) {
                if (generation != m_editorGeneration) return;
                emit parameterEdited(definition.name, static_cast<int>(FcFdsParameterKind::Raw),
                                     checked ? QStringLiteral(".TRUE.") : QStringLiteral(".FALSE."), {});
            });
            form->addRow(label, editor);
        } else if (definition.type == FdsSchemaValueType::Enumeration) {
            auto* editor = new QComboBox(pages[definition.category].scrollContents);
            editor->setEditable(true); editor->addItems(definition.enumValues); editor->setCurrentText(initial);
            editor->setObjectName(QStringLiteral("Schema_%1").arg(definition.name));
            connect(editor, &QComboBox::currentTextChanged, this, [this, definition, generation](const QString& value) {
                if (generation != m_editorGeneration) return;
                emit parameterEdited(definition.name, static_cast<int>(FcFdsParameterKind::String), value, {});
            });
            form->addRow(label, editor);
        } else if (definition.type == FdsSchemaValueType::Integer) {
            auto* editor = new QSpinBox(pages[definition.category].scrollContents);
            editor->setRange(definition.minimum ? static_cast<int>(std::ceil(*definition.minimum)) : -1000000000,
                             definition.maximum ? static_cast<int>(std::floor(*definition.maximum)) : 1000000000);
            bool ok = false; const int value = initial.toInt(&ok); editor->setValue(ok ? value : 0);
            editor->setObjectName(QStringLiteral("Schema_%1").arg(definition.name));
            connect(editor, &QSpinBox::valueChanged, this, [this, definition, generation](int value) {
                if (generation != m_editorGeneration) return;
                emit parameterEdited(definition.name, static_cast<int>(FcFdsParameterKind::Raw), QString::number(value), {});
            });
            form->addRow(label, editor);
        } else if (definition.type == FdsSchemaValueType::Real) {
            auto* editor = new QDoubleSpinBox(pages[definition.category].scrollContents);
            editor->setDecimals(8);
            editor->setRange(definition.minimum.value_or(-1.0e12), definition.maximum.value_or(1.0e12));
            bool ok = false; const double value = initial.toDouble(&ok); editor->setValue(ok ? value : 0.0);
            if (!definition.unit.isEmpty()) editor->setSuffix(QStringLiteral(" %1").arg(definition.unit));
            editor->setObjectName(QStringLiteral("Schema_%1").arg(definition.name));
            connect(editor, &QDoubleSpinBox::valueChanged, this, [this, definition, generation](double value) {
                if (generation != m_editorGeneration) return;
                emit parameterEdited(definition.name, static_cast<int>(FcFdsParameterKind::Raw), QString::number(value, 'g', 15), {});
            });
            form->addRow(label, editor);
        } else if (definition.type == FdsSchemaValueType::ObjectReference) {
            auto* container = new QWidget(pages[definition.category].scrollContents);
            auto* row = new QHBoxLayout(container); row->setContentsMargins(0, 0, 0, 0);
            auto* value = new QLabel(container); value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setObjectName(QStringLiteral("SchemaReferenceValue_%1").arg(definition.name));
            auto selected = std::make_shared<QStringList>(currentReferences(definition.name, parameters));
            // Literal built-ins such as OPEN have a value but no project UUID.
            value->setText(selected->isEmpty()
                               ? (current.isEmpty() ? trText(QStringLiteral("None")) : current)
                               : selected->join(QStringLiteral(", ")));
            auto* choose = new QPushButton(trText(QStringLiteral("Choose...")), container);
            choose->setObjectName(QStringLiteral("SchemaChoose_%1").arg(definition.name));
            row->addWidget(value, 1); row->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, definition, value, selected, generation]() {
                if (generation != m_editorGeneration) return;
                chooseReferences(definition, value, *selected, generation);
            });
            form->addRow(label, container);
        } else if ((definition.type == FdsSchemaValueType::RealArray ||
                    definition.type == FdsSchemaValueType::IntegerArray) &&
                   definition.arrayLength > 0) {
            auto* container = new QWidget(pages[definition.category].scrollContents);
            auto* row = new QHBoxLayout(container);
            row->setContentsMargins(0, 0, 0, 0);
            const QStringList initialValues = initial.split(QLatin1Char(','));
            auto editors = std::make_shared<QVector<QDoubleSpinBox*>>();
            const QStringList componentLabels = definition.arrayLength == 3
                ? QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}
                : definition.arrayLength == 6
                    ? QStringList{trText(QStringLiteral("X min")), trText(QStringLiteral("X max")),
                                  trText(QStringLiteral("Y min")), trText(QStringLiteral("Y max")),
                                  trText(QStringLiteral("Z min")), trText(QStringLiteral("Z max"))}
                    : QStringList{};
            for (int index = 0; index < definition.arrayLength; ++index) {
                auto* spin = new QDoubleSpinBox(container);
                spin->setDecimals(definition.type == FdsSchemaValueType::IntegerArray ? 0 : 6);
                spin->setRange(definition.minimum.value_or(-1.0e12),
                               definition.maximum.value_or(1.0e12));
                if (index < initialValues.size()) spin->setValue(initialValues[index].toDouble());
                if (!definition.unit.isEmpty()) {
                    spin->setSuffix(QStringLiteral(" %1").arg(definition.unit));
                }
                spin->setObjectName(
                    QStringLiteral("Schema_%1_%2").arg(definition.name).arg(index));
                if (index < componentLabels.size()) {
                    spin->setPrefix(componentLabels[index] + QStringLiteral(": "));
                }
                editors->append(spin);
                row->addWidget(spin);
            }
            const auto emitArray = [this, definition, editors, generation]() {
                if (generation != m_editorGeneration) return;
                QStringList values;
                for (QDoubleSpinBox* spin : *editors) {
                    values.append(definition.type == FdsSchemaValueType::IntegerArray
                                      ? QString::number(qRound64(spin->value()))
                                      : QString::number(spin->value(), 'g', 15));
                }
                emit parameterEdited(definition.name,
                                     static_cast<int>(FcFdsParameterKind::Raw),
                                     values.join(QLatin1Char(',')), {});
            };
            for (QDoubleSpinBox* spin : *editors) {
                connect(spin, &QDoubleSpinBox::valueChanged, this,
                        [emitArray](double) { emitArray(); });
            }
            form->addRow(label, container);
        } else {
            auto* editor = new QLineEdit(initial, pages[definition.category].scrollContents);
            editor->setObjectName(QStringLiteral("Schema_%1").arg(definition.name));
            if (definition.arrayLength > 0) {
                editor->setPlaceholderText(trText(QStringLiteral("Comma-separated values (%1 required)")).arg(definition.arrayLength));
            }
            const int kind = definition.type == FdsSchemaValueType::String
                                 ? static_cast<int>(FcFdsParameterKind::String)
                                 : static_cast<int>(FcFdsParameterKind::Raw);
            connect(editor, &QLineEdit::editingFinished, this,
                    [this, definition, editor, kind, generation,
                     lastCommitted = initial.trimmed()]() mutable {
                if (generation != m_editorGeneration) return;
                const QString value = editor->text().trimmed();
                // Merely focusing an untouched default must not insert a new
                // optional parameter. Changed programmatic text still commits.
                if (value == lastCommitted) return;
                lastCommitted = value;
                emit parameterEdited(definition.name, kind, value, {});
            });
            form->addRow(label, editor);
        }
    }
    if (pages.isEmpty()) {
        auto* message = new QLabel(
            m_mode == FdsSchemaEditorMode::Basic
                ? trText(QStringLiteral("This record has no separate basic fields. Use Professional Parameters or the dedicated assistant."))
                : trText(QStringLiteral("All commonly used fields are on the Basic Parameters page.")),
            m_categoryTabs);
        message->setWordWrap(true);
        m_categoryTabs->addTab(message, trText(QStringLiteral("Overview")));
    }
}

void FdsSchemaEditorWidget::chooseReferences(
    const FdsParameterSchema& definition, QLabel* valueLabel,
    QStringList& selectedIds, quint64 generation)
{
    if (!m_project || !m_project->document()) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("FdsSchemaReferenceDialog"));
    dialog.setWindowTitle(trText(QStringLiteral("Choose Referenced Objects")));
    dialog.resize(620, 430);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    std::vector<FcObject::Ptr> objects;
    for (const auto& group : m_project->document()->groups()) collectObjects(group, objects);
    for (const FcObject::Ptr& object : objects) {
        if (!object || object.get() == m_currentObject) continue;
        const QString keyword = keywordForObject(object);
        if (!definition.referenceKeywords.isEmpty() &&
            !definition.referenceKeywords.contains(keyword, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 | %2 | %3 | UUID %4")
                .arg(keyword, fdsIdForObject(object), object->name(), object->id()), list);
        item->setData(Qt::UserRole, object->id());
        item->setSelected(selectedIds.contains(object->id()));
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || generation != m_editorGeneration) return;
    selectedIds.clear();
    QStringList labels;
    for (QListWidgetItem* item : list->selectedItems()) {
        selectedIds.append(item->data(Qt::UserRole).toString());
        labels.append(item->text().section(QStringLiteral(" | "), 1, 2));
    }
    valueLabel->setText(labels.isEmpty() ? trText(QStringLiteral("None"))
                                         : labels.join(QStringLiteral("; ")));
    emit parameterEdited(definition.name,
                         static_cast<int>(FcFdsParameterKind::ObjectReferences),
                         QString{}, selectedIds);
}
