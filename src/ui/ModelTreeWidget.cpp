#include "ui/ModelTreeWidget.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcIfcObject.h"
#include "ui/UiLanguage.h"

#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <QSet>

namespace
{
enum class TreeItemKind
{
    Project,
    Object,
    Continuation
};

constexpr int ObjectIdRole = Qt::UserRole;
constexpr int ItemKindRole = Qt::UserRole + 1;
constexpr int ObjectTypeRole = Qt::UserRole + 2;
constexpr int SearchTextRole = Qt::UserRole + 3;
constexpr int FloorNameRole = Qt::UserRole + 4;
constexpr int LoadedChildCountRole = Qt::UserRole + 5;
constexpr int ContinuationRole = Qt::UserRole + 6;
constexpr int kLazyTreeBatchSize = 100;

bool isLockedForModification(const FcObject* object)
{
    for (const FcObject* current = object; current; current = current->parent()) {
        if (current->isLocked()) return true;
    }
    return false;
}

class ReorderableTreeWidget final : public QTreeWidget
{
public:
    using DropHandler = std::function<void(QTreeWidgetItem*, QTreeWidgetItem*, int)>;
    explicit ReorderableTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {}
    DropHandler dropHandler;

protected:
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* movedItem = currentItem();
        QTreeWidget::dropEvent(event);
        if (!event->isAccepted() || !movedItem || !dropHandler) return;
        QTreeWidgetItem* parentItem = movedItem->parent();
        const int index = parentItem ? parentItem->indexOfChild(movedItem) : -1;
        dropHandler(movedItem, parentItem, index);
    }
};
}

