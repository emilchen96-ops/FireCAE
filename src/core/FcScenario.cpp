#include "core/FcScenario.h"

#include <QUuid>

FcScenario FcScenario::create(const QString& name, const QString& chid)
{
    FcScenario scenario;
    scenario.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    scenario.name = name.trimmed().isEmpty() ? QStringLiteral("Scenario") : name.trimmed();
    scenario.chid = chid.trimmed();
    return scenario;
}

bool FcScenario::isValid() const
{
    return !QUuid(id).isNull() && !name.trimmed().isEmpty();
}

const FcScenarioParameterOverride* FcScenario::overrideFor(
    const QString& objectId, const QString& parameterKey) const
{
    for (const FcScenarioParameterOverride& entry : parameterOverrides) {
        if (entry.objectId == objectId &&
            entry.parameterKey.compare(parameterKey, Qt::CaseInsensitive) == 0) {
            return &entry;
        }
    }
    return nullptr;
}
