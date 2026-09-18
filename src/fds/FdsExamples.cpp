#include "fds/FdsExamples.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"

#include <array>

namespace
{
using Namelist = std::shared_ptr<FcFdsNamelist>;

FcFdsParameter raw(const char* key, const QString& value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::Raw, value, {}};
}

FcFdsParameter text(const char* key, const QString& value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::String, value, {}};
}

FcFdsParameter reference(const char* key, const Namelist& target)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::ObjectReferences,
            {}, {target->id()}};
}

FcFdsParameter references(const char* key,
                           std::initializer_list<Namelist> targets)
{
    QStringList ids;
    for (const Namelist& target : targets) {
        ids.append(target->id());
    }
    return {QString::fromLatin1(key), FcFdsParameterKind::ObjectReferences,
            {}, ids};
}

Namelist namelist(const QString& name,
                  FcObjectType type,
                  const char* keyword,
                  const QString& fdsId,
                  int sequence,
                  std::initializer_list<FcFdsParameter> parameters)
{
    auto object = std::make_shared<FcFdsNamelist>(
        name, type, QString::fromLatin1(keyword), fdsId, sequence);
    object->setParameters(std::vector<FcFdsParameter>(parameters));
    return object;
}
}

namespace FdsExamples
{
std::unique_ptr<FcProject> createSimpleTestProject()
{
    auto project = std::make_unique<FcProject>(QStringLiteral("Simple demonstration case."));
    project->setChid(QStringLiteral("simple_test"));
    project->setEndTime(60.0);

    FcDocument* document = project->document();
    document->meshesGroup()->addChild(std::make_shared<FcFdsMesh>(
        QStringLiteral("Domain Mesh"),
        QStringLiteral("MESH_1"),
        std::array<int, 3>{36, 24, 24},
        FcFdsBounds{0.0, 3.6, 0.0, 2.4, 0.0, 2.4}));
    document->reactionsGroup()->addChild(std::make_shared<FcFdsReaction>(
        QStringLiteral("Propane Reaction"),
        QStringLiteral("PROPANE_REACTION"),
        QStringLiteral("PROPANE"),
        0.01));
    document->surfacesGroup()->addChild(std::make_shared<FcFdsSurface>(
        QStringLiteral("Burner Surface"),
        QStringLiteral("BURNER"),
        1000.0,
        QStringLiteral("RED")));
    document->geometryGroup()->addChild(std::make_shared<FcFdsObstruction>(
        QStringLiteral("Burner Base"),
        QStringLiteral("BURNER_BASE"),
        FcFdsBounds{0.0, 0.4, 1.0, 1.4, 0.0, 0.2}));
    document->ventsGroup()->addChild(std::make_shared<FcFdsVent>(
        QStringLiteral("Burner Vent"),
        QStringLiteral("BURNER_VENT"),
        FcFdsBounds{0.0, 0.4, 1.0, 1.4, 0.2, 0.2},
        QStringLiteral("BURNER")));
    document->ventsGroup()->addChild(std::make_shared<FcFdsVent>(
        QStringLiteral("Open Boundary"),
        QStringLiteral("OPENING"),
        FcFdsBounds{3.6, 3.6, 0.8, 1.6, 0.0, 2.0},
        QStringLiteral("OPEN")));
    document->outputsGroup()->addChild(FcFdsOutput::boundary(
        QStringLiteral("Gauge Heat Flux"),
        QStringLiteral("BNDF_GAUGE_HEAT_FLUX"),
        QStringLiteral("GAUGE HEAT FLUX")));
    document->outputsGroup()->addChild(FcFdsOutput::slice(
        QStringLiteral("Temperature Slice"),
        QStringLiteral("SLCF_TEMPERATURE_Y"),
        FcFdsPlaneAxis::Y,
        1.2,
        QStringLiteral("TEMPERATURE"),
        true));

    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createActivateVentsProject()
{
    auto project = std::make_unique<FcProject>(
        QStringLiteral("Test of VENT activation/deactivation"));
    project->setChid(QStringLiteral("activate_vents"));
    project->setEndTime(20.0);
    FcDocument* document = project->document();

    const Namelist mesh = namelist(
        QStringLiteral("Activation domain"), FcObjectType::Mesh, "MESH", {}, 0,
        {raw("IJK", QStringLiteral("21,10,10")),
         raw("XB", QStringLiteral("0.0,2.1,0.0,1.0,0.0,1.0"))});
    document->meshesGroup()->addChild(mesh);
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Time step"), FcObjectType::SimulationParameter,
        "TIME", {}, 1, {raw("DT", QStringLiteral("0.05"))}));