ModelTreeWidget::ModelTreeWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("ModelTreeSearchEdit"));
    m_searchEdit->setPlaceholderText(UiLanguageManager::text(QStringLiteral("Search objects...")));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_typeFilter = new QComboBox(this);
    m_typeFilter->setObjectName(QStringLiteral("ModelTreeTypeFilter"));
    m_typeFilter->addItem(UiLanguageManager::text(QStringLiteral("All Types")), -1);
    m_typeFilter->addItem(UiLanguageManager::text(QStringLiteral("Geometry")),
                          static_cast<int>(FcObjectType::Geometry));
    m_typeFilter->addItem(QStringLiteral("IFC"), -2);
    m_typeFilter->addItem(QStringLiteral("FDS"), -3);
    m_typeFilter->addItem(UiLanguageManager::text(QStringLiteral("Results")), -4);
    layout->addWidget(m_typeFilter);

    m_floorFilter = new QComboBox(this);
    m_floorFilter->setObjectName(QStringLiteral("ModelTreeFloorFilter"));
    m_floorFilter->addItem(
        UiLanguageManager::text(QStringLiteral("All Floors")), QString{});
    layout->addWidget(m_floorFilter);

    m_scenarioLabel = new QLabel(this);
    m_scenarioLabel->setObjectName(QStringLiteral("ModelTreeScenarioLabel"));
    m_scenarioLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_scenarioLabel);

    auto* navigationLayout = new QHBoxLayout;
    navigationLayout->setContentsMargins(2, 0, 2, 0);
    navigationLayout->setSpacing(4);
    navigationLayout->addStretch();
    auto* collapseAllButton = new QToolButton(this);
    collapseAllButton->setObjectName(QStringLiteral("ModelTreeCollapseAllButton"));
    collapseAllButton->setText(
        UiLanguageManager::text(QStringLiteral("Collapse All")));
    collapseAllButton->setToolTip(
        UiLanguageManager::text(QStringLiteral("Collapse the model tree")));
    navigationLayout->addWidget(collapseAllButton);
    auto* locateSelectionButton = new QToolButton(this);
    locateSelectionButton->setObjectName(QStringLiteral("ModelTreeLocateSelectionButton"));
    locateSelectionButton->setText(
        UiLanguageManager::text(QStringLiteral("Locate Selection")));
    locateSelectionButton->setToolTip(
        UiLanguageManager::text(QStringLiteral("Locate the selected object")));
    navigationLayout->addWidget(locateSelectionButton);
    layout->addLayout(navigationLayout);

    auto* reorderableTree = new ReorderableTreeWidget(this);
    m_treeWidget = reorderableTree;
    m_treeWidget->setObjectName(QStringLiteral("ModelTreeObjectTree"));
    m_treeWidget->setHeaderLabel(
        UiLanguageManager::text(QStringLiteral("Name")));
    m_treeWidget->setHeaderHidden(false);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_treeWidget->header()->resizeSection(0, 320);
    m_treeWidget->header()->setMinimumSectionSize(120);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setIndentation(16);
    m_treeWidget->setTextElideMode(Qt::ElideMiddle);
    m_treeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeWidget->setDragEnabled(true);
    m_treeWidget->setAcceptDrops(true);
    m_treeWidget->setDropIndicatorShown(true);
    m_treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
    m_treeWidget->setDefaultDropAction(Qt::MoveAction);
    m_treeWidget->installEventFilter(this);
    layout->addWidget(m_treeWidget);

    connect(m_treeWidget,
            &QTreeWidget::itemSelectionChanged,
            this,
            &ModelTreeWidget::handleSelectionChanged);
    connect(m_treeWidget,
            &QTreeWidget::customContextMenuRequested,
            this,
            &ModelTreeWidget::showContextMenu);
    connect(m_treeWidget,
            &QTreeWidget::itemDoubleClicked,
            this,
            &ModelTreeWidget::handleItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem* item) { loadNextChildBatch(item); });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_typeFilter, &QComboBox::currentIndexChanged, this, [this]() { applyFilter(); });
    connect(m_floorFilter, &QComboBox::currentIndexChanged,
            this, [this]() { applyFilter(); });
    connect(collapseAllButton, &QToolButton::clicked, this, [this]() {
        m_treeWidget->collapseAll();
        if (m_treeWidget->topLevelItemCount() > 0) {
            m_treeWidget->topLevelItem(0)->setExpanded(true);
        }
        if (QScrollBar* scrollBar = m_treeWidget->horizontalScrollBar()) {
            scrollBar->setValue(0);
        }
    });
    connect(locateSelectionButton, &QToolButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_treeWidget->currentItem();
        if (!item && m_treeWidget->topLevelItemCount() > 0) {
            item = m_treeWidget->topLevelItem(0);
        }
        for (QTreeWidgetItem* parentItem = item ? item->parent() : nullptr;
             parentItem;
             parentItem = parentItem->parent()) {
            parentItem->setExpanded(true);
        }
        scrollToItemKeepingHierarchyVisible(item);
    });
    reorderableTree->dropHandler = [this](QTreeWidgetItem* item,
                                          QTreeWidgetItem* parent,
                                          int index) {
        if (!item || !parent || index < 0) { refresh(); return; }
        const QString objectId = item->data(0, ObjectIdRole).toString();
        const QString parentId = parent->data(0, ObjectIdRole).toString();
        if (objectId.isEmpty() || parentId.isEmpty()) { refresh(); return; }
        emit reparentObjectRequested(objectId, parentId, index);
    };
}

QString ModelTreeWidget::selectedObjectId() const
{
    if (!m_treeWidget) {
        return {};
    }

    QTreeWidgetItem* item = m_treeWidget->currentItem();
    if (!item ||
        static_cast<TreeItemKind>(item->data(0, ItemKindRole).toInt()) !=
            TreeItemKind::Object) {
        return {};
    }
    return item->data(0, ObjectIdRole).toString();
}

QStringList ModelTreeWidget::selectedObjectIds() const
{
    QStringList ids;
    if (!m_treeWidget) return ids;
    for (QTreeWidgetItem* item : m_treeWidget->selectedItems()) {
        if (static_cast<TreeItemKind>(item->data(0, ItemKindRole).toInt()) ==
            TreeItemKind::Object) {
            const QString id = item->data(0, ObjectIdRole).toString();
            if (!id.isEmpty()) ids.append(id);
        }
    }
    return ids;
}

