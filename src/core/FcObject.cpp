#include "core/FcObject.h"

#include <QUuid>

#include <algorithm>

FcObject::FcObject(const QString& name, FcObjectType type)
    : m_id(QUuid::createUuid().toString(QUuid::WithBraces))
    , m_name(name)
    , m_type(type)
{
}

FcObject::~FcObject()
{
    for (const Ptr& child : m_children) {
        if (child && child->m_parent == this) {
            child->m_parent = nullptr;
        }
    }
}

const QString& FcObject::id() const
{
    return m_id;
}

const QString& FcObject::name() const
{
    return m_name;
}

void FcObject::setName(const QString& name)
{
    m_name = name;
}

FcObjectType FcObject::type() const
{
    return m_type;
}

bool FcObject::isVisible() const
{
    return m_visible;
}

void FcObject::setVisible(bool visible)
{
    m_visible = visible;
}

bool FcObject::isLocked() const
{
    return m_locked;
}

void FcObject::setLocked(bool locked)
{
    m_locked = locked;
}

const QString& FcObject::floorName() const
{
    return m_floorName;
}

void FcObject::setFloorName(const QString& floorName)
{
    m_floorName = floorName.trimmed();
}

const QStringList& FcObject::tags() const
{
    return m_tags;
}

void FcObject::setTags(const QStringList& tags)
{
    m_tags.clear();
    for (const QString& tag : tags) {
        const QString normalized = tag.trimmed();
        if (!normalized.isEmpty() && !m_tags.contains(normalized, Qt::CaseInsensitive)) {
            m_tags.append(normalized);
        }
    }
}

FcObject* FcObject::parent() const
{
    return m_parent;
}

const std::vector<FcObject::Ptr>& FcObject::children() const
{
    return m_children;
}

bool FcObject::addChild(const Ptr& child)
{
    if (!child || child.get() == this || child->m_parent != nullptr || wouldCreateCycle(child.get())) {
        return false;
    }

    const auto duplicate = std::find_if(
        m_children.cbegin(), m_children.cend(), [&child](const Ptr& existing) {
            return existing && existing->id() == child->id();
        });
    if (duplicate != m_children.cend()) {
        return false;
    }

    child->m_parent = this;
    m_children.push_back(child);
    return true;
}

bool FcObject::insertChild(const Ptr& child, std::size_t index)
{
    if (index >= m_children.size()) {
        return addChild(child);
    }
    if (!child || child.get() == this || child->m_parent != nullptr ||
        wouldCreateCycle(child.get())) {
        return false;
    }
    const auto duplicate = std::find_if(
        m_children.cbegin(), m_children.cend(), [&child](const Ptr& existing) {
            return existing && existing->id() == child->id();
        });
    if (duplicate != m_children.cend()) return false;
    child->m_parent = this;
    m_children.insert(m_children.begin() +
                          static_cast<std::vector<Ptr>::difference_type>(index),
                      child);
    return true;
}

bool FcObject::removeChild(const QString& id)
{
    const auto child = std::find_if(
        m_children.begin(), m_children.end(), [&id](const Ptr& candidate) {
            return candidate && candidate->id() == id;
        });
    if (child == m_children.end()) {
        return false;
    }

    (*child)->m_parent = nullptr;
    m_children.erase(child);
    return true;
}

bool FcObject::moveChild(const QString& id, std::size_t newIndex)
{
    const auto current = std::find_if(
        m_children.begin(), m_children.end(), [&id](const Ptr& candidate) {
            return candidate && candidate->id() == id;
        });
    if (current == m_children.end()) {
        return false;
    }
    const Ptr child = *current;
    m_children.erase(current);
    if (newIndex >= m_children.size()) {
        m_children.push_back(child);
    } else {
        m_children.insert(m_children.begin() +
                              static_cast<std::vector<Ptr>::difference_type>(newIndex),
                          child);
    }
    return true;
}

void FcObject::clearChildren()
{
    for (const Ptr& child : m_children) {
        if (child && child->m_parent == this) {
            child->m_parent = nullptr;
        }
    }
    m_children.clear();
}

FcObject::Ptr FcObject::findChild(const QString& id, bool recursive) const
{
    for (const Ptr& child : m_children) {
        if (!child) {
            continue;
        }
        if (child->id() == id) {
            return child;
        }
        if (recursive) {
            Ptr match = child->findChild(id, true);
            if (match) {
                return match;
            }
        }
    }
    return {};
}

bool FcObject::restorePersistentId(const QString& id)
{
    const QUuid uuid(id);
    if (uuid.isNull()) {
        return false;
    }
    m_id = uuid.toString(QUuid::WithBraces);
    return true;
}

bool FcObject::wouldCreateCycle(const FcObject* child) const
{
    for (const FcObject* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent()) {
        if (ancestor == child) {
            return true;
        }
    }
    return false;
}
