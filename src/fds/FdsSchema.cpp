#include "fds/FdsSchema.h"

#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
using Type = FdsSchemaValueType;

FdsParameterSchema field(const char* name,
                         const char* category,
                         Type type,
                         const char* defaultValue = "",
                         const char* unit = "",
                         bool required = false,
                         std::optional<double> minimum = {},
                         std::optional<double> maximum = {},
                         int arrayLength = 0,
                         QStringList enumValues = {},
                         QStringList references = {},
                         const char* hint = "")
{
    return {QString::fromLatin1(name), QString::fromLatin1(category), type,
            QString::fromLatin1(unit), QString::fromLatin1(defaultValue),
            required, minimum, maximum, arrayLength, std::move(enumValues),
            std::move(references), QStringLiteral("6.7"), QString::fromUtf8(hint)};
}

FdsNamelistSchema list(const char* keyword,
                       const char* title,
                       bool idRequired,
                       std::initializer_list<FdsParameterSchema> parameters)
{
    return {QString::fromLatin1(keyword), QString::fromUtf8(title),
            QStringLiteral("6.7"), idRequired,
            QVector<FdsParameterSchema>(parameters.begin(), parameters.end())};
}

FdsParameterSchema availableSince(FdsParameterSchema schema,
                                  const char* version)
{
    schema.sinceVersion = QString::fromLatin1(version);
    return schema;
}