bool ModelTreeWidget::selectObjectById(const QString& objectId, bool notify)
{
    return selectObjectsByIds({objectId}, notify);
}

bool ModelTreeWidget::selectObjectsByIds(const QStringList& objectIds, bool notify)
{
    if (objectIds.isEmpty()) {
        clearSelection();
        return false;
    }
    QList<QTreeWidgetItem*> items;
    for (const QString& objectId : objectIds) {
        QTreeWidgetItem* item = m_itemsByObjectId.value(objectId, nullptr);
        if (!item && m_project && m_project->document()) {
            const FcObject::Ptr object = m_project->document()->findObject(objectId);
            item = ensureItemForObject(object.get());
        }
        if (item) {
            items.append(item);
        }
    }
    if (items.isEmpty()) {
        clearSelection();
        return false;
    }

    const QSignalBlocker blocker(m_treeWidget);
    m_treeWidget->clearSelection();
    for (QTreeWidgetItem* item : items) {
        for (QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent()) {
            parent->setExpanded(true);
        }
        item->setSelected(true);
    }
    m_treeWidget->setCurrentItem(items.constFirst(), 0, QItemSelectionModel::NoUpdate);
    scrollToItemKeepingHierarchyVisible(items.constFirst());
    if (notify) {
        const QStringList ids = selectedObjectIds();
        emit objectsSelected(ids);
        if (ids.size() == 1) emit objectSelected(ids.constFirst());
    }
    return true;
}

void ModelTreeWidget::clearSelection()
{
    const QSignalBlocker blocker(m_treeWidget);
    m_treeWidget->clearSelection();
    m_treeWidget->setCurrentItem(nullptr);
}

void ModelTreeWidget::setIsolationActive(bool active)
{
    m_isolationActive = active;
}

bool ModelTreeWidget::isIsolationActive() const
{
    return m_isolationActive;
}

void ModelTreeWidget::setProject(FcProject* project)
{
    m_project = project;
    refresh();
}

void ModelTreeWidget::retranslateUi()
{
    m_searchEdit->setPlaceholderText(
        UiLanguageManager::text(QStringLiteral("Search objects...")));
    if (m_treeWidget) {
        m_treeWidget->setHeaderLabel(
            UiLanguageManager::text(QStringLiteral("Name")));
    }
    m_typeFilter->setItemText(0, UiLanguageManager::text(QStringLiteral("All Types")));
    m_typeFilter->setItemText(1, UiLanguageManager::text(QStringLiteral("Geometry")));
    m_typeFilter->setItemText(4, UiLanguageManager::text(QStringLiteral("Results")));
    if (m_floorFilter && m_floorFilter->count() > 0) {
        m_floorFilter->setItemText(
            0, UiLanguageManager::text(QStringLiteral("All Floors")));
    }
    if (m_scenarioLabel && m_project) {
        const FcScenario* scenario = m_project->activeScenario();
        m_scenarioLabel->setText(
            UiLanguageManager::text(QStringLiteral("Scenario: %1"))
                .arg(scenario ? scenario->name
                              : UiLanguageManager::text(QStringLiteral("Default"))));
    }
    if (QToolButton* button = findChild<QToolButton*>(
            QStringLiteral("ModelTreeCollapseAllButton"))) {
        button->setText(UiLanguageManager::text(QStringLiteral("Collapse All")));
        button->setToolTip(
            UiLanguageManager::text(QStringLiteral("Collapse the model tree")));
    }
    if (QToolButton* button = findChild<QToolButton*>(
            QStringLiteral("ModelTreeLocateSelectionButton"))) {
        button->setText(UiLanguageManager::text(QStringLiteral("Locate Selection")));
        button->setToolTip(
            UiLanguageManager::text(QStringLiteral("Locate the selected object")));
    }
}

