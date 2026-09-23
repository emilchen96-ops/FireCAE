#pragma once

#include <QHash>
#include <QByteArray>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <memory>

class FcObject;
class FcProject;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QComboBox;
class QLabel;
class QEvent;

class ModelTreeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ModelTreeWidget(QWidget* parent = nullptr);

    void setProject(FcProject* project);
    void retranslateUi();
    void refresh();
    QString selectedObjectId() const;
    QStringList selectedObjectIds() const;
    bool selectObjectById(const QString& objectId, bool notify = false);
    bool selectObjectsByIds(const QStringList& objectIds, bool notify = false);
    void clearSelection();
    void setIsolationActive(bool active);
    bool isIsolationActive() const;
    QByteArray saveViewState() const;
    bool restoreViewState(const QByteArray& state);

signals:
    void projectSelected();
    void objectSelected(const QString& objectId);
    void objectsSelected(const QStringList& objectIds);
    void editObjectRequested(const QString& objectId);
    void duplicateObjectRequested(const QString& objectId);
    void moveObjectRequested(const QString& objectId, int offset);
    void deleteObjectRequested(const QString& objectId);
    void visibilityChangeRequested(const QString& objectId, bool visible);
    void isolateObjectRequested(const QString& objectId);
    void restoreVisibilityRequested();
    void viewNormalRequested(const QString& objectId);
    void lockChangeRequested(const QString& objectId, bool locked);
    void reparentObjectRequested(const QString& objectId,
                                 const QString& parentObjectId,
                                 int childIndex);
    void selectionCleared();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void appendObjectItem(QTreeWidgetItem* parentItem,
                          const std::shared_ptr<FcObject>& object);
    void loadNextChildBatch(QTreeWidgetItem* parentItem,
                            int minimumChildIndex = -1);
    void materializeAllChildren(QTreeWidgetItem* parentItem);
    QTreeWidgetItem* ensureItemForObject(const FcObject* object);
    void handleSelectionChanged();
    void handleItemDoubleClicked(QTreeWidgetItem* item, int column);
    void handleSelectedItem(QTreeWidgetItem* selectedItem);
    void showContextMenu(const QPoint& position);
    void applyFilter();
    bool isFiltering() const;
    bool filterItem(QTreeWidgetItem* item);
    void scrollToItemKeepingHierarchyVisible(QTreeWidgetItem* item);
    void updateNameColumnWidth();

    struct TreeState
    {
        QSet<QString> expandedIds;
        QHash<QString, int> loadedChildCounts;
        bool projectExpanded = true;
    };
    TreeState captureTreeState() const;
    void restoreTreeState(const TreeState& state);

    FcProject* m_project = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_typeFilter = nullptr;
    QComboBox* m_floorFilter = nullptr;
    QLabel* m_scenarioLabel = nullptr;
    QHash<QString, QTreeWidgetItem*> m_itemsByObjectId;
    bool m_filterWasActive = false;
    bool m_isolationActive = false;
    TreeState m_unfilteredTreeState;
    int m_unfilteredVerticalPosition = 0;
    int m_preferredNameColumnWidth = 320;
    bool m_nameColumnUserSized = false;
    bool m_updatingNameColumn = false;
};
