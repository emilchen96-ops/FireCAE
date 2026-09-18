#pragma once

#include "core/FcObject.h"

#include <QDialog>
#include <QList>
#include <QStringList>

class FcDocument;
class QTableWidget;

// Keep navigation identity separate from user-facing labels. Multiple objects
// may legitimately share a display name or a path in the model tree.
struct ObjectReferenceOwner
{
    QString objectUuid;
    QString objectName;
    FcObjectType objectType = FcObjectType::Unknown;
    QString keyword;
    QString fdsId;
    QString objectPath;
    QStringList fieldKeys;
};

QList<ObjectReferenceOwner> collectObjectReferenceOwners(
    const FcDocument& document, const QString& targetUuid);

class ObjectReferenceDialog final : public QDialog
{
public:
    ObjectReferenceDialog(const FcObject& target,
                          const QList<ObjectReferenceOwner>& owners,
                          QWidget* parent = nullptr);

    QString selectedOwnerUuid() const;

private:
    QTableWidget* m_ownerTable = nullptr;
};