void ModelTreeWidget::refresh()
{
    const QString previousSelection = selectedObjectId();
    const QString previousFloor = m_floorFilter
                                      ? m_floorFilter->currentData().toString()
                                      : QString{};
    QSet<QString> expandedObjectIds;
    std::function<void(QTreeWidgetItem*)> rememberExpandedItems =
        [&](QTreeWidgetItem* item) {
            if (!item) return;
            if (item->isExpanded()) {
                const QString id = item->data(0, ObjectIdRole).toString();
                if (!id.isEmpty()) expandedObjectIds.insert(id);
            }
            for (int index = 0; index < item->childCount(); ++index) {
                rememberExpandedItems(item->child(index));
            }
        };
    for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index) {
        rememberExpandedItems(m_treeWidget->topLevelItem(index));
    }

    const QSignalBlocker blocker(m_treeWidget);
    m_itemsByObjectId.clear();
    m_treeWidget->clear();

    if (!m_project || !m_project->document()) {
        if (m_floorFilter) {
            const QSignalBlocker floorBlocker(m_floorFilter);
            m_floorFilter->clear();
            m_floorFilter->addItem(
                UiLanguageManager::text(QStringLiteral("All Floors")), QString{});
        }
        if (m_scenarioLabel) m_scenarioLabel->clear();
        return;
    }

    QSet<QString> floorNames;
    const std::function<void(const FcObject::Ptr&)> collectFloors =
        [&](const FcObject::Ptr& object) {
            if (!object) return;
            if (object->type() == FcObjectType::Floor && !object->name().trimmed().isEmpty()) {
                floorNames.insert(object->name().trimmed());
            }
            if (!object->floorName().trimmed().isEmpty()) {
                floorNames.insert(object->floorName().trimmed());
            }
            for (const FcObject::Ptr& child : object->children()) collectFloors(child);
        };
    for (const auto& group : m_project->document()->groups()) collectFloors(group);
    if (m_floorFilter) {
        const QSignalBlocker floorBlocker(m_floorFilter);
        m_floorFilter->clear();
        m_floorFilter->addItem(
            UiLanguageManager::text(QStringLiteral("All Floors")), QString{});
        QStringList orderedFloors = floorNames.values();
        orderedFloors.sort(Qt::CaseInsensitive);
        for (const QString& floorName : orderedFloors) {
            m_floorFilter->addItem(floorName, floorName);
        }
        const int floorIndex = m_floorFilter->findData(previousFloor);
        m_floorFilter->setCurrentIndex(floorIndex >= 0 ? floorIndex : 0);
    }
    if (m_scenarioLabel) {
        const FcScenario* scenario = m_project->activeScenario();
        m_scenarioLabel->setText(
            UiLanguageManager::text(QStringLiteral("Scenario: %1"))
                .arg(scenario ? scenario->name
                              : UiLanguageManager::text(QStringLiteral("Default"))));
    }

    auto* projectItem = new QTreeWidgetItem(m_treeWidget, {m_project->name()});
    projectItem->setData(0, ItemKindRole, static_cast<int>(TreeItemKind::Project));

    for (const auto& group : m_project->document()->groups()) {
        appendObjectItem(projectItem, group);
    }

    projectItem->setExpanded(true);
    for (const QString& objectId : expandedObjectIds) {
        if (QTreeWidgetItem* item = m_itemsByObjectId.value(objectId, nullptr)) {
            item->setExpanded(true);
        }
    }
    applyFilter();

    QTreeWidgetItem* previousItem = m_itemsByObjectId.value(previousSelection, nullptr);
    if (previousItem) {
        for (QTreeWidgetItem* parent = previousItem->parent(); parent;
             parent = parent->parent()) {
            parent->setExpanded(true);
        }
        m_treeWidget->setCurrentItem(previousItem);
        previousItem->setSelected(true);
        scrollToItemKeepingHierarchyVisible(previousItem);
    }
}

