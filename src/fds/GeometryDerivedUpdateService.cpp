#include "fds/GeometryDerivedUpdateService.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "fds/FdsBlockConversionService.h"
#include "geometry/FcGeometryObject.h"
#include <QMap>
#include <QSet>
#include <functional>

namespace {
QString compactRaw(const QString& value)
{
    QString result;
    QChar quote;
    for (const QChar character : value) {
        if (quote.isNull()) {
            if (character == QLatin1Char('\'') || character == QLatin1Char('"')) quote = character;
            if (character.isSpace()) continue;
        } else if (character == quote) quote = QChar();
        result.append(character);
    }
    return result;
}

bool equivalent(const FcFdsNamelist& actual, const FcFdsNamelist& expected)
{
    if (actual.keyword() != expected.keyword() || actual.parameters().size() != expected.parameters().size()) return false;
    QMap<QString, FcFdsParameter> parameters;
    for (const auto& parameter : actual.parameters()) {
        const QString key = parameter.key.trimmed().toUpper();
        if (parameters.contains(key)) return false;
        parameters.insert(key, parameter);
    }
    for (const auto& parameter : expected.parameters()) {
        const auto found = parameters.constFind(parameter.key.trimmed().toUpper());
        if (found == parameters.cend() || found->kind != parameter.kind ||
            found->targetObjectIds != parameter.targetObjectIds) return false;
        if (parameter.kind == FcFdsParameterKind::Raw) {
            if (compactRaw(found->value) != compactRaw(parameter.value)) return false;
        } else if (found->value != parameter.value) return false;
    }
    return true;
}
}

GeometryDerivedUpdatePlan GeometryDerivedUpdateService::plan(
    const FcDocument& document,
    const std::shared_ptr<FcGeometryObject>& sourceBefore,
    const std::shared_ptr<FcGeometryObject>& sourceAfter,
    const QVector<std::shared_ptr<FcFdsMesh>>& meshes)
{
    GeometryDerivedUpdatePlan result;
    const auto fail = [&result](const QString& message) {
        result.errors.append(message);
        result.updates.clear();
        return result;
    };
    if (!sourceBefore || !sourceAfter || sourceBefore->id() != sourceAfter->id() ||
        !std::dynamic_pointer_cast<FcGeometryObject>(document.findObject(sourceBefore->id())))
        return fail(QStringLiteral("The source geometry must exist in this project and retain its UUID."));

    const QString sourceTag = QStringLiteral("source-uuid:%1").arg(sourceBefore->id());
    QVector<std::shared_ptr<FcFdsNamelist>> existing;
    bool unsupported = false;
    const std::function<void(const FcObject::Ptr&)> collect = [&](const FcObject::Ptr& object) {
        if (!object) return;
        // A source-UUID relation remains a dependency even if its auto tag was
        // removed. Never silently leave such a record with stale geometry.
        if (std::dynamic_pointer_cast<FcFdsObject>(object) && object->tags().contains(sourceTag)) {
            const auto record = std::dynamic_pointer_cast<FcFdsNamelist>(object);
            if (!record || !object->tags().contains(QStringLiteral("firecae:auto-converted")))
                unsupported = true;
            else existing.append(record);
        }
        for (const auto& child : object->children()) collect(child);
    };
    for (const auto& group : document.groups()) collect(group);
    if (unsupported)
        return fail(QStringLiteral("A linked FDS object is not an unmodified automatic conversion; resolve it before editing the source geometry."));
    if (existing.isEmpty()) return result; // Editing never silently converts a source.

    const auto before = FdsBlockConversionService::convert({sourceBefore}, meshes);
    const auto after = FdsBlockConversionService::convert({sourceAfter}, meshes);
    result.warnings = after.warnings;
    if (!before.success() || !after.success()) {
        result.errors = before.errors + after.errors;
        return result;
    }
    if (before.fdsObjects.size() != existing.size() || after.fdsObjects.size() != existing.size())
        return fail(QStringLiteral("This edit changes the number of linked FDS records; reconvert them explicitly before continuing."));

    QSet<QString> usedIds;
    for (qsizetype index = 0; index < before.fdsObjects.size(); ++index) {
        const auto expected = std::dynamic_pointer_cast<FcFdsNamelist>(before.fdsObjects[index]);
        const auto replacement = std::dynamic_pointer_cast<FcFdsNamelist>(after.fdsObjects[index]);
        if (!expected || !replacement || expected->keyword() != replacement->keyword())
            return fail(QStringLiteral("This edit changes the type of a linked FDS record; reconvert it explicitly before continuing."));
        QVector<std::shared_ptr<FcFdsNamelist>> matches;
        for (const auto& actual : existing)
            if (!usedIds.contains(actual->id()) && equivalent(*actual, *expected)) matches.append(actual);
        if (matches.size() != 1)
            return fail(QStringLiteral("Linked FDS records were manually changed or cannot be matched uniquely; resolve them before editing the source geometry."));
        const auto& target = matches.first();
        if (target->isLocked())
            return fail(QStringLiteral("A linked FDS record is locked; unlock it before editing its source geometry."));
        usedIds.insert(target->id());
        result.updates.append({target, target->parameters(), replacement->parameters()});
    }
    return result;
}
