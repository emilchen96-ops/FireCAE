#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

enum class FcObjectType
{
    Unknown,
    Group,
    Geometry,
    IfcModel,
    IfcEntity,
    Mesh,
    MeshMultiplier,
    SimulationParameter,
    Species,
    Obstruction,
    Vent,
    Material,
    Surface,
    Reaction,
    Particle,
    Property,
    Table,
    Ramp,
    Device,
    Control,
    HVAC,
    InitialCondition,
    Output,
    Result,
    ResultCase,
    ResultFile,
    Folder,
    Floor
};

class FcObject
{
public:
    using Ptr = std::shared_ptr<FcObject>;

    explicit FcObject(const QString& name, FcObjectType type);
    virtual ~FcObject();

    FcObject(const FcObject&) = delete;
    FcObject& operator=(const FcObject&) = delete;
    FcObject(FcObject&&) = delete;
    FcObject& operator=(FcObject&&) = delete;

    const QString& id() const;

    const QString& name() const;
    void setName(const QString& name);

    FcObjectType type() const;

    bool isVisible() const;
    void setVisible(bool visible);

    bool isLocked() const;
    void setLocked(bool locked);

    const QString& floorName() const;
    void setFloorName(const QString& floorName);

    const QStringList& tags() const;
    void setTags(const QStringList& tags);

    FcObject* parent() const;
    const std::vector<Ptr>& children() const;

    bool addChild(const Ptr& child);
    bool insertChild(const Ptr& child, std::size_t index);
    bool removeChild(const QString& id);
    bool moveChild(const QString& id, std::size_t newIndex);
    void clearChildren();
    Ptr findChild(const QString& id, bool recursive = true) const;

    // Project deserialization is the only supported reason to replace an ID.
    // The value is validated as a UUID and duplicate detection is performed by
    // the project serializer before the restored tree is exposed to callers.
    bool restorePersistentId(const QString& id);

private:
    bool wouldCreateCycle(const FcObject* child) const;

    QString m_id;
    QString m_name;
    FcObjectType m_type = FcObjectType::Unknown;
    bool m_visible = true;
    bool m_locked = false;
    QString m_floorName;
    QStringList m_tags;
    FcObject* m_parent = nullptr;
    std::vector<Ptr> m_children;
};