    const std::array<QString, 7> colors = {
        QStringLiteral("PURPLE"), QStringLiteral("RED"),
        QStringLiteral("ORANGE"), QStringLiteral("YELLOW"),
        QStringLiteral("GREEN"), QStringLiteral("CYAN"),
        QStringLiteral("BLUE")};
    std::array<Namelist, 7> particles;
    for (int index = 0; index < 7; ++index) {
        particles[static_cast<std::size_t>(index)] = namelist(
            QStringLiteral("Tracer %1").arg(index + 1), FcObjectType::Particle,
            "PART", QStringLiteral("TRACER %1").arg(index + 1), 9 + index,
            {raw("MASSLESS", QStringLiteral(".TRUE.")),
             text("COLOR", colors[static_cast<std::size_t>(index)])});
        document->particlesGroup()->addChild(
            particles[static_cast<std::size_t>(index)]);
    }

    struct DeviceDefinition
    {
        const char* id;
        const char* setpoint;
    };
    const std::array<DeviceDefinition, 6> deviceDefinitions = {{
        {"clock 1", ""}, {"timer 2", "5.0"}, {"timer 5", "5.0"},
        {"timer 6", "6.0"}, {"timer 7", "7.0"}, {"timer 7b", "11.0"}}};
    std::array<Namelist, 6> devices;
    for (int index = 0; index < 6; ++index) {
        const DeviceDefinition& definition =
            deviceDefinitions[static_cast<std::size_t>(index)];
        std::vector<FcFdsParameter> parameters = {
            raw("XYZ", QStringLiteral("0.1,0.1,0.1")),
            text("QUANTITY", QStringLiteral("TIME"))};
        if (*definition.setpoint != '\0') {
            parameters.push_back(raw("SETPOINT", QString::fromLatin1(definition.setpoint)));
        }
        devices[static_cast<std::size_t>(index)] =
            std::make_shared<FcFdsNamelist>(
                QString::fromLatin1(definition.id), FcObjectType::Device,
                QStringLiteral("DEVC"), QString::fromLatin1(definition.id),
                23 + index);
        devices[static_cast<std::size_t>(index)]->setParameters(parameters);
        document->devicesGroup()->addChild(devices[static_cast<std::size_t>(index)]);
    }

    std::array<Namelist, 7> ramps;
    const std::array<const char*, 7> rampTimes = {
        "0.00", "2.99", "3.01", "5.99", "6.01", "11.99", "12.01"};
    const std::array<const char*, 7> rampValues = {
        "-1.", "-1.", "1.", "1.", "-1.", "-1.", "1."};
    for (int index = 0; index < 7; ++index) {
        ramps[static_cast<std::size_t>(index)] = namelist(
            QStringLiteral("Ramp 1 point %1").arg(index + 1), FcObjectType::Ramp,
            "RAMP", QStringLiteral("ramp 1"), 30 + index,
            {raw("T", QString::fromLatin1(rampTimes[static_cast<std::size_t>(index)])),
             raw("F", QString::fromLatin1(rampValues[static_cast<std::size_t>(index)]))});
        document->controlsGroup()->addChild(ramps[static_cast<std::size_t>(index)]);
    }

    const Namelist controller1 = namelist(
        QStringLiteral("Controller 1"), FcObjectType::Control, "CTRL",
        QStringLiteral("controller 1"), 29,
        {text("FUNCTION_TYPE", QStringLiteral("CUSTOM")),
         reference("INPUT_ID", devices[0]), reference("RAMP_ID", ramps[0])});
    const Namelist controller3 = namelist(
        QStringLiteral("Controller 3"), FcObjectType::Control, "CTRL",
        QStringLiteral("controller 3"), 37,
        {text("FUNCTION_TYPE", QStringLiteral("TIME_DELAY")),
         reference("INPUT_ID", devices[1]), raw("DELAY", QStringLiteral("3."))});
    const Namelist controller4 = namelist(
        QStringLiteral("Controller 4"), FcObjectType::Control, "CTRL",
        QStringLiteral("controller 4"), 38,
        {text("FUNCTION_TYPE", QStringLiteral("ALL")),
         references("INPUT_ID", {controller1, controller3})});
    document->controlsGroup()->addChild(controller1);
    document->controlsGroup()->addChild(controller3);
    document->controlsGroup()->addChild(controller4);

    particles[6]->addReferenceParameter(
        QStringLiteral("DEVC_ID"), {devices[5]->id()});

    std::array<Namelist, 7> surfaces;
    for (int index = 0; index < 7; ++index) {
        surfaces[static_cast<std::size_t>(index)] = namelist(
            QStringLiteral("Blower %1").arg(index + 1), FcObjectType::Surface,
            "SURF", QStringLiteral("BLOW %1").arg(index + 1), 2 + index,
            {raw("VEL", QStringLiteral("-0.2")),
             text("COLOR", colors[static_cast<std::size_t>(index)]),
             reference("PART_ID", particles[static_cast<std::size_t>(index)])});
        document->surfacesGroup()->addChild(surfaces[static_cast<std::size_t>(index)]);
    }