void ModelTreeWidget::appendObjectItem(QTreeWidgetItem* parentItem,
                                       const std::shared_ptr<FcObject>& object)
{
    if (!parentItem || !object) {
        return;
    }

    const QString displayName = object->type() == FcObjectType::Group
                                    ? UiLanguageManager::text(object->name())
                                    : object->name();
    auto* item = new QTreeWidgetItem(parentItem, {displayName});
    item->setData(0, ItemKindRole, static_cast<int>(TreeItemKind::Object));
    item->setData(0, ObjectIdRole, object->id());
    item->setData(0, ObjectTypeRole, static_cast<int>(object->type()));
    QString effectiveFloor = object->floorName().trimmed();
    if (object->type() == FcObjectType::Floor) effectiveFloor = object->name().trimmed();
    if (effectiveFloor.isEmpty() && parentItem) {
        effectiveFloor = parentItem->data(0, FloorNameRole).toString();
    }
    item->setData(0, FloorNameRole, effectiveFloor);
    QStringList searchable{displayName, object->floorName()};
    searchable.append(object->tags());
    if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
        searchable.append(ifc->ifcClass());
        searchable.append(ifc->globalId());
        searchable.append(ifc->description());
    }
    item->setData(0, SearchTextRole, searchable.join(QLatin1Char('\n')));
    QStringList tooltipLines{displayName};
    if (!object->isVisible()) {
        item->setForeground(0, QColor(145, 145, 145));
        tooltipLines.append(UiLanguageManager::text(QStringLiteral("Hidden")));
    }
    if (object->isLocked()) {
        item->setText(0, QStringLiteral("[L] %1").arg(displayName));
        tooltipLines.append(UiLanguageManager::text(QStringLiteral("Locked")));
    }
    const FcScenario* scenario = m_project ? m_project->activeScenario() : nullptr;
    if (scenario && scenario->disabledObjectIds.contains(object->id())) {
        item->setText(0, QStringLiteral("[Off] %1").arg(item->text(0)));
        item->setForeground(0, QColor(155, 155, 155));
        tooltipLines.append(
            QStringLiteral("Disabled in scenario: %1").arg(scenario->name));
    }
    item->setToolTip(0, tooltipLines.join(QLatin1Char('\n')));
    m_itemsByObjectId.insert(object->id(), item);

    const int childCount = static_cast<int>(object->children().size());
    item->setData(0, LoadedChildCountRole, 0);
    if (childCount > kLazyTreeBatchSize) {
        auto* continuation = new QTreeWidgetItem(
            item,
            {UiLanguageManager::text(QStringLiteral("Load next %1 objects..."))
                 .arg(qMin(kLazyTreeBatchSize, childCount))});
        continuation->setData(0, ItemKindRole,
                              static_cast<int>(TreeItemKind::Continuation));
        continuation->setData(0, ContinuationRole, true);
        continuation->setToolTip(
            0, UiLanguageManager::text(
                   QStringLiteral("Double-click to load the next objects")));
    } else {
        for (const FcObject::Ptr& child : object->children()) {
            appendObjectItem(item, child);
        }
        item->setData(0, LoadedChildCountRole, childCount);
    }
}

void ModelTreeWidget::loadNextChildBatch(QTreeWidgetItem* parentItem,
                                         int minimumChildIndex)
{
    if (!parentItem || !m_project || !m_project->document()) return;
    if (static_cast<TreeItemKind>(parentItem->data(0, ItemKindRole).toInt()) !=
        TreeItemKind::Object) {
        return;
    }
    const FcObject::Ptr object = m_project->document()->findObject(
        parentItem->data(0, ObjectIdRole).toString());
    if (!object) return;
    const int total = static_cast<int>(object->children().size());
    int loaded = parentItem->data(0, LoadedChildCountRole).toInt();
    if (loaded >= total) return;

    for (int index = parentItem->childCount() - 1; index >= 0; --index) {
        QTreeWidgetItem* child = parentItem->child(index);
        if (child->data(0, ContinuationRole).toBool()) {
            delete parentItem->takeChild(index);
        }
    }
    const int requestedEnd = minimumChildIndex >= 0
                                 ? minimumChildIndex + 1
                                 : loaded + kLazyTreeBatchSize;
    const int end = qMin(total, qMax(loaded + kLazyTreeBatchSize, requestedEnd));
    for (int index = loaded; index < end; ++index) {
        appendObjectItem(parentItem,
                         object->children()[static_cast<std::size_t>(index)]);
    }
    loaded = end;
    parentItem->setData(0, LoadedChildCountRole, loaded);
    if (loaded < total) {
        const int remainingBatch = qMin(kLazyTreeBatchSize, total - loaded);
        auto* continuation = new QTreeWidgetItem(
            parentItem,
            {UiLanguageManager::text(QStringLiteral("Load next %1 objects..."))
                 .arg(remainingBatch)});
        continuation->setData(0, ItemKindRole,
                              static_cast<int>(TreeItemKind::Continuation));
        continuation->setData(0, ContinuationRole, true);
        continuation->setToolTip(
            0, UiLanguageManager::text(
                   QStringLiteral("Double-click to load the next objects")));
    }
}