const QMap<QString, FdsNamelistSchema>& schemas()
{
    static const QMap<QString, FdsNamelistSchema> value = [] {
        QMap<QString, FdsNamelistSchema> result;
        const auto add = [&result](FdsNamelistSchema schema) {
            result.insert(schema.keyword, std::move(schema));
        };
        add(list("HEAD", "Case identity", false, {
            field("CHID", "General", Type::String, "firecae_case", "", true, {}, {}, 0, {}, {}, "Filesystem-safe case identifier."),
            field("TITLE", "General", Type::String, "FireCAE Case") }));
        add(list("TIME", "Simulation time", false, {
            field("T_BEGIN", "Time", Type::Real, "0", "s", false),
            field("T_END", "Time", Type::Real, "60", "s", false, 0.0),
            field("DT", "Time", Type::Real, "", "s", false, 0.0) }));
        add(list("MESH", "Computational mesh", false, {
            field("IJK", "Mesh", Type::IntegerArray, "20,20,12", "cells", true, 1.0, {}, 3),
            field("XB", "Mesh", Type::RealArray, "0,10,0,10,0,3", "m", true, {}, {}, 6),
            field("MULT_ID", "Replication", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"MULT"}) }));
        add(list("MULT", "Multiplier", true, {
            field("DX", "Translation", Type::Real, "10", "m"), field("DY", "Translation", Type::Real, "0", "m"),
            field("DZ", "Translation", Type::Real, "0", "m"), field("I_UPPER", "Count", Type::Integer, "1", "", false, 0.0),
            field("J_UPPER", "Count", Type::Integer, "0", "", false, 0.0), field("K_UPPER", "Count", Type::Integer, "0", "", false, 0.0) }));
        add(list("OBST", "Obstruction", false, {
            field("XB", "Geometry", Type::RealArray, "0,1,0,1,0,1", "m", true, {}, {}, 6),
            field("SURF_ID", "Surfaces", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SURF"}),
            field("SURF_ID6", "Surfaces", Type::ObjectReference, "", "", false, {}, {}, 6, {}, {"SURF"}),
            field("CTRL_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"CTRL"}),
            field("DEVC_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"DEVC"}),
            field("COLOR", "Appearance", Type::String, "GRAY") }));
        add(list("HOLE", "Obstruction opening", false, {
            field("XB", "Geometry", Type::RealArray, "0,1,0,1,0,1", "m", true, {}, {}, 6),
            field("CTRL_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"CTRL"}),
            field("DEVC_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"DEVC"}) }));
        add(list("VENT", "Vent", false, {
            field("XB", "Geometry", Type::RealArray, "0,1,0,1,0,0", "m", false, {}, {}, 6),
            field("MB", "Geometry", Type::Enumeration, "", "", false, {}, {}, 0, {"XMIN","XMAX","YMIN","YMAX","ZMIN","ZMAX"}),
            field("PBX", "Geometry", Type::Real, "", "m"), field("PBY", "Geometry", Type::Real, "", "m"), field("PBZ", "Geometry", Type::Real, "", "m"),
            field("SURF_ID", "Surface", Type::ObjectReference, "OPEN", "", true, {}, {}, 0, {}, {"SURF"}),
            field("CTRL_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"CTRL"}),
            field("DEVC_ID", "Activation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"DEVC"}) }));
        add(list("MATL", "Solid material", true, {
            field("DENSITY", "Thermal", Type::Real, "800", "kg/m³", true, 0.0),
            field("CONDUCTIVITY", "Thermal", Type::Real, "0.15", "W/(m·K)", true, 0.0),
            field("SPECIFIC_HEAT", "Thermal", Type::Real, "1.5", "kJ/(kg·K)", true, 0.0),
            field("EMISSIVITY", "Thermal", Type::Real, "0.9", "", false, 0.0, 1.0),
            field("RAMP_K", "Temperature Curves", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}),
            field("RAMP_C_P", "Temperature Curves", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}),
            field("N_REACTIONS", "Pyrolysis", Type::Integer, "0", "", false, 0.0),
            field("HEAT_OF_REACTION", "Pyrolysis", Type::RealArray, "", "kJ/kg", false, 0.0),
            field("HEAT_OF_COMBUSTION", "Pyrolysis", Type::Real, "", "kJ/kg", false, 0.0),
            field("NU_MATL", "Pyrolysis", Type::RealArray, "", "", false, 0.0),
            field("REFERENCE_TEMPERATURE", "Pyrolysis", Type::RealArray, "", "°C"),
            field("HEATING_RATE", "Pyrolysis", Type::RealArray, "", "K/min", false, 0.0),
            field("MOISTURE_FRACTION", "Moisture", Type::Real, "0", "kg/kg", false, 0.0, 1.0),
            field("ABSORPTION_COEFFICIENT", "Radiation", Type::Real, "", "1/m", false, 0.0),
            field("BOILING_TEMPERATURE", "Liquid fuel", Type::Real, "", "°C"),
            field("HEAT_OF_VAPORIZATION", "Liquid fuel", Type::Real, "", "kJ/kg", false, 0.0),
            field("VAPORIZATION_RATE", "Liquid fuel", Type::Real, "", "kg/(m²·s)", false, 0.0) }));
        add(list("SURF", "Boundary surface", true, {
            field("MATL_ID", "Layers", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"MATL"}),
            field("THICKNESS", "Layers", Type::RealArray, "0.012", "m", false, 0.0),
            field("BACKING", "Layers", Type::Enumeration, "VOID", "", false, {}, {}, 0, {"VOID","EXPOSED","INSULATED"}),
            field("HRRPUA", "Fire", Type::Real, "", "kW/m²", false, 0.0),
            field("MLRPUA", "Fire", Type::Real, "", "kg/(m²·s)", false, 0.0),
            field("VEL", "Airflow", Type::Real, "", "m/s"),
            field("VOLUME_FLUX", "Airflow", Type::Real, "", "m³/(m²·s)"),
            field("TMP_FRONT", "Thermal", Type::Real, "", "°C"),
            field("PART_ID", "Particle injection", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"PART"}),
            field("SPEC_ID", "Species injection", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SPEC"}),
            field("MASS_FLUX", "Species injection", Type::Real, "", "kg/(m²·s)"),
            field("MASS_FRACTION", "Species injection", Type::RealArray, "", "", false, 0.0, 1.0),
            field("RAMP_Q", "Fire", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}),
            field("TAU_Q", "Fire", Type::Real, "", "s"), field("TMP_IGN", "Fire", Type::Real, "", "°C"),
            field("BURN_AWAY", "Fire", Type::Boolean, ".FALSE."), field("EMISSIVITY", "Radiation", Type::Real, "0.9", "", false, 0.0, 1.0),
            field("COLOR", "Appearance", Type::String, "GRAY"), field("TEXTURE_MAP", "Appearance", Type::String),
            availableSince(field("DELAMINATION_DENSITY", "Layers", Type::RealArray, "", "kg/m3", false, 0.0), "6.11"),
            availableSince(field("DELAMINATION_TMP", "Layers", Type::RealArray, "", "°C"), "6.11"),
            availableSince(field("HT3D_WEIGHT", "Heat Transfer", Type::RealArray, "", "", false, 0.0, 1.0, 3), "6.11"),
            availableSince(field("MINIMUM_LAYER_MASS_FRACTION", "Layers", Type::RealArray, "", "", false, 0.0, 1.0), "6.11"),
            availableSince(field("MOISTURE_CONTENT", "Moisture", Type::RealArray, "", "kg/kg", false, 0.0), "6.11"),
            availableSince(field("NODE_ID", "HVAC", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}), "6.11"),
            availableSince(field("SKIP_INRAD", "Radiation", Type::Boolean), "6.11"),
            availableSince(field("VEG_LSET_WIND_HEIGHT", "Vegetation", Type::Real, "", "m"), "6.11"),
            availableSince(field("VEG_LSET_WIND_RAMP", "Vegetation", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}), "6.11") }));
        // REAC ID is optional in FDS.  A reaction can be identified solely by
        // FUEL (as the official couch and tunnel tutorials do).
        add(list("REAC", "Gas reaction", false, {
            field("FUEL", "Combustion", Type::String, "PROPANE", "", true),
            field("SOOT_YIELD", "Yields", Type::Real, "0.01", "kg/kg", false, 0.0),
            field("CO_YIELD", "Yields", Type::Real, "0", "kg/kg", false, 0.0),
            field("HEAT_OF_COMBUSTION", "Combustion", Type::Real, "", "kJ/kg", false, 0.0),
            field("RADIATIVE_FRACTION", "Radiation", Type::Real, "", "", false, 0.0, 1.0) }));
        add(list("SPEC", "Species", true, {
            field("FORMULA", "Identity", Type::String, "CO2"), field("MASS_FRACTION_0", "Initial", Type::Real, "0", "", false, 0.0, 1.0),
            field("BACKGROUND", "Initial", Type::Boolean, ".FALSE.") }));
        add(list("PART", "Lagrangian particle", true, {
            field("SPEC_ID", "Composition", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SPEC"}),
            field("SURF_ID", "Surface", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SURF"}),
            field("DIAMETER", "Particle", Type::Real, "1000", "µm", false, 0.0),
            field("DISTRIBUTION", "Particle", Type::Enumeration, "MONODISPERSE", "", false, {}, {}, 0,
                  {"MONODISPERSE","ROSIN-RAMMLER-LOGNORMAL","LOGNORMAL","NORMAL","UNIFORM"}),
            field("MINIMUM_DIAMETER", "Particle", Type::Real, "", "µm", false, 0.0),
            field("MAXIMUM_DIAMETER", "Particle", Type::Real, "", "µm", false, 0.0),
            field("GAMMA_D", "Particle", Type::Real, "", "", false, 0.0),
            field("QUANTITIES(1)", "Output", Type::String, "PARTICLE DIAMETER"),
            field("STATIC", "Particle", Type::Boolean, ".FALSE."), field("AGE", "Particle", Type::Real, "", "s", false, 0.0) }));
        add(list("PROP", "Device/particle property", true, {
            field("QUANTITY", "General", Type::String, "SPRINKLER LINK TEMPERATURE"),
            field("PART_ID", "Spray", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"PART"}),
            field("FLOW_RATE", "Spray", Type::Real, "60", "L/min", false, 0.0),
            field("K_FACTOR", "Spray", Type::Real, "", "L/(min·bar½)", false, 0.0),
            field("PARTICLE_VELOCITY", "Spray", Type::Real, "5", "m/s", false, 0.0),
            field("PARTICLES_PER_SECOND", "Spray", Type::Integer, "10000", "1/s", false, 1.0),
            field("OPERATING_PRESSURE", "Spray", Type::Real, "", "bar", false, 0.0),
            field("ACTIVATION_TEMPERATURE", "Activation", Type::Real, "74", "°C"),
            field("RTI", "Activation", Type::Real, "50", "(m·s)½", false, 0.0),
            field("SPRAY_PATTERN_TABLE", "Spray", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"TABL"}),
            field("SMOKEVIEW_ID", "Appearance", Type::String, "sprinkler_upright"),
            availableSince(field("CALIBRATION_CONSTANT", "Detector", Type::Real, "", "", false, 0.0), "6.11"),
            availableSince(field("IGNITION_ZONE", "Activation", Type::Boolean), "6.11"),
            availableSince(field("PROBE_DIAMETER", "Detector", Type::Real, "", "m", false, 0.0), "6.11"),
            availableSince(field("SPECIFIC_HEAT_RAMP", "Thermal", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}), "6.11"),
            availableSince(field("TC", "Detector", Type::Boolean), "6.11") }));
        add(list("TABL", "Lookup table row", true, {
            field("TABLE_DATA", "Table", Type::RealArray, "0,0,0,0,0,0", "", true, {}, {}, 6) }));
        add(list("RAMP", "Time/value ramp row", true, {
            field("T", "Ramp", Type::Real, "0", "s", true), field("F", "Ramp", Type::Real, "0", "", true),
            availableSince(field("CYCLING", "Ramp", Type::Boolean), "6.11") }));
        add(list("DEVC", "Device", true, {
            field("QUANTITY", "Measurement", Type::Enumeration, "TEMPERATURE", "", false, {}, {}, 0,
                  {"TEMPERATURE","THERMOCOUPLE","LINK TEMPERATURE","SPRINKLER LINK TEMPERATURE","CHAMBER OBSCURATION","CHAMBER DENSITY","PATH OBSCURATION","PATH LENGTH","VISIBILITY","HEAT FLUX","RADIATIVE HEAT FLUX","VOLUME FRACTION","MASS FRACTION","HRR","VOLUME FLOW","VELOCITY","PRESSURE","TIME","AMPUA","AIRCOIL HEAT EXCHANGE","NODE TEMPERATURE"}),
            field("XYZ", "Location", Type::RealArray, "0.5,0.5,1.5", "m", false, {}, {}, 3),
            field("XB", "Location", Type::RealArray, "", "m", false, {}, {}, 6),
            field("PROP_ID", "Detector", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"PROP"}),
            field("PART_ID", "Particle", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"PART"}),
            availableSince(field("ELEM_ID", "Measurement", Type::String), "6.11"),
            field("SETPOINT", "Activation", Type::Real), field("TRIP_DIRECTION", "Activation", Type::Integer, "1", "", false, -1.0, 1.0),
            field("INITIAL_STATE", "Activation", Type::Boolean, ".FALSE."),
            field("DUCT_ID", "HVAC", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}),
            field("NODE_ID", "HVAC", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}) }));
        add(list("CTRL", "Control logic", true, {
            field("FUNCTION_TYPE", "Logic", Type::Enumeration, "ANY", "", true, {}, {}, 0, {"ANY","ALL","ONLY","AT_LEAST","SUM","PRODUCT","CUSTOM","TIME_DELAY","DEAD_TIME","CYCLIC"}),
            field("INPUT_ID", "Logic", Type::ObjectReference, "", "", true, {}, {}, 0, {}, {"DEVC","CTRL"}),
            field("RAMP_ID", "Logic", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}),
            field("CONSTANT", "Logic", Type::RealArray),
            field("DELAY", "Logic", Type::Real, "", "s", false, 0.0),
            field("INITIAL_STATE", "Logic", Type::Boolean, ".FALSE.") }));
        add(list("HVAC", "HVAC network component", true, {
            field("TYPE_ID", "Component", Type::Enumeration, "DUCT", "", true, {}, {}, 0, {"NODE","DUCT","FAN","AIRCOIL","FILTER"}),
            field("NODE_ID", "Topology", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}),
            field("DUCT_ID", "Topology", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}),
            field("VENT_ID", "Topology", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"VENT"}),
            field("FAN_ID", "Equipment", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}),
            field("AIRCOIL_ID", "Equipment", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"HVAC"}),
            field("AREA", "Flow", Type::Real, "0.1", "m²", false, 0.0), field("LENGTH", "Flow", Type::Real, "1", "m", false, 0.0),
            field("VOLUME_FLOW", "Flow", Type::Real, "", "m³/s"), field("LOSS", "Flow", Type::RealArray, "", "", false, 0.0) }));
        add(list("INIT", "Initial condition", false, {
            field("XB", "Region", Type::RealArray, "0,1,0,1,0,1", "m", false, {}, {}, 6),
            field("TEMPERATURE", "State", Type::Real, "20", "°C"), field("DENSITY", "State", Type::Real, "", "kg/m³", false, 0.0),
            field("MASS_FRACTION", "State", Type::RealArray, "", "", false, 0.0, 1.0),
            field("SPEC_ID", "State", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SPEC"}) }));
        const auto output = [&add](const char* keyword, const char* title) {
            add(list(keyword, title, false, {
                field("QUANTITY", "Output", Type::String, "TEMPERATURE", "", true),
                field("SPEC_ID", "Output", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SPEC"}),
                field("PBX", "Plane", Type::Real, "", "m"), field("PBY", "Plane", Type::Real, "", "m"), field("PBZ", "Plane", Type::Real, "", "m"),
                field("XB", "Region", Type::RealArray, "", "m", false, {}, {}, 6), field("VECTOR", "Output", Type::Boolean, ".FALSE.") }));
        };
        output("SLCF", "Slice output"); output("BNDF", "Boundary output"); output("ISOF", "Isosurface output"); output("PL3D", "Plot3D output");
        output("SM3D", "Smoke3D output");
        result[QStringLiteral("SLCF")].parameters.append(
            availableSince(field("DRY", "Output", Type::Boolean), "6.11"));
        add(list("PROF", "Profile output", true, {
            field("QUANTITY", "Output", Type::String, "TEMPERATURE", "", true),
            field("XYZ", "Location", Type::RealArray, "0,0,1.5", "m", true, {}, {}, 3),
            field("IOR", "Direction", Type::Integer, "3", "", false, -3.0, 3.0),
            field("CELL_CENTERED", "Output", Type::Boolean, ".FALSE.") }));
        add(list("DUMP", "Output cadence", false, {
            field("DT_DEVC", "CSV", Type::Real, "1", "s", false, 0.0), field("DT_HRR", "CSV", Type::Real, "1", "s", false, 0.0),
            field("DT_SLCF", "Field", Type::Real, "1", "s", false, 0.0), field("DT_BNDF", "Field", Type::Real, "1", "s", false, 0.0),
            field("DT_PART", "Particle", Type::Real, "1", "s", false, 0.0),
            field("DT_RESTART", "Restart", Type::Real, "", "s", false, 0.0),
            field("WRITE_XYZ", "CSV", Type::Boolean, ".FALSE."),
            availableSince(field("BINGEOM_DIR", "Geometry", Type::String), "6.11"),
            availableSince(field("DECIMAL_SPECIFIER", "CSV", Type::Enumeration, "", "", false, {}, {}, 0, {"POINT","COMMA"}), "6.11"),
            availableSince(field("WRITE_CVODE_SUBSTEPS", "Diagnostics", Type::Boolean), "6.11") }));
        add(list("MISC", "Miscellaneous physics", false, {
            field("TMPA", "Ambient", Type::Real, "20", "°C"),
            field("P_INF", "Ambient", Type::Real, "101325", "Pa", false, 1.0),
            field("TURBULENCE_MODEL", "Flow", Type::String, "DEARDORFF"),
            field("SIMULATION_MODE", "Flow", Type::Enumeration, "VLES", "", false,
                  {}, {}, 0, {"DNS","LES","VLES","SVLES"}),
            field("DNS", "Flow", Type::Boolean, ".FALSE."),
            field("GVEC", "Gravity", Type::RealArray, "0,0,-9.81", "m/s²", false, {}, {}, 3),
            field("HUMIDITY", "Ambient", Type::Real, "40", "%", false, 0.0, 100.0),
            field("RESTART", "Restart", Type::Boolean, ".FALSE."),
            field("RESTART_CHID", "Restart", Type::String),
            availableSince(field("PR_T", "Flow", Type::Real, "", "", false, 0.0), "6.11"),
            availableSince(field("SC_T", "Flow", Type::Real, "", "", false, 0.0), "6.11"),
            availableSince(field("TEST_NEW_KSGS_MODEL", "Advanced", Type::Boolean), "6.11") }));
        add(list("PRES", "Pressure solver", false, {
            field("MAX_PRESSURE_ITERATIONS", "Solver", Type::Integer, "1000", "", false, 1.0), field("VELOCITY_TOLERANCE", "Solver", Type::Real, "", "m/s", false, 0.0),
            availableSince(field("MAX_PREDICTOR_PRESSURE_ITERATIONS", "Solver", Type::Integer), "6.11"),
            availableSince(field("WRITE_PARCSRPCG_MATRIX", "Diagnostics", Type::Boolean), "6.11") }));
        add(list("RADI", "Radiation", false, {
            field("RADIATION", "Radiation", Type::Boolean, ".TRUE."), field("NUMBER_RADIATION_ANGLES", "Radiation", Type::Integer, "100", "", false, 1.0),
            availableSince(field("RANDOMIZE_RADIATION_DIRECTIONS", "Radiation", Type::Boolean), "6.11") }));
        add(list("COMB", "Combustion", false, {
            field("EXTINCTION_MODEL", "Combustion", Type::String), field("FIXED_MIX_TIME", "Combustion", Type::Real, "", "s", false, 0.0),
            availableSince(field("CVODE_ORDER", "Finite Rate", Type::Integer, "", "", false, 0.0), "6.11"),
            availableSince(field("FUEL_ID_FOR_AFT", "Combustion", Type::String), "6.11"),
            availableSince(field("RAMP_ZETA_0", "Combustion", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}), "6.11"),
            availableSince(field("TURBULENT_FLAME_SPEED", "Combustion", Type::Real, "", "m/s", false, 0.0), "6.11"),
            availableSince(field("USE_MIXED_ZN_AFT_TMP", "Combustion", Type::Boolean), "6.11"),
            availableSince(field("VARIABLE_CFT", "Combustion", Type::Boolean), "6.11") }));
        add(list("WIND", "Wind and atmosphere", false, {
            field("SPEED", "Wind", Type::Real, "0", "m/s", false, 0.0),
            field("DIRECTION", "Wind", Type::Real, "270", "deg"),
            field("Z_0", "Wind", Type::Real, "0.03", "m", false, 0.0),
            field("Z_REF", "Wind", Type::Real, "2", "m", false, 0.0),
            field("STRATIFICATION", "Atmosphere", Type::Boolean, ".TRUE.") }));
        add(list("MOVE", "Prescribed motion", true, {
            field("AXIS", "Motion", Type::RealArray, "0,0,1", "", false, {}, {}, 3), field("ROTATION_ANGLE", "Motion", Type::Real, "0", "deg"),
            field("DX", "Motion", Type::Real, "0", "m"), field("DY", "Motion", Type::Real, "0", "m"), field("DZ", "Motion", Type::Real, "0", "m"),
            field("RAMP_ID", "Motion", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"RAMP"}) }));
        add(list("GEOM", "Complex geometry", true, {
            field("XB", "Geometry", Type::RealArray, "", "m", false, {}, {}, 6), field("VERTS", "Geometry", Type::RealArray, "", "m"),
            field("FACES", "Geometry", Type::IntegerArray), field("SURF_ID", "Surface", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"SURF"}),
            field("MOVE_ID", "Motion", Type::ObjectReference, "", "", false, {}, {}, 0, {}, {"MOVE"}) }));
        return result;
    }();
    return value;
}