    const std::array<Namelist, 7> activators = {
        controller1, devices[1], controller3, controller4,
        devices[2], devices[3], devices[4]};
    for (int index = 0; index < 7; ++index) {
        const double xMin = 0.10 + 0.30 * index;
        const QString bounds = QStringLiteral("%1,%2,0.40,0.60,0.00,0.00")
                                   .arg(xMin, 0, 'f', 2)
                                   .arg(xMin + 0.10, 0, 'f', 2);
        const bool usesControl = index == 0 || index == 2 || index == 3;
        const char* activationKey = usesControl ? "CTRL_ID" : "DEVC_ID";
        document->ventsGroup()->addChild(namelist(
            QStringLiteral("Activated vent %1").arg(index + 1), FcObjectType::Vent,
            "VENT", {}, 16 + index,
            {raw("XB", bounds),
             reference("SURF_ID", surfaces[static_cast<std::size_t>(index)]),
             text("COLOR", colors[static_cast<std::size_t>(index)]),
             reference(activationKey, activators[static_cast<std::size_t>(index)])}));
    }

    const std::array<const char*, 5> meshBoundaries = {
        "XMIN", "XMAX", "YMIN", "YMAX", "ZMAX"};
    for (int index = 0; index < 5; ++index) {
        document->ventsGroup()->addChild(namelist(
            QStringLiteral("Open %1 boundary")
                .arg(QString::fromLatin1(meshBoundaries[static_cast<std::size_t>(index)])),
            FcObjectType::Vent, "VENT", {}, 39 + index,
            {text("MB", QString::fromLatin1(
                            meshBoundaries[static_cast<std::size_t>(index)])),
             text("SURF_ID", QStringLiteral("OPEN"))}));
    }

    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createBucketTest2Project()
{
    auto project = std::make_unique<FcProject>(
        QStringLiteral("Customized sprinkler test case"));
    project->setChid(QStringLiteral("bucket_test_2"));
    project->setEndTime(15.0);
    FcDocument* document = project->document();

    const Namelist mesh = namelist(
        QStringLiteral("Sprinkler test domain"), FcObjectType::Mesh,
        "MESH", {}, 0,
        {raw("IJK", QStringLiteral("50,50,25")),
         raw("XB", QStringLiteral("-5.0,5.0,-5.0,5.0,0.0,5.0"))});
    document->meshesGroup()->addChild(mesh);

    const Namelist waterVapor = namelist(
        QStringLiteral("Water vapor"), FcObjectType::Species,
        "SPEC", QStringLiteral("WATER VAPOR"), 1, {});
    document->speciesGroup()->addChild(waterVapor);

    const Namelist waterDrops = namelist(
        QStringLiteral("Water drops"), FcObjectType::Particle,
        "PART", QStringLiteral("water drops"), 2,
        {reference("SPEC_ID", waterVapor),
         text("QUANTITIES(1)", QStringLiteral("PARTICLE DIAMETER")),
         raw("DIAMETER", QStringLiteral("1750."))});
    document->particlesGroup()->addChild(waterDrops);

    // FDS represents one spray distribution as repeated TABL records with the
    // same FDS ID.  FireCAE keeps both records as independent UUID objects.
    const Namelist tableLower = namelist(
        QStringLiteral("TABLE1 lower hemisphere"), FcObjectType::Table,
        "TABL", QStringLiteral("TABLE1"), 4,
        {raw("TABLE_DATA", QStringLiteral("30,31,  0,  1,5,0.2"))});
    const Namelist tableUpper = namelist(
        QStringLiteral("TABLE1 upper hemisphere"), FcObjectType::Table,
        "TABL", QStringLiteral("TABLE1"), 5,
        {raw("TABLE_DATA", QStringLiteral("30,31,179,180,5,0.8"))});

    const Namelist sprinklerProperty = namelist(
        QStringLiteral("K-11 sprinkler property"), FcObjectType::Property,
        "PROP", QStringLiteral("K-11"), 3,
        {text("QUANTITY", QStringLiteral("SPRINKLER LINK TEMPERATURE")),
         raw("PARTICLE_VELOCITY", QStringLiteral("5.")),
         reference("PART_ID", waterDrops),
         raw("FLOW_RATE", QStringLiteral("60.")),
         reference("SPRAY_PATTERN_TABLE", tableLower),
         text("SMOKEVIEW_ID", QStringLiteral("sprinkler_upright")),
         raw("PARTICLES_PER_SECOND", QStringLiteral("10000"))});
    document->particlesGroup()->addChild(sprinklerProperty);
    document->controlsGroup()->addChild(tableLower);
    document->controlsGroup()->addChild(tableUpper);

    const Namelist sprinklerDevice = namelist(
        QStringLiteral("Spr_1"), FcObjectType::Device,
        "DEVC", QStringLiteral("Spr_1"), 6,
        {raw("XYZ", QStringLiteral("0.0,0.0,4.9")),
         reference("PROP_ID", sprinklerProperty),
         text("QUANTITY", QStringLiteral("TIME")),
         raw("SETPOINT", QStringLiteral("5.")),
         raw("INITIAL_STATE", QStringLiteral(".TRUE."))});
    document->devicesGroup()->addChild(sprinklerDevice);

    const std::array<const char*, 4> openBoundaries = {
        "XMIN", "XMAX", "YMIN", "YMAX"};
    for (int index = 0; index < 4; ++index) {
        const QString boundary = QString::fromLatin1(
            openBoundaries[static_cast<std::size_t>(index)]);
        document->ventsGroup()->addChild(namelist(
            QStringLiteral("Open %1 boundary").arg(boundary),
            FcObjectType::Vent, "VENT", {}, 7 + index,
            {text("MB", boundary), text("SURF_ID", QStringLiteral("OPEN"))}));
    }

    const Namelist accumulatedMass = namelist(
        QStringLiteral("Accumulated water mass"), FcObjectType::Device,
        "DEVC", QStringLiteral("Mass"), 11,
        {text("QUANTITY", QStringLiteral("AMPUA")),
         reference("PART_ID", waterDrops),
         text("SPATIAL_STATISTIC", QStringLiteral("SURFACE INTEGRAL")),
         raw("XB", QStringLiteral("-5,5,-5,5,0,0"))});
    document->devicesGroup()->addChild(accumulatedMass);

    document->outputsGroup()->addChild(namelist(
        QStringLiteral("Accumulated mass per unit area"), FcObjectType::Output,
        "BNDF", {}, 12,
        {text("QUANTITY", QStringLiteral("AMPUA")),
         reference("PART_ID", waterDrops)}));

    project->setModified(false);
    return project;
}

static std::unique_ptr<FcProject> createCouchProjectVariant(
    const QString& projectName,
    const QString& chid,
    double endTime,
    const QString& frameCount,
    const QString& hrrInterval,
    const QString& deviceInterval)
{
    auto project = std::make_unique<FcProject>(projectName);
    project->setChid(chid);
    project->setEndTime(endTime);
    FcDocument* document = project->document();

    const Namelist multiplier = namelist(
        QStringLiteral("2 x 2 x 2 mesh array"), FcObjectType::MeshMultiplier,
        "MULT", QStringLiteral("mesh"), 1,
        {raw("DX", QStringLiteral("2.5")), raw("DY", QStringLiteral("2.5")),
         raw("DZ", QStringLiteral("1.2")), raw("I_UPPER", QStringLiteral("1")),
         raw("J_UPPER", QStringLiteral("1")), raw("K_UPPER", QStringLiteral("1"))});
    const Namelist mesh = namelist(
        QStringLiteral("Couch domain mesh"), FcObjectType::Mesh,
        "MESH", {}, 0,
        {raw("IJK", QStringLiteral("25,25,12")),
         raw("XB", QStringLiteral("0.0,2.5,0.0,2.5,0.0,1.2")),
         reference("MULT_ID", multiplier)});
    document->meshesGroup()->addChild(mesh);
    document->meshesGroup()->addChild(multiplier);

    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Output intervals"), FcObjectType::SimulationParameter,
        "DUMP", {}, 2,
        {raw("NFRAMES", frameCount), raw("DT_HRR", hrrInterval),
         raw("DT_DEVC", deviceInterval)}));

    const Namelist polyurethane = namelist(
        QStringLiteral("Polyurethane"), FcObjectType::Species,
        "SPEC", QStringLiteral("POLYURETHANE"), 3,
        {text("FORMULA", QStringLiteral("C6.3H7.1N1.0O2.1"))});
    document->speciesGroup()->addChild(polyurethane);
    document->reactionsGroup()->addChild(namelist(
        QStringLiteral("Polyurethane reaction"), FcObjectType::Reaction,
        "REAC", {}, 4,
        {reference("FUEL", polyurethane), raw("SOOT_YIELD", QStringLiteral("0.01")),
         raw("HEAT_OF_COMBUSTION", QStringLiteral("22700."))}));

    const Namelist fabric = namelist(
        QStringLiteral("Fabric"), FcObjectType::Material,
        "MATL", QStringLiteral("FABRIC"), 5,
        {text("FYI", QStringLiteral("Properties completely fabricated")),
         raw("SPECIFIC_HEAT", QStringLiteral("1.0")),
         raw("CONDUCTIVITY", QStringLiteral("0.5")),
         raw("DENSITY", QStringLiteral("50.")), raw("NU_SPEC", QStringLiteral("1.")),
         reference("SPEC_ID", polyurethane),
         raw("REFERENCE_TEMPERATURE", QStringLiteral("250.")),
         raw("HEAT_OF_REACTION", QStringLiteral("500.")),
         raw("HEAT_OF_COMBUSTION", QStringLiteral("16000."))});
    const Namelist foam = namelist(
        QStringLiteral("Foam"), FcObjectType::Material,
        "MATL", QStringLiteral("FOAM"), 6,
        {text("FYI", QStringLiteral("Properties completely fabricated")),
         raw("SPECIFIC_HEAT", QStringLiteral("1.0")),
         raw("CONDUCTIVITY", QStringLiteral("0.1")),
         raw("DENSITY", QStringLiteral("40.0")), raw("NU_SPEC", QStringLiteral("1.")),
         reference("SPEC_ID", polyurethane),
         raw("REFERENCE_TEMPERATURE", QStringLiteral("280.")),
         raw("HEAT_OF_REACTION", QStringLiteral("800.")),
         raw("HEAT_OF_COMBUSTION", QStringLiteral("22700."))});
    const Namelist gypsum = namelist(
        QStringLiteral("Gypsum plaster"), FcObjectType::Material,
        "MATL", QStringLiteral("GYPSUM PLASTER"), 7,
        {raw("CONDUCTIVITY", QStringLiteral("0.5")),
         raw("SPECIFIC_HEAT", QStringLiteral("1.0")),
         raw("DENSITY", QStringLiteral("500."))});
    document->materialsGroup()->addChild(fabric);
    document->materialsGroup()->addChild(foam);
    document->materialsGroup()->addChild(gypsum);

    const Namelist upholstery = namelist(
        QStringLiteral("Upholstery"), FcObjectType::Surface,
        "SURF", QStringLiteral("UPHOLSTERY"), 8,
        {text("FYI", QStringLiteral("Properties completely fabricated")),
         text("BACKING", QStringLiteral("VOID")),
         text("COLOR", QStringLiteral("PURPLE")),
         raw("BURN_AWAY", QStringLiteral(".TRUE.")),
         references("MATL_ID(1:2,1)", {fabric, foam}),
         raw("THICKNESS(1:2)", QStringLiteral("0.0005,0.1"))});
    const Namelist wall = namelist(
        QStringLiteral("Wall"), FcObjectType::Surface,
        "SURF", QStringLiteral("WALL"), 9,
        {raw("DEFAULT", QStringLiteral(".TRUE.")),
         raw("RGB", QStringLiteral("200,200,200")),
         reference("MATL_ID", gypsum), raw("THICKNESS", QStringLiteral("0.012"))});
    const Namelist ignitorSurface = namelist(
        QStringLiteral("Ignitor surface"), FcObjectType::Surface,
        "SURF", QStringLiteral("ignitor"), 16,
        {raw("TMP_FRONT", QStringLiteral("1000.")),
         raw("EMISSIVITY", QStringLiteral("1.")),
         text("GEOMETRY", QStringLiteral("CYLINDRICAL")),
         raw("LENGTH", QStringLiteral("0.15")), raw("RADIUS", QStringLiteral("0.01"))});
    document->surfacesGroup()->addChild(upholstery);
    document->surfacesGroup()->addChild(wall);
    document->surfacesGroup()->addChild(ignitorSurface);

    const std::array<QString, 5> obstructionBounds = {
        QStringLiteral("1.50, 3.10, 3.80, 4.60, 0.00, 0.40"),
        QStringLiteral("1.50, 3.10, 3.80, 4.60, 0.40, 0.60"),
        QStringLiteral("1.30, 1.50, 3.80, 4.60, 0.00, 0.90"),
        QStringLiteral("3.10, 3.30, 3.80, 4.60, 0.00, 0.90"),
        QStringLiteral("1.50, 3.10, 4.40, 4.60, 0.60, 1.20")};
    for (int index = 0; index < 5; ++index) {
        std::vector<FcFdsParameter> parameters = {
            raw("XB", obstructionBounds[static_cast<std::size_t>(index)])};
        if (index > 0) {
            parameters.push_back(reference("SURF_ID", upholstery));
            parameters.push_back(raw("BULK_DENSITY", QStringLiteral("40.")));
        }
        auto obstruction = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Couch obstruction %1").arg(index + 1),
            FcObjectType::Obstruction, QStringLiteral("OBST"), QString(), 10 + index);
        obstruction->setParameters(parameters);
        document->geometryGroup()->addChild(obstruction);
    }

    const Namelist ignitorParticle = namelist(
        QStringLiteral("Ignitor particle"), FcObjectType::Particle,
        "PART", QStringLiteral("ignitor particle"), 15,
        {reference("SURF_ID", ignitorSurface), raw("STATIC", QStringLiteral(".TRUE."))});
    document->particlesGroup()->addChild(ignitorParticle);
    document->initialConditionsGroup()->addChild(namelist(
        QStringLiteral("Ignitor particle region"), FcObjectType::InitialCondition,
        "INIT", {}, 17,
        {raw("XB", QStringLiteral("2.4,2.7,4.1,4.4,0.60,0.70")),
         reference("PART_ID", ignitorParticle),
         raw("N_PARTICLES_PER_CELL", QStringLiteral("1")),
         raw("CELL_CENTERED", QStringLiteral("T"))}));
    document->ventsGroup()->addChild(namelist(
        QStringLiteral("Open room boundary"), FcObjectType::Vent,
        "VENT", {}, 18,
        {raw("XB", QStringLiteral("1,4,0,0,0,2")),
         text("SURF_ID", QStringLiteral("OPEN"))}));

    const std::array<const char*, 5> boundaryQuantities = {
        "RADIATIVE HEAT FLUX", "CONVECTIVE HEAT FLUX", "NET HEAT FLUX",
        "WALL TEMPERATURE", "BURNING RATE"};
    for (int index = 0; index < 5; ++index) {
        const QString quantity = QString::fromLatin1(
            boundaryQuantities[static_cast<std::size_t>(index)]);
        document->outputsGroup()->addChild(namelist(
            quantity, FcObjectType::Output, "BNDF", {}, 19 + index,
            {text("QUANTITY", quantity)}));
    }
    document->outputsGroup()->addChild(namelist(
        QStringLiteral("Temperature vector slice"), FcObjectType::Output,
        "SLCF", {}, 24,
        {raw("PBX", QStringLiteral("2.50")),
         text("QUANTITY", QStringLiteral("TEMPERATURE")),
         raw("VECTOR", QStringLiteral(".TRUE.")),
         raw("CELL_CENTERED", QStringLiteral(".TRUE."))}));
    document->outputsGroup()->addChild(namelist(
        QStringLiteral("HRRPUV slice"), FcObjectType::Output,
        "SLCF", {}, 25,
        {raw("PBX", QStringLiteral("2.50")),
         text("QUANTITY", QStringLiteral("HRRPUV")),
         raw("CELL_CENTERED", QStringLiteral(".TRUE."))}));

    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createCouchProject()
{
    return createCouchProjectVariant(
        QStringLiteral("Single Couch Test Case"), QStringLiteral("couch"),
        600.0, QStringLiteral("3000"), QStringLiteral("5."), QStringLiteral("5."));
}