void ModelTreeWidget::materializeAllChildren(QTreeWidgetItem* parentItem)
{
    if (!parentItem) return;
    if (static_cast<TreeItemKind>(parentItem->data(0, ItemKindRole).toInt()) ==
        TreeItemKind::Object) {
        loadNextChildBatch(parentItem, std::numeric_limits<int>::max() / 2);
    }
    const QList<QTreeWidgetItem*> children = [&]() {
        QList<QTreeWidgetItem*> result;
        for (int index = 0; index < parentItem->childCount(); ++index) {
            QTreeWidgetItem* child = parentItem->child(index);
            if (!child->data(0, ContinuationRole).toBool()) result.append(child);
        }
        return result;
    }();
    for (QTreeWidgetItem* child : children) materializeAllChildren(child);
}

QTreeWidgetItem* ModelTreeWidget::ensureItemForObject(const FcObject* object)
{
    if (!object) return nullptr;
    if (QTreeWidgetItem* existing = m_itemsByObjectId.value(object->id(), nullptr)) {
        return existing;
    }
    QTreeWidgetItem* parentItem = ensureItemForObject(object->parent());
    if (!parentItem) return nullptr;
    const auto& siblings = object->parent()->children();
    for (int index = 0; index < static_cast<int>(siblings.size()); ++index) {
        if (siblings[static_cast<std::size_t>(index)].get() == object) {
            loadNextChildBatch(parentItem, index);
            break;
        }
    }
    return m_itemsByObjectId.value(object->id(), nullptr);
}

void ModelTreeWidget::handleSelectionChanged()
{
    const QList<QTreeWidgetItem*> selectedItems = m_treeWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        emit selectionCleared();
        return;
    }
    const QStringList ids = selectedObjectIds();
    if (ids.isEmpty()) {
        handleSelectedItem(selectedItems.constFirst());
        return;
    }
    emit objectsSelected(ids);
    if (ids.size() == 1) emit objectSelected(ids.constFirst());
}

bool ModelTreeWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_treeWidget && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        clearSelection();
        emit selectionCleared();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ModelTreeWidget::handleItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item || !m_project || !m_project->document()) return;
    if (item->data(0, ContinuationRole).toBool()) {
        QTreeWidgetItem* parentItem = item->parent();
        loadNextChildBatch(parentItem);
        if (parentItem) parentItem->setExpanded(true);
        return;
    }
    const QString objectId = item->data(0, ObjectIdRole).toString();
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (object && !isLockedForModification(object.get()) &&
        std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        emit editObjectRequested(objectId);
    }
}

void ModelTreeWidget::handleSelectedItem(QTreeWidgetItem* selectedItem)
{
    if (!selectedItem || !m_project) {
        emit selectionCleared();
        return;
    }

    const auto kind = static_cast<TreeItemKind>(selectedItem->data(0, ItemKindRole).toInt());
    if (kind == TreeItemKind::Project) {
        emit projectSelected();
        return;
    }

    const QString objectId = selectedItem->data(0, ObjectIdRole).toString();
    if (objectId.isEmpty()) {
        emit selectionCleared();
        return;
    }
    emit objectSelected(objectId);
}