FcFdsParameterKind defaultParameterKind(const FdsNamelistSchema& schema,
                                        const FdsParameterSchema& definition)
{
    if (definition.type == Type::ObjectReference) {
        // OPEN is an FDS built-in surface, not a project object with a UUID.
        // Keep every other reference as a reference, including empty defaults.
        if (schema.keyword == QStringLiteral("VENT") &&
            definition.name == QStringLiteral("SURF_ID") &&
            definition.defaultValue == QStringLiteral("OPEN")) {
            return FcFdsParameterKind::String;
        }
        return FcFdsParameterKind::ObjectReferences;
    }
    return definition.type == Type::String || definition.type == Type::Enumeration
               ? FcFdsParameterKind::String : FcFdsParameterKind::Raw;
}

QString baseName(const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    const int index = normalized.indexOf(QLatin1Char('('));
    return normalized.left(index < 0 ? normalized.size() : index);
}

bool versionAtLeast(const QString& version, const QString& minimum)
{
    const QStringList current = version.split(QLatin1Char('.'));
    const QStringList needed = minimum.split(QLatin1Char('.'));
    for (int i = 0; i < std::max(current.size(), needed.size()); ++i) {
        const int a = i < current.size() ? current[i].toInt() : 0;
        const int b = i < needed.size() ? needed[i].toInt() : 0;
        if (a != b) return a > b;
    }
    return true;
}

