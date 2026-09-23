#include "ui/PropertiesWidget.h"

#include "core/FcObject.h"
#include "core/FcFloorObject.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcIfcObject.h"
#include "geometry/FcGeometryObject.h"
#include "results/FcResultCase.h"
#include "results/FcResultFile.h"
#include "ui/UiLanguage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
QString objectTypeName(FcObjectType type)
{
    QString name;
    switch (type) {
    case FcObjectType::Group:
        name = QStringLiteral("Group"); break;
    case FcObjectType::Geometry:
        name = QStringLiteral("Geometry"); break;
    case FcObjectType::IfcModel:
        name = QStringLiteral("IFC Model"); break;
    case FcObjectType::IfcEntity:
        name = QStringLiteral("IFC Entity"); break;
    case FcObjectType::Mesh:
        name = QStringLiteral("Mesh"); break;
    case FcObjectType::MeshMultiplier:
        name = QStringLiteral("Mesh Multiplier"); break;
    case FcObjectType::SimulationParameter:
        name = QStringLiteral("Simulation Parameter"); break;
    case FcObjectType::Species:
        name = QStringLiteral("Species"); break;
    case FcObjectType::Obstruction:
        name = QStringLiteral("Obstruction"); break;
    case FcObjectType::Vent:
        name = QStringLiteral("Vent"); break;
    case FcObjectType::Material:
        name = QStringLiteral("Material"); break;
    case FcObjectType::Surface:
        name = QStringLiteral("Surface"); break;
    case FcObjectType::Reaction:
        name = QStringLiteral("Reaction"); break;
    case FcObjectType::Particle:
        name = QStringLiteral("Particle"); break;
    case FcObjectType::Property:
        name = QStringLiteral("Property"); break;
    case FcObjectType::Table:
        name = QStringLiteral("Table"); break;
    case FcObjectType::Ramp:
        name = QStringLiteral("Ramp"); break;
    case FcObjectType::Device:
        name = QStringLiteral("Device"); break;
    case FcObjectType::Control:
        name = QStringLiteral("Control"); break;
    case FcObjectType::HVAC:
        name = QStringLiteral("HVAC"); break;
    case FcObjectType::InitialCondition:
        name = QStringLiteral("Initial Condition"); break;
    case FcObjectType::Output:
        name = QStringLiteral("Output"); break;
    case FcObjectType::Result:
        name = QStringLiteral("Result"); break;
    case FcObjectType::ResultCase:
        name = QStringLiteral("FDS Result Case"); break;
    case FcObjectType::ResultFile:
        name = QStringLiteral("FDS Result File"); break;
    case FcObjectType::Folder:
        name = QStringLiteral("Folder"); break;
    case FcObjectType::Floor:
        name = QStringLiteral("Floor"); break;
    case FcObjectType::Unknown:
    default:
        name = QStringLiteral("Unknown"); break;
    }
    return UiLanguageManager::text(name);
}

QString t(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

QString fdsNumber(double value)
{
    return QLocale::c().toString(value, 'g', 15);
}

QString boundsText(const FcFdsBounds& value)
{
    return QStringLiteral("%1, %2, %3, %4, %5, %6")
        .arg(fdsNumber(value.xMin),
             fdsNumber(value.xMax),
             fdsNumber(value.yMin),
             fdsNumber(value.yMax),
             fdsNumber(value.zMin),
             fdsNumber(value.zMax));
}
}

PropertiesWidget::PropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setAlignment(Qt::AlignTop);

    m_emptyLabel = new QLabel(t("No object selected"), this);
    layout->addWidget(m_emptyLabel);

    m_detailsWidget = new QWidget(this);
    m_form = new QFormLayout(m_detailsWidget);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("PropertiesScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidget(m_detailsWidget);
    layout->addWidget(m_scrollArea, 1);

    clear();
}

void PropertiesWidget::clear()
{
    m_emptyLabel->setText(t("No object selected"));
    m_emptyLabel->show();
    m_scrollArea->hide();
}