void ModelTreeWidget::showContextMenu(const QPoint& position)
{
    if (!m_project || !m_project->document()) {
        return;
    }

    QTreeWidgetItem* item = m_treeWidget->itemAt(position);
    QMenu menu(this);
    QAction* restoreAction = menu.addAction(UiLanguageManager::text(
        m_isolationActive ? QStringLiteral("Exit Isolation")
                          : QStringLiteral("Show All Objects")));
    connect(restoreAction, &QAction::triggered, this,
            &ModelTreeWidget::restoreVisibilityRequested);

    if (!item ||
        static_cast<TreeItemKind>(item->data(0, ItemKindRole).toInt()) !=
            TreeItemKind::Object) {
        menu.exec(m_treeWidget->viewport()->mapToGlobal(position));
        return;
    }

    const QString objectId = item->data(0, ObjectIdRole).toString();
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (!object) return;
    const bool lockedForModification = isLockedForModification(object.get());

    m_treeWidget->setCurrentItem(item);
    menu.addSeparator();
    QAction* visibilityAction = menu.addAction(UiLanguageManager::text(
        object->isVisible() ? QStringLiteral("Hide") : QStringLiteral("Show")));
    connect(visibilityAction, &QAction::triggered, this,
            [this, objectId, object]() {
                emit visibilityChangeRequested(objectId, !object->isVisible());
            });
    QAction* isolateAction = menu.addAction(
        UiLanguageManager::text(QStringLiteral("Show Only")));
    connect(isolateAction, &QAction::triggered, this, [this, objectId]() {
        emit isolateObjectRequested(objectId);
    });
    QAction* normalAction = menu.addAction(
        UiLanguageManager::text(QStringLiteral("View Normal to Plane")));
    normalAction->setToolTip(UiLanguageManager::text(
        QStringLiteral("Use an orthographic camera normal to the selected planar object")));
    connect(normalAction, &QAction::triggered, this, [this, objectId]() {
        emit viewNormalRequested(objectId);
    });
    QAction* lockAction = menu.addAction(UiLanguageManager::text(
        object->isLocked() ? QStringLiteral("Unlock") : QStringLiteral("Lock")));
    connect(lockAction, &QAction::triggered, this, [this, objectId, object]() {
        emit lockChangeRequested(objectId, !object->isLocked());
    });
    menu.addSeparator();
    if (std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        QAction* editAction = menu.addAction(
            UiLanguageManager::text(QStringLiteral("Edit FDS Object...")));
        editAction->setEnabled(!lockedForModification);
        connect(editAction, &QAction::triggered, this, [this, objectId]() {
            emit editObjectRequested(objectId);
        });
        QAction* duplicateAction = menu.addAction(
            UiLanguageManager::text(QStringLiteral("Duplicate Object")));
        duplicateAction->setEnabled(!lockedForModification);
        connect(duplicateAction, &QAction::triggered, this, [this, objectId]() {
            emit duplicateObjectRequested(objectId);
        });
        QAction* moveUpAction = menu.addAction(
            UiLanguageManager::text(QStringLiteral("Move Up")));
        moveUpAction->setEnabled(!lockedForModification);
        connect(moveUpAction, &QAction::triggered, this, [this, objectId]() {
            emit moveObjectRequested(objectId, -1);
        });
        QAction* moveDownAction = menu.addAction(
            UiLanguageManager::text(QStringLiteral("Move Down")));
        moveDownAction->setEnabled(!lockedForModification);
        connect(moveDownAction, &QAction::triggered, this, [this, objectId]() {
            emit moveObjectRequested(objectId, 1);
        });
        menu.addSeparator();
    }
    const bool canDelete = object->type() == FcObjectType::IfcModel ||
                           object->type() == FcObjectType::Geometry ||
                           object->type() == FcObjectType::Folder ||
                           object->type() == FcObjectType::Floor ||
                           object->type() == FcObjectType::ResultCase ||
                           static_cast<bool>(std::dynamic_pointer_cast<FcFdsNamelist>(object));
    if (canDelete) {
        QAction* deleteAction = menu.addAction(
            object->type() == FcObjectType::IfcModel
                ? UiLanguageManager::text(QStringLiteral("Delete Imported IFC"))
                : UiLanguageManager::text(QStringLiteral("Delete Object")));
        deleteAction->setEnabled(!lockedForModification);
        connect(deleteAction, &QAction::triggered, this, [this, objectId]() {
            emit deleteObjectRequested(objectId);
        });
    }
    menu.exec(m_treeWidget->viewport()->mapToGlobal(position));
}