std::unique_ptr<FcProject> createCouchSmoke12sProject()
{
    return createCouchProjectVariant(
        QStringLiteral("Single Couch Tutorial - 12 s FireCAE Smoke Test"),
        QStringLiteral("couch_smoke_12s"), 12.0, QStringLiteral("60"),
        QStringLiteral("1."), QStringLiteral("1."));
}

std::unique_ptr<FcProject> createHvacAircoilProject()
{
    auto project = std::make_unique<FcProject>(QStringLiteral("Test of aircoil"));
    project->setChid(QStringLiteral("HVAC_aircoil"));
    project->setEndTime(1.0);
    FcDocument* document = project->document();

    document->meshesGroup()->addChild(namelist(
        QStringLiteral("Aircoil test domain"), FcObjectType::Mesh,
        "MESH", {}, 0,
        {raw("IJK", QStringLiteral("10,10,10")),
         raw("XB", QStringLiteral("0.,1.,0.,1.,0.,1."))}));
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Disable stratification"), FcObjectType::SimulationParameter,
        "MISC", {}, 1, {raw("STRATIFICATION", QStringLiteral("F"))}));
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Disable radiation"), FcObjectType::SimulationParameter,
        "RADI", {}, 2, {raw("RADIATION", QStringLiteral("F"))}));
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Output cadence"), FcObjectType::SimulationParameter,
        "DUMP", {}, 3, {raw("NFRAMES", QStringLiteral("20"))}));

    const std::array<const char*, 4> boundaries = {
        "XMIN", "YMIN", "XMAX", "YMAX"};
    for (int index = 0; index < 4; ++index) {
        const QString boundary = QString::fromLatin1(
            boundaries[static_cast<std::size_t>(index)]);
        document->ventsGroup()->addChild(namelist(
            QStringLiteral("Open %1 boundary").arg(boundary), FcObjectType::Vent,
            "VENT", {}, 4 + index,
            {text("MB", boundary), text("SURF_ID", QStringLiteral("OPEN"))}));
    }

    document->surfacesGroup()->addChild(namelist(
        QStringLiteral("Null surface"), FcObjectType::Surface,
        "SURF", QStringLiteral("NULL"), 8, {}));
    document->speciesGroup()->addChild(namelist(
        QStringLiteral("Background species"), FcObjectType::Species,
        "SPEC", QStringLiteral("SPECIES1"), 9,
        {raw("MW", QStringLiteral("28")), raw("SPECIFIC_HEAT", QStringLiteral("1.")),
         raw("BACKGROUND", QStringLiteral("T"))}));

    const Namelist inletVent = namelist(
        QStringLiteral("HVAC inlet vent"), FcObjectType::Vent,
        "VENT", QStringLiteral("INLET"), 10,
        {raw("XB", QStringLiteral("0.3,0.7,0.3,0.7,0.0,0.0")),
         text("SURF_ID", QStringLiteral("HVAC")),
         text("COLOR", QStringLiteral("RED"))});
    const Namelist outletVent = namelist(
        QStringLiteral("HVAC outlet vent"), FcObjectType::Vent,
        "VENT", QStringLiteral("OUTLET"), 11,
        {raw("XB", QStringLiteral("0.3,0.7,0.3,0.7,1.0,1.0")),
         text("SURF_ID", QStringLiteral("HVAC")),
         text("COLOR", QStringLiteral("GREEN"))});
    document->ventsGroup()->addChild(inletVent);
    document->ventsGroup()->addChild(outletVent);

    const Namelist inletNode = namelist(
        QStringLiteral("Inlet HVAC node"), FcObjectType::HVAC,
        "HVAC", QStringLiteral("INLET"), 12, {});
    const Namelist outletNode = namelist(
        QStringLiteral("Outlet HVAC node"), FcObjectType::HVAC,
        "HVAC", QStringLiteral("OUTLET"), 13, {});
    const Namelist aircoil = namelist(
        QStringLiteral("Aircoil"), FcObjectType::HVAC,
        "HVAC", QStringLiteral("AIRCOIL"), 15,
        {text("TYPE_ID", QStringLiteral("AIRCOIL")),
         raw("EFFICIENCY", QStringLiteral("0.5")),
         raw("COOLANT_SPECIFIC_HEAT", QStringLiteral("4.0")),
         raw("COOLANT_TEMPERATURE", QStringLiteral("100.0")),
         raw("COOLANT_MASS_FLOW", QStringLiteral("10.0"))});
    const Namelist duct = namelist(
        QStringLiteral("HVAC duct"), FcObjectType::HVAC,
        "HVAC", QStringLiteral("DUCT"), 14,
        {text("TYPE_ID", QStringLiteral("DUCT")),
         references("NODE_ID", {inletNode, outletNode}),
         raw("LENGTH", QStringLiteral("1")), raw("AREA", QStringLiteral("0.1")),
         raw("VOLUME_FLOW", QStringLiteral("1.0")),
         reference("AIRCOIL_ID", aircoil), raw("TAU_VF", QStringLiteral("0."))});
    inletNode->setParameters({
        text("TYPE_ID", QStringLiteral("NODE")), reference("VENT_ID", inletVent),
        reference("DUCT_ID", duct)});
    outletNode->setParameters({
        text("TYPE_ID", QStringLiteral("NODE")), reference("VENT_ID", outletVent),
        reference("DUCT_ID", duct)});
    document->hvacGroup()->addChild(inletNode);
    document->hvacGroup()->addChild(outletNode);
    document->hvacGroup()->addChild(duct);
    document->hvacGroup()->addChild(aircoil);

    document->devicesGroup()->addChild(namelist(
        QStringLiteral("Aircoil heat exchange"), FcObjectType::Device,
        "DEVC", QStringLiteral("FDS Q"), 16,
        {text("QUANTITY", QStringLiteral("AIRCOIL HEAT EXCHANGE")),
         reference("DUCT_ID", duct)}));
    document->devicesGroup()->addChild(namelist(
        QStringLiteral("Outlet node temperature"), FcObjectType::Device,
        "DEVC", QStringLiteral("FDS T"), 17,
        {text("QUANTITY", QStringLiteral("NODE TEMPERATURE")),
         reference("NODE_ID", outletNode)}));
    document->geometryGroup()->addChild(namelist(
        QStringLiteral("Central obstruction"), FcObjectType::Obstruction,
        "OBST", {}, 18, {raw("XB", QStringLiteral("0,1,0,1,0.4,0.6"))}));
    document->outputsGroup()->addChild(namelist(
        QStringLiteral("Temperature vector slice"), FcObjectType::Output,
        "SLCF", {}, 19,
        {raw("PBY", QStringLiteral("0.5")),
         text("QUANTITY", QStringLiteral("TEMPERATURE")),
         raw("VECTOR", QStringLiteral("T"))}));

    project->setModified(false);
    return project;
}