void PropertiesWidget::showProject(const FcProject* project)
{
    if (!project) {
        clear();
        return;
    }

    setDetails({{t("Name:"), project->name()},
                {t("Type:"), t("Project")},
                {QStringLiteral("CHID:"), project->chid()},
                {t("End Time:"), fdsNumber(project->endTime()) + QStringLiteral(" s")},
                {t("FDS Version / Schema:"), project->fdsVersion()},
                {t("Display Unit:"), project->displayUnitSymbol()},
                {t("Modified:"), project->isModified() ? t("Yes") : t("No")}});
    m_emptyLabel->hide();
    m_scrollArea->show();
}

void PropertiesWidget::showObject(const FcObject* object)
{
    if (!object) {
        clear();
        return;
    }

    QList<QPair<QString, QString>> rows = {
        {t("Name:"), object->name()},
        {t("Type:"), objectTypeName(object->type())},
        {QStringLiteral("UUID:"), object->id()},
    };
    rows.append({t("Visible:"), object->isVisible() ? t("Yes") : t("No")});
    rows.append({t("Locked:"), object->isLocked() ? t("Yes") : t("No")});
    if (!object->floorName().isEmpty()) rows.append({t("Floor:"), object->floorName()});
    if (!object->tags().isEmpty()) rows.append({t("Tags:"), object->tags().join(QStringLiteral(", "))});
    if (const auto* floor = dynamic_cast<const FcFloorObject*>(object)) {
        const FcFloorClipRange& clip = floor->clippingRange();
        rows.append({t("Base Elevation:"), fdsNumber(floor->baseElevation()) + QStringLiteral(" m")});
        rows.append({t("Default Storey Height:"), fdsNumber(floor->defaultStoreyHeight()) + QStringLiteral(" m")});
        rows.append({t("Default Slab Thickness:"), fdsNumber(floor->defaultSlabThickness()) + QStringLiteral(" m")});
        rows.append({t("Default Wall Height:"), fdsNumber(floor->defaultWallHeight()) + QStringLiteral(" m")});
        rows.append({t("2D Background:"), floor->backgroundImagePath().isEmpty()
                                           ? t("None") : floor->backgroundImagePath()});
        rows.append({t("Display Clipping:"), floor->clippingEnabled() ? t("Enabled")
                                                                       : t("Disabled")});
        rows.append({t("Clipping Range:"),
                     QStringLiteral("X[%1, %2]  Y[%3, %4]  Z[%5, %6] m")
                         .arg(fdsNumber(clip.xMin), fdsNumber(clip.xMax),
                              fdsNumber(clip.yMin), fdsNumber(clip.yMax),
                              fdsNumber(clip.zMin), fdsNumber(clip.zMax))});
    } else if (const auto* fdsObject = dynamic_cast<const FcFdsObject*>(object)) {
        rows.append({t("FDS ID:"), fdsObject->fdsId()});
    }
    if (const auto* namelist = dynamic_cast<const FcFdsNamelist*>(object)) {
        rows.append({t("Namelist:"), namelist->keyword()});
        rows.append({t("Source Order:"), QString::number(namelist->sequenceIndex())});
        for (const FcFdsParameter& parameter : namelist->parameters()) {
            QString value = parameter.value;
            if (parameter.kind == FcFdsParameterKind::ObjectReferences) {
                value += QStringLiteral("\nUUID -> ") +
                         parameter.targetObjectIds.join(QStringLiteral(", "));
            }
            rows.append({parameter.key + QLatin1Char(':'), value});
        }
    } else if (const auto* ifcObject = dynamic_cast<const FcIfcObject*>(object)) {
        rows.append({t("IFC Class:"), ifcObject->ifcClass()});
        rows.append({t("Global ID:"), ifcObject->globalId()});
        if (!ifcObject->schema().isEmpty()) {
            rows.append({t("Schema:"), ifcObject->schema()});
        }
        rows.append({t("FDS Conversion:"), ifcObject->fdsConversionRoute()});
        const QString appearanceSource = ifcObject->hasSourceAppearance()
            ? (ifcObject->hasFallbackAppearance()
                ? t("IFC source colors and type fallback colors")
                : t("IFC source colors (converted)"))
            : t("IFC type fallback colors (no source colors)");
        rows.append({t("Display appearance:"), appearanceSource});
        rows.append({t("Opacity:"), QString::number(ifcObject->appearance().alpha, 'g', 4)});
        if (object->type() == FcObjectType::IfcModel &&
            !ifcObject->sourceFile().isEmpty()) {
            rows.append({t("Source:"), ifcObject->sourceFile()});
        }
        if (!ifcObject->description().isEmpty()) {
            rows.append({t("Description:"), ifcObject->description()});
        }
    } else if (const auto* resultCase = dynamic_cast<const FcResultCase*>(object)) {
        rows.append({t("Result Directory:"), resultCase->resultDirectory()});
        rows.append({t("SMV File:"), resultCase->smvFilePath()});
        if (!resultCase->fdsInputFilePath().isEmpty()) {
            rows.append({t("FDS Input:"), resultCase->fdsInputFilePath()});
        }
        rows.append({t("Status:"), UiLanguageManager::text(resultStatusName(resultCase->status()))});
        rows.append({t("Result Files:"),
                     QString::number(resultCase->resultFileCount())});
        rows.append({t("Start Time:"),
                     resultCase->startTime().isEmpty()
                         ? t("Unknown")
                         : resultCase->startTime() + QStringLiteral(" s")});
        rows.append({t("End Time:"),
                     resultCase->endTime().isEmpty()
                         ? t("Unknown")
                         : resultCase->endTime() + QStringLiteral(" s")});
        rows.append({t("Warnings:"),
                     QString::number(resultCase->warningCount())});
        rows.append({t("Last Scan:"),
                     resultCase->lastScanTime().toString(Qt::ISODate)});
    } else if (const auto* resultFile = dynamic_cast<const FcResultFile*>(object)) {
        rows.append({t("Result Type:"),
                     UiLanguageManager::text(resultFileTypeName(resultFile->fileType()))});
        rows.append({t("File Path:"), resultFile->filePath()});
        rows.append({t("File Size:"),
                     QStringLiteral("%1 bytes").arg(resultFile->fileSize())});
        rows.append({t("File Exists:"), resultFile->exists() ? t("Yes") : t("No")});
        rows.append({t("Last Modified:"),
                     resultFile->lastModified().isValid()
                         ? resultFile->lastModified().toString(Qt::ISODate)
                         : t("Unknown")});
        if (resultFile->columnCount() > 0) {
            rows.append({t("Columns:"), QString::number(resultFile->columnCount())});
            rows.append({t("Data Rows:"), QString::number(resultFile->dataRowCount())});
            rows.append({t("Time Range:"),
                         resultFile->startTime() + QStringLiteral(" - ") +
                             resultFile->endTime() + QStringLiteral(" s")});
            rows.append({t("Column Names:"),
                         resultFile->columnNames().join(QStringLiteral(", "))});
        }
    } else if (const auto* mesh = dynamic_cast<const FcFdsMesh*>(object)) {
        const auto& cells = mesh->cells();
        rows.append({t("Cells:"), QStringLiteral("%1, %2, %3").arg(cells[0]).arg(cells[1]).arg(cells[2])});
        rows.append({QStringLiteral("XB:"), boundsText(mesh->bounds())});
    } else if (const auto* reaction = dynamic_cast<const FcFdsReaction*>(object)) {
        rows.append({t("Fuel:"), reaction->fuel()});
        rows.append({t("Soot Yield:"), fdsNumber(reaction->sootYield())});
    } else if (const auto* surface = dynamic_cast<const FcFdsSurface*>(object)) {
        rows.append({QStringLiteral("HRRPUA:"),
                     fdsNumber(surface->heatReleaseRatePerArea()) + QStringLiteral(" kW/m²")});
        rows.append({t("Color:"), surface->color()});
    } else if (const auto* obstruction = dynamic_cast<const FcFdsObstruction*>(object)) {
        rows.append({QStringLiteral("XB:"), boundsText(obstruction->bounds())});
        rows.append({t("Surface ID:"), obstruction->surfaceId()});
    } else if (const auto* vent = dynamic_cast<const FcFdsVent*>(object)) {
        rows.append({QStringLiteral("XB:"), boundsText(vent->bounds())});
        rows.append({t("Surface ID:"), vent->surfaceId()});
    } else if (const auto* output = dynamic_cast<const FcFdsOutput*>(object)) {
        rows.append({t("Output Kind:"),
                     output->kind() == FcFdsOutputKind::Boundary ? t("Boundary") : t("Slice")});
        rows.append({t("Quantity:"), output->quantity()});
        if (output->kind() == FcFdsOutputKind::Slice) {
            const QString axis = output->planeAxis() == FcFdsPlaneAxis::X
                                     ? QStringLiteral("X")
                                     : output->planeAxis() == FcFdsPlaneAxis::Y
                                           ? QStringLiteral("Y")
                                           : QStringLiteral("Z");
            rows.append({t("Plane:"), axis + QStringLiteral(" = ") + fdsNumber(output->planeValue())});
            rows.append({t("Vector:"), output->vectorOutput() ? t("Yes") : t("No")});
        }
    } else if (const auto* geometry = dynamic_cast<const FcGeometryObject*>(object)) {
        // Keep the legacy ID row for existing projects/tests while UUID remains
        // the only model-tree association key.
        rows.append({QStringLiteral("ID:"), object->id()});
        rows.append({t("Geometry Kind:"),
                     UiLanguageManager::text(fcGeometryKindName(geometry->geometryKind()))});
        if (!geometry->hostObjectId().isEmpty()) {
            rows.append({t("Host UUID:"), geometry->hostObjectId()});
        }
        if (!geometry->controlObjectId().isEmpty()) {
            rows.append({t("Control UUID:"), geometry->controlObjectId()});
        }
        rows.append({t("Dynamic Opening:"), geometry->isDynamicOpening() ? t("Yes") : t("No")});
        if (geometry->geometryKind() == FcGeometryKind::BackgroundImage) {
            rows.append({t("Source:"), geometry->geometryParameters()
                                            .value(QStringLiteral("resourcePath")).toString()});
            rows.append({t("Plane:"), geometry->geometryParameters()
                                           .value(QStringLiteral("plane")).toString()});
            rows.append({t("Opacity:"), geometry->geometryParameters()
                                             .value(QStringLiteral("opacity")).toString()});
        }
        if (!geometry->defaultSurfaceId().isEmpty()) {
            rows.append({t("Default Surface UUID:"), geometry->defaultSurfaceId()});
        }
        for (auto iterator = geometry->faceSurfaceIds().cbegin();
             iterator != geometry->faceSurfaceIds().cend(); ++iterator) {
            rows.append({t("Surface %1 UUID:").arg(iterator.key()), iterator.value()});
        }
        for (auto iterator = geometry->geometryParameters().cbegin();
             iterator != geometry->geometryParameters().cend(); ++iterator) {
            if (iterator.value().canConvert<double>() &&
                iterator.value().typeId() != QMetaType::QVariantList) {
                rows.append({iterator.key() + QLatin1Char(':'), iterator.value().toString()});
            }
        }
    } else {
        rows.append({t("Objects:"),
                     QString::number(object->children().size())});
    }
    setDetails(rows);
    m_emptyLabel->hide();
    m_scrollArea->show();
}

void PropertiesWidget::setDetails(const QList<QPair<QString, QString>>& rows)
{
    while (m_form->rowCount() > 0) {
        m_form->removeRow(0);
    }
    for (const auto& row : rows) {
        auto* value = new QLabel(row.second, m_detailsWidget);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        m_form->addRow(row.first, value);
    }
}