void ModelTreeWidget::applyFilter()
{
    if (!m_treeWidget) return;
    const bool filtering =
        (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty()) ||
        (m_typeFilter && m_typeFilter->currentData().toInt() != -1) ||
        (m_floorFilter && !m_floorFilter->currentData().toString().isEmpty());
    if (filtering) {
        const QSignalBlocker blocker(m_treeWidget);
        for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index) {
            materializeAllChildren(m_treeWidget->topLevelItem(index));
        }
        m_filterWasActive = true;
    } else if (m_filterWasActive) {
        m_filterWasActive = false;
        refresh();
        return;
    }
    for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index) {
        QTreeWidgetItem* root = m_treeWidget->topLevelItem(index);
        root->setHidden(!filterItem(root));
    }
}

void ModelTreeWidget::scrollToItemKeepingHierarchyVisible(QTreeWidgetItem* item)
{
    if (!m_treeWidget || !item) return;
    m_treeWidget->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    // Qt may horizontally auto-scroll a deeply nested IFC item into view. That
    // hides the parent hierarchy and makes the tree appear to be cut off.
    if (QScrollBar* scrollBar = m_treeWidget->horizontalScrollBar()) {
        scrollBar->setValue(0);
    }
}

bool ModelTreeWidget::filterItem(QTreeWidgetItem* item)
{
    if (!item) return false;
    bool childMatches = false;
    for (int index = 0; index < item->childCount(); ++index) {
        QTreeWidgetItem* child = item->child(index);
        const bool matches = filterItem(child);
        child->setHidden(!matches);
        childMatches = childMatches || matches;
    }
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString{};
    const bool textMatches = query.isEmpty() ||
        item->data(0, SearchTextRole).toString().contains(query, Qt::CaseInsensitive) ||
        item->text(0).contains(query, Qt::CaseInsensitive);
    const int filter = m_typeFilter ? m_typeFilter->currentData().toInt() : -1;
    const int type = item->data(0, ObjectTypeRole).toInt();
    bool typeMatches = filter == -1 ||
        static_cast<TreeItemKind>(item->data(0, ItemKindRole).toInt()) == TreeItemKind::Project;
    if (filter >= 0) typeMatches = type == filter;
    else if (filter == -2) typeMatches = type == static_cast<int>(FcObjectType::IfcModel) ||
                                        type == static_cast<int>(FcObjectType::IfcEntity);
    else if (filter == -3) typeMatches = type >= static_cast<int>(FcObjectType::Mesh) &&
                                        type <= static_cast<int>(FcObjectType::Output);
    else if (filter == -4) typeMatches = type >= static_cast<int>(FcObjectType::Result) &&
                                        type <= static_cast<int>(FcObjectType::ResultFile);
    const QString selectedFloor = m_floorFilter
                                      ? m_floorFilter->currentData().toString()
                                      : QString{};
    const bool floorMatches = selectedFloor.isEmpty() ||
        item->data(0, FloorNameRole).toString().compare(
            selectedFloor, Qt::CaseInsensitive) == 0 ||
        static_cast<TreeItemKind>(item->data(0, ItemKindRole).toInt()) ==
            TreeItemKind::Project;
    const bool filtering = !query.isEmpty() || filter != -1 ||
                           !selectedFloor.isEmpty();
    if (filtering && childMatches) item->setExpanded(true);
    return childMatches || (textMatches && typeMatches && floorMatches);
}
