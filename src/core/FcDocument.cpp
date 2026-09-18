#include "core/FcDocument.h"

#include "core/FcObjectGroup.h"

FcDocument::FcDocument()
    : m_groups{
          std::make_shared<FcObjectGroup>(QStringLiteral("Geometry")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Meshes")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Configuration")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Species")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Materials")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Surfaces")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Reactions")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Particles")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Vents")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Devices")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Controls")),
          std::make_shared<FcObjectGroup>(QStringLiteral("HVAC")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Initial Conditions")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Outputs")),
          std::make_shared<FcObjectGroup>(QStringLiteral("Results")),
      }
{
}

const FcDocument::GroupArray& FcDocument::groups() const
{
    return m_groups;
}

FcDocument::GroupPtr FcDocument::group(FcDocumentGroup type) const
{
    const std::size_t index = static_cast<std::size_t>(type);
    return index < m_groups.size() ? m_groups[index] : GroupPtr{};
}

FcDocument::GroupPtr FcDocument::geometryGroup() const { return group(FcDocumentGroup::Geometry); }
FcDocument::GroupPtr FcDocument::meshesGroup() const { return group(FcDocumentGroup::Meshes); }
FcDocument::GroupPtr FcDocument::configurationGroup() const { return group(FcDocumentGroup::Configuration); }
FcDocument::GroupPtr FcDocument::speciesGroup() const { return group(FcDocumentGroup::Species); }
FcDocument::GroupPtr FcDocument::materialsGroup() const { return group(FcDocumentGroup::Materials); }
FcDocument::GroupPtr FcDocument::surfacesGroup() const { return group(FcDocumentGroup::Surfaces); }
FcDocument::GroupPtr FcDocument::reactionsGroup() const { return group(FcDocumentGroup::Reactions); }
FcDocument::GroupPtr FcDocument::particlesGroup() const { return group(FcDocumentGroup::Particles); }
FcDocument::GroupPtr FcDocument::ventsGroup() const { return group(FcDocumentGroup::Vents); }
FcDocument::GroupPtr FcDocument::devicesGroup() const { return group(FcDocumentGroup::Devices); }
FcDocument::GroupPtr FcDocument::controlsGroup() const { return group(FcDocumentGroup::Controls); }
FcDocument::GroupPtr FcDocument::hvacGroup() const { return group(FcDocumentGroup::HVAC); }
FcDocument::GroupPtr FcDocument::initialConditionsGroup() const { return group(FcDocumentGroup::InitialConditions); }
FcDocument::GroupPtr FcDocument::outputsGroup() const { return group(FcDocumentGroup::Outputs); }
FcDocument::GroupPtr FcDocument::resultsGroup() const { return group(FcDocumentGroup::Results); }

FcObject::Ptr FcDocument::findObject(const QString& id) const
{
    if (id.isEmpty()) {
        return {};
    }

    for (const GroupPtr& root : m_groups) {
        if (root->id() == id) {
            return root;
        }
        FcObject::Ptr match = root->findChild(id, true);
        if (match) {
            return match;
        }
    }
    return {};
}
