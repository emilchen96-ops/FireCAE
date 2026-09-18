#pragma once

#include <QSet>
#include <QStringList>

class FcProject;
class GeometryDisplayManager;

struct FdsSceneSyncResult
{
    int businessObjectCount = 0;
    int presentationCount = 0;
    QStringList warnings;
};

class FdsSceneSynchronizer final
{
public:
    explicit FdsSceneSynchronizer(GeometryDisplayManager* displayManager);

    FdsSceneSyncResult rebuild(const FcProject& project);
    void clear();
    const QSet<QString>& displayedObjectIds() const;

private:
    GeometryDisplayManager* m_displayManager = nullptr;
    QSet<QString> m_displayedObjectIds;
};
