#pragma once

#include "core/FcObject.h"

#include <array>
#include <cstddef>
#include <memory>

class FcObjectGroup;

enum class FcDocumentGroup : std::size_t
{
    Geometry,
    Meshes,
    Configuration,
    Species,
    Materials,
    Surfaces,
    Reactions,
    Particles,
    Vents,
    Devices,
    Controls,
    HVAC,
    InitialConditions,
    Outputs,
    Results,
    Count
};

class FcDocument
{
public:
    using GroupPtr = std::shared_ptr<FcObjectGroup>;
    static constexpr std::size_t StandardGroupCount =
        static_cast<std::size_t>(FcDocumentGroup::Count);
    using GroupArray = std::array<GroupPtr, StandardGroupCount>;

    FcDocument();

    FcDocument(const FcDocument&) = delete;
    FcDocument& operator=(const FcDocument&) = delete;

    const GroupArray& groups() const;
    GroupPtr group(FcDocumentGroup type) const;

    GroupPtr geometryGroup() const;
    GroupPtr meshesGroup() const;
    GroupPtr configurationGroup() const;
    GroupPtr speciesGroup() const;
    GroupPtr materialsGroup() const;
    GroupPtr surfacesGroup() const;
    GroupPtr reactionsGroup() const;
    GroupPtr particlesGroup() const;
    GroupPtr ventsGroup() const;
    GroupPtr devicesGroup() const;
    GroupPtr controlsGroup() const;
    GroupPtr hvacGroup() const;
    GroupPtr initialConditionsGroup() const;
    GroupPtr outputsGroup() const;
    GroupPtr resultsGroup() const;

    FcObject::Ptr findObject(const QString& id) const;

private:
    GroupArray m_groups;
};