static std::unique_ptr<FcProject> createTunnelProjectVariant(
    const QString& projectName, const QString& chid, double endTime)
{
    auto project = std::make_unique<FcProject>(projectName);
    project->setChid(chid);
    project->setEndTime(endTime);
    FcDocument* document = project->document();

    const Namelist multiplier = namelist(
        QStringLiteral("Eight tunnel mesh sections"), FcObjectType::MeshMultiplier,
        "MULT", QStringLiteral("mesh"), 1,
        {raw("DX", QStringLiteral("16.")), raw("I_UPPER", QStringLiteral("7"))});
    document->meshesGroup()->addChild(namelist(
        QStringLiteral("Tunnel mesh"), FcObjectType::Mesh,
        "MESH", {}, 0,
        {raw("IJK", QStringLiteral("80,20,20")),
         raw("XB", QStringLiteral("0.0,16.0,-2.0,2.0,0.0,4.0")),
         reference("MULT_ID", multiplier)}));
    document->meshesGroup()->addChild(multiplier);
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Sloped gravity vector"), FcObjectType::SimulationParameter,
        "MISC", {}, 2, {raw("GVEC", QStringLiteral("-1.70,0.0,-9.65"))}));
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Tunnel pressure solver"), FcObjectType::SimulationParameter,
        "PRES", {}, 3,
        {raw("CHECK_POISSON", QStringLiteral("T")),
         raw("TUNNEL_PRECONDITIONER", QStringLiteral("T"))}));
    document->configurationGroup()->addChild(namelist(
        QStringLiteral("Velocity error output"), FcObjectType::SimulationParameter,
        "DUMP", {}, 4,
        {raw("VELOCITY_ERROR_FILE", QStringLiteral(".TRUE."))}));
    document->reactionsGroup()->addChild(namelist(
        QStringLiteral("Propane reaction"), FcObjectType::Reaction,
        "REAC", {}, 5,
        {text("FUEL", QStringLiteral("PROPANE")),
         raw("SOOT_YIELD", QStringLiteral("0.015"))}));
    const Namelist fireSurface = namelist(
        QStringLiteral("Tunnel fire"), FcObjectType::Surface,
        "SURF", QStringLiteral("fire"), 6,
        {text("COLOR", QStringLiteral("RED")),
         raw("HRRPUA", QStringLiteral("2000.0"))});
    document->surfacesGroup()->addChild(fireSurface);
    document->ventsGroup()->addChild(namelist(
        QStringLiteral("Tunnel fire vent"), FcObjectType::Vent,
        "VENT", {}, 7,
        {reference("SURF_ID", fireSurface),
         raw("XB", QStringLiteral("40.,42.,-1.0,1.0,0.0,0.0")),
         text("COLOR", QStringLiteral("RED"))}));
    document->ventsGroup()->addChild(namelist(
        QStringLiteral("Open tunnel portal"), FcObjectType::Vent,
        "VENT", {}, 8,
        {text("SURF_ID", QStringLiteral("OPEN")),
         raw("PBX", QStringLiteral("128.0"))}));
    document->outputsGroup()->addChild(namelist(
        QStringLiteral("Tunnel temperature vector slice"), FcObjectType::Output,
        "SLCF", {}, 9,
        {text("QUANTITY", QStringLiteral("TEMPERATURE")),
         raw("VECTOR", QStringLiteral(".TRUE.")), raw("PBY", QStringLiteral("0."))}));
    document->outputsGroup()->addChild(namelist(
        QStringLiteral("Tunnel pressure head slice"), FcObjectType::Output,
        "SLCF", {}, 10,
        {text("QUANTITY", QStringLiteral("H")),
         raw("CELL_CENTERED", QStringLiteral(".TRUE.")),
         raw("PBY", QStringLiteral("0."))}));

    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createTunnelDemoProject()
{
    return createTunnelProjectVariant(
        QStringLiteral("Example of a tunnel simulation"),
        QStringLiteral("tunnel_demo"), 30.0);
}

std::unique_ptr<FcProject> createTunnelSmoke10sProject()
{
    return createTunnelProjectVariant(
        QStringLiteral("Tunnel Demo - 10 s FireCAE Smoke Test"),
        QStringLiteral("tunnel_smoke_10s"), 10.0);
}
}