QString extractedVersionNumber(const QString& text)
{
    static const QRegularExpression expression(
        QStringLiteral(R"((?:FDS-)?(\d+\.\d+(?:\.\d+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(text.trimmed());
    return match.hasMatch() ? match.captured(1) : QString{};
}

QStringList splitValues(const QString& raw)
{
    return raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
}
}

QString FdsSchemaRegistry::defaultVersion() { return QStringLiteral("6.11.1"); }

QStringList FdsSchemaRegistry::supportedVersions()
{
    return {QStringLiteral("6.7"), QStringLiteral("6.8"),
            QStringLiteral("6.9"), QStringLiteral("6.10"),
            QStringLiteral("6.11"), QStringLiteral("6.11.1")};
}

QString FdsSchemaRegistry::versionFromRevision(const QString& revision)
{
    return extractedVersionNumber(revision);
}

QString FdsSchemaRegistry::compatibleVersionForRevision(const QString& revision)
{
    const QString detected = versionFromRevision(revision);
    if (detected.isEmpty()) return {};
    if (isSupportedVersion(detected)) return detected;

    const QStringList detectedParts = detected.split(QLatin1Char('.'));
    if (detectedParts.size() < 2) return {};
    QString best;
    for (const QString& candidate : supportedVersions()) {
        const QStringList candidateParts = candidate.split(QLatin1Char('.'));
        if (candidateParts.size() < 2 || candidateParts[0] != detectedParts[0] ||
            candidateParts[1] != detectedParts[1] ||
            !versionAtLeast(detected, candidate)) {
            continue;
        }
        if (best.isEmpty() || versionAtLeast(candidate, best)) best = candidate;
    }
    return best;
}

bool FdsSchemaRegistry::isSupportedVersion(const QString& version)
{
    return supportedVersions().contains(version.trimmed(), Qt::CaseInsensitive);
}

QStringList FdsSchemaRegistry::keywords(const QString& version)
{
    QStringList result;
    for (auto iterator = schemas().cbegin(); iterator != schemas().cend(); ++iterator) {
        if (versionAtLeast(version, iterator->sinceVersion)) result.append(iterator.key());
    }
    return result;
}

const FdsNamelistSchema* FdsSchemaRegistry::namelist(const QString& keyword,
                                                      const QString& version)
{
    const auto iterator = schemas().constFind(keyword.trimmed().toUpper());
    if (iterator == schemas().cend() || !versionAtLeast(version, iterator->sinceVersion)) return nullptr;
    return &iterator.value();
}

const FdsParameterSchema* FdsSchemaRegistry::parameter(const QString& keyword,
                                                       const QString& parameterName,
                                                       const QString& version)
{
    const FdsNamelistSchema* schema = namelist(keyword, version);
    if (!schema) return nullptr;
    const QString name = baseName(parameterName);
    for (const FdsParameterSchema& candidate : schema->parameters) {
        if (candidate.name == name && versionAtLeast(version, candidate.sinceVersion)) return &candidate;
    }
    return nullptr;
}

std::vector<FcFdsParameter> FdsSchemaRegistry::defaultParameters(
    const QString& keyword, const QString& version)
{
    std::vector<FcFdsParameter> result;
    const FdsNamelistSchema* schema = namelist(keyword, version);
    if (!schema) return result;
    for (const FdsParameterSchema& definition : schema->parameters) {
        if (!definition.required && definition.defaultValue.isEmpty()) continue;
        if (definition.defaultValue.isEmpty()) continue;
        const FcFdsParameterKind kind = defaultParameterKind(*schema, definition);
        result.push_back({definition.name, kind, definition.defaultValue, {}});
    }
    return result;
}

std::vector<FcFdsParameter> FdsSchemaRegistry::recommendedParameters(
    const QString& keyword, const QString& version)
{
    std::vector<FcFdsParameter> result;
    const FdsNamelistSchema* schema = namelist(keyword, version);
    if (!schema) return result;
    for (const FdsParameterSchema& definition : schema->parameters) {
        const FcFdsParameterKind kind = defaultParameterKind(*schema, definition);
        result.push_back({definition.name, kind, definition.defaultValue, {}});
    }
    return result;
}

QStringList FdsSchemaRegistry::allowedReferenceKeywords(
    const QString& keyword, const QString& parameterName, const QString& version)
{
    const FdsParameterSchema* definition = parameter(keyword, parameterName, version);
    return definition ? definition->referenceKeywords : QStringList{};
}

QStringList FdsSchemaRegistry::validate(
    const QString& keyword, const QString& fdsId,
    const std::vector<FcFdsParameter>& parameters, const QString& version)
{
    QStringList errors;
    const FdsNamelistSchema* schema = namelist(keyword, version);
    if (!schema) return errors; // Unknown namelists remain lossless and editable.
    if (schema->idRequired && fdsId.trimmed().isEmpty()) {
        errors.append(schema->keyword + QStringLiteral(" requires a non-empty FDS ID."));
    }
    QSet<QString> present;
    for (const FcFdsParameter& parameterValue : parameters) {
        const QString normalized = baseName(parameterValue.key);
        if (present.contains(parameterValue.key.trimmed().toUpper())) {
            errors.append(QStringLiteral("Duplicate parameter %1 in %2.")
                              .arg(parameterValue.key.trimmed().toUpper(), schema->keyword));
        }
        present.insert(parameterValue.key.trimmed().toUpper());
        // These pairs are known invalid, rather than merely absent from our
        // partial schema. Preserve all other unknown fields for lossless import.
        const bool geometryRecord = schema->keyword == QStringLiteral("OBST") ||
                                    schema->keyword == QStringLiteral("HOLE");
        const bool invalidGeometryParameter = geometryRecord &&
            (normalized == QStringLiteral("IJK") || normalized == QStringLiteral("INITIAL_STATE"));
        const bool invalidVentState = schema->keyword == QStringLiteral("VENT") &&
                                      normalized == QStringLiteral("INITIAL_STATE");
        if (invalidGeometryParameter || invalidVentState) {
            errors.append(QStringLiteral("%1 does not support parameter %2.")
                              .arg(schema->keyword, normalized));
            continue;
        }
        const FdsParameterSchema* definition = parameter(schema->keyword, normalized, version);
        if (!definition) continue; // Preserve and pass through unknown parameters.
        if (parameterValue.kind == FcFdsParameterKind::ObjectReferences) {
            if (schema->keyword == QStringLiteral("CTRL") && normalized == QStringLiteral("DELAY")) {
                errors.append(QStringLiteral("CTRL DELAY must be a non-negative real number in seconds, not an object reference."));
                continue;
            }
            if (parameterValue.targetObjectIds.isEmpty() && definition->required &&
                parameterValue.value.trimmed().isEmpty()) {
                errors.append(QStringLiteral("%1 requires reference %2.")
                                  .arg(schema->keyword, definition->name));
            }
            continue;
        }
        const QString raw = parameterValue.value.trimmed();
        if (raw.isEmpty()) {
            if (definition->required) errors.append(QStringLiteral("%1 requires parameter %2.").arg(schema->keyword, definition->name));
            continue;
        }
        const auto checkNumber = [&](const QString& text, bool integer) {
            bool ok = false;
            const double number = QLocale::c().toDouble(text.trimmed(), &ok);
            if (!ok || !std::isfinite(number) || (integer && std::floor(number) != number)) return false;
            if (definition->minimum && number < *definition->minimum) return false;
            if (definition->maximum && number > *definition->maximum) return false;
            return true;
        };
        bool valid = true;
        if (definition->type == Type::Integer) valid = checkNumber(raw, true);
        else if (definition->type == Type::Real) valid = checkNumber(raw, false);
        else if (definition->type == Type::IntegerArray || definition->type == Type::RealArray) {
            const QStringList values = splitValues(raw);
            valid = definition->arrayLength <= 0 || values.size() == definition->arrayLength;
            for (const QString& value : values) valid = valid && checkNumber(value, definition->type == Type::IntegerArray);
        } else if (definition->type == Type::Boolean) {
            const QString boolean = raw.toUpper();
            valid = boolean == QStringLiteral("T") || boolean == QStringLiteral("F") ||
                    boolean == QStringLiteral(".TRUE.") || boolean == QStringLiteral(".FALSE.") ||
                    boolean == QStringLiteral("TRUE") || boolean == QStringLiteral("FALSE");
        } else if (definition->type == Type::Enumeration) {
            QString enumValue = raw;
            enumValue.remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
            valid = definition->enumValues.contains(enumValue, Qt::CaseInsensitive);
            // FDS exposes a version-dependent, extensible quantity catalogue.
            // Keep the professional DEVC combo as a set of common suggestions,
            // while accepting a user-entered quantity so imported/newer FDS
            // cases remain lossless and exportable.
            if (schema->keyword == QStringLiteral("DEVC") &&
                definition->name == QStringLiteral("QUANTITY") &&
                !enumValue.trimmed().isEmpty()) {
                valid = true;
            }
        }
        if (!valid) {
            QString expectation = valueTypeName(definition->type);
            if (definition->arrayLength > 0) expectation += QStringLiteral("[%1]").arg(definition->arrayLength);
            if (!definition->enumValues.isEmpty()) expectation += QStringLiteral(" {%1}").arg(definition->enumValues.join(QStringLiteral(", ")));
            errors.append(QStringLiteral("%1 %2 must be %3%4.")
                              .arg(schema->keyword, definition->name, expectation,
                                   definition->unit.isEmpty() ? QString{} : QStringLiteral(" (%1)").arg(definition->unit)));
        }
    }
    for (const FdsParameterSchema& definition : schema->parameters) {
        if (definition.required && !present.contains(definition.name)) {
            errors.append(QStringLiteral("%1 requires parameter %2.").arg(schema->keyword, definition.name));
        }
    }
    return errors;
}

QString FdsSchemaRegistry::valueTypeName(FdsSchemaValueType type)
{
    switch (type) {
    case Type::Boolean: return QStringLiteral("Boolean");
    case Type::Integer: return QStringLiteral("Integer");
    case Type::Real: return QStringLiteral("Real");
    case Type::String: return QStringLiteral("Text");
    case Type::Enumeration: return QStringLiteral("Choice");
    case Type::IntegerArray: return QStringLiteral("Integer Array");
    case Type::RealArray: return QStringLiteral("Real Array");
    case Type::ObjectReference: return QStringLiteral("Object Reference");
    }
    return {};
}
