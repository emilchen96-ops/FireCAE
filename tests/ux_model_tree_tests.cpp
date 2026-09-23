#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "geometry/FcIfcObject.h"
#include "ui/ModelTreeWidget.h"
#include "ui/UiLanguage.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!condition) ++failures;
}

void settle()
{
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
}

QTreeWidgetItem* itemFor(QTreeWidget* tree, const QString& id)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == id) return *it;
    }
    return nullptr;
}

QSet<QString> expandedIds(QTreeWidget* tree)
{
    QSet<QString> ids;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        const QString id = (*it)->data(0, Qt::UserRole).toString();
        if ((*it)->isExpanded() && !id.isEmpty()) ids.insert(id);
    }
    return ids;
}

void evidence(QWidget& widget, const QString& name)
{
    const QString directory = qEnvironmentVariable("FIRECAE_UX_EVIDENCE_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    check(widget.grab().save(QDir(directory).filePath(name + QStringLiteral(".png"))),
          "offscreen model-tree evidence image saved");
}
}

int main(int argc, char** argv)
{
    // This fixture exercises Qt widgets only, without a native viewer, desktop
    // input, user settings, or an IFC converter/solver process.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE.UxModelTree.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("UxModelTree"));
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "isolated model-tree settings directory exists");
    if (!settingsDirectory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    qunsetenv("FIRECAE_UI_LANGUAGE");
    UiLanguageManager::setCurrentLanguage(UiLanguage::ChineseSimplified);
    UiLanguageManager::initialize();
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
#endif
    FcProject project(QStringLiteral("体育馆深层中文 IFC 模型树验收"));
    auto root = std::make_shared<FcIfcObject>(
        QStringLiteral("体育馆参考模型"), QStringLiteral("IfcProject"),
        QStringLiteral("ifc-project"), true);
    project.document()->geometryGroup()->addChild(root);
    FcObject::Ptr parent = root;
    QStringList branchIds{root->id()};
    for (int depth = 0; depth < 8; ++depth) {
        auto branch = std::make_shared<FcIfcObject>(
            QStringLiteral("体育馆建筑分区与楼层第%1层").arg(depth + 1),
            QStringLiteral("IfcBuildingStorey"), QStringLiteral("storey-%1").arg(depth));
        branch->setFloorName(QStringLiteral("首层"));
        parent->addChild(branch);
        parent = branch;
        branchIds.append(branch->id());
    }
    QList<FcObject::Ptr> leaves;
    for (int index = 0; index < 245; ++index) {
        // The first two intentionally share their entire display name. UUIDs
        // must continue to distinguish them after selection and tree rebuilds.
        const QString name = QStringLiteral(
            "基本墙：常规混凝土隔墙：体育馆东侧疏散通道与观众看台之间的防火分隔墙：编号%1")
                                 .arg(index <= 1 ? 0 : index);
        auto leaf = std::make_shared<FcIfcObject>(
            name, QStringLiteral("IfcWall"), QStringLiteral("wall-%1").arg(index));
        leaf->setFloorName(QStringLiteral("首层"));
        parent->addChild(leaf);
        leaves.append(leaf);
    }

    ModelTreeWidget widget;
    widget.resize(330, 640);
    widget.setProject(&project);
    widget.show();
    settle();
    auto* tree = widget.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
    auto* search = widget.findChild<QLineEdit*>(QStringLiteral("ModelTreeSearchEdit"));
    auto* types = widget.findChild<QComboBox*>(QStringLiteral("ModelTreeTypeFilter"));
    auto* floors = widget.findChild<QComboBox*>(QStringLiteral("ModelTreeFloorFilter"));
    if (!tree || !search || !types || !floors) return 1;
    check(tree->indentation() < 16, "deep hierarchy consumes less horizontal indentation");
    check(tree->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
          "horizontal scrollbar is available when name content exceeds viewport");
    check(itemFor(tree, parent->id())->childCount() == 1 &&
              !itemFor(tree, leaves.at(0)->id()),
          "245-child IFC branch starts lazy with a continuation row");
    tree->topLevelItem(0)->child(0)->setExpanded(true);
    for (const QString& id : branchIds) itemFor(tree, id)->setExpanded(true);
    settle();
    check(itemFor(tree, parent->id())->childCount() == 101 &&
              itemFor(tree, leaves.at(99)->id()) && !itemFor(tree, leaves.at(100)->id()),
          "expanding a lazy IFC branch loads exactly the first 100 objects");
    check(widget.selectObjectById(leaves.at(0)->id()), "deep IFC leaf is selected by UUID");
    settle();
    QTreeWidgetItem* first = itemFor(tree, leaves.at(0)->id());
    check(first && first->toolTip(0).contains(leaves.at(0)->name()),
          "full Chinese name remains available in tooltip");
    check(tree->columnWidth(0) > tree->viewport()->width() &&
              tree->horizontalScrollBar()->maximum() > 0,
          "automatic name width exposes full long names through horizontal scrolling");
    const int textStart = tree->visualItemRect(first).left();
    const int narrowAvailable = tree->viewport()->width() - textStart;
    evidence(widget, QStringLiteral("tree-deep-chinese-narrow"));
    widget.resize(630, 640);
    settle();
    const int wideAvailable = tree->viewport()->width() - tree->visualItemRect(first).left();
    check(wideAvailable >= narrowAvailable + 250,
          "widening the panel provides substantially more visible name space");
    evidence(widget, QStringLiteral("tree-deep-chinese-wide"));
    widget.resize(330, 640);
    settle();
    tree->header()->resizeSection(0, 1500);
    tree->horizontalScrollBar()->setValue(210);
    settle();
    check(widget.selectObjectsByIds({leaves.at(0)->id(), leaves.at(1)->id()}),
          "same-name IFC leaves can be selected independently by UUID");
    settle();
    check(widget.selectedObjectIds().size() == 2 &&
              tree->horizontalScrollBar()->value() == 210 && tree->columnWidth(0) == 1500,
          "multi-selection preserves the manual name width and horizontal position");
    widget.findChild<QToolButton*>(QStringLiteral("ModelTreeLocateSelectionButton"))->click();
    settle();
    check(tree->horizontalScrollBar()->value() == 210,
          "locate selection preserves the user's horizontal position");
    check(widget.selectObjectsByIds({leaves.at(0)->id(), leaves.at(179)->id()}),
          "selection materializes a later lazy object using its UUID");
    settle();
    const int loadedCount = itemFor(tree, parent->id())->childCount();
    const QSet<QString> expandedBeforeRefresh = expandedIds(tree);
    const int yBeforeRefresh = tree->verticalScrollBar()->value();
    widget.refresh();
    settle();
    check(widget.selectedObjectIds().contains(leaves.at(0)->id()) &&
              widget.selectedObjectIds().contains(leaves.at(179)->id()) &&
              widget.selectedObjectIds().size() == 2,
          "refresh preserves the full UUID multi-selection including a lazy object");
    check(expandedIds(tree) == expandedBeforeRefresh &&
              itemFor(tree, parent->id())->childCount() == loadedCount,
          "refresh preserves expanded hierarchy and the previously loaded lazy batch");
    check(tree->horizontalScrollBar()->value() == 210 &&
              tree->verticalScrollBar()->value() == yBeforeRefresh &&
              tree->columnWidth(0) == 1500,
          "refresh preserves user column width and both scroll offsets");

    // Collapse a branch before filtering; clear must restore this choice and
    // must not permanently materialize every child merely because search did.
    itemFor(tree, parent->id())->setExpanded(false);
    settle();
    const QSet<QString> expandedBeforeFilter = expandedIds(tree);
    widget.refresh();
    settle();
    check(expandedIds(tree) == expandedBeforeFilter,
          "refresh does not reopen a collapsed ancestor of the current selection");
    search->setText(QStringLiteral("编号244"));
    settle();
    check(itemFor(tree, leaves.at(244)->id()) &&
              !itemFor(tree, leaves.at(244)->id())->isHidden() &&
              itemFor(tree, leaves.at(0)->id())->isHidden(),
          "search reaches an unmaterialized IFC child and retains the real ancestors");
    check(tree->horizontalScrollBar()->value() == 210 && tree->columnWidth(0) == 1500,
          "search leaves manual horizontal viewing state unchanged");
    widget.refresh();
    settle();
    search->clear();
    settle();
    check(expandedIds(tree) == expandedBeforeFilter,
          "clearing search after refresh restores prior expanded and collapsed branches");
    check(itemFor(tree, parent->id())->childCount() == loadedCount &&
              !itemFor(tree, leaves.at(244)->id()),
          "clearing search after refresh restores the original lazy loading extent");
    check(widget.selectedObjectIds().size() == 2 &&
              tree->horizontalScrollBar()->value() == 210,
          "filter and refresh retain hidden selected UUIDs and horizontal offset");
    types->setCurrentIndex(types->findData(-2));
    floors->setCurrentIndex(floors->findData(QStringLiteral("首层")));
    settle();
    check(itemFor(tree, leaves.at(244)->id()) &&
              !itemFor(tree, leaves.at(244)->id())->isHidden(),
          "IFC type and floor filters still reach the complete business hierarchy");
    types->setCurrentIndex(0);
    floors->setCurrentIndex(0);
    settle();
    check(expandedIds(tree) == expandedBeforeFilter && tree->columnWidth(0) == 1500 &&
              tree->horizontalScrollBar()->value() == 210,
          "clearing type/floor filters restores expansion without overriding user width");

    itemFor(tree, parent->id())->setExpanded(true);
    settle();
    check(itemFor(tree, parent->id())->childCount() == loadedCount,
          "reopening a branch does not unexpectedly load another lazy batch");
    QTreeWidgetItem* continuation = itemFor(tree, parent->id())->child(loadedCount - 1);
    tree->itemDoubleClicked(continuation, 0);
    settle();
    check(itemFor(tree, parent->id())->childCount() == 245 &&
              itemFor(tree, leaves.at(244)->id()),
          "explicit continuation activation still loads the remaining objects");

    const QByteArray state = widget.saveViewState();
    ModelTreeWidget reopened;
    reopened.resize(330, 640);
    reopened.setProject(&project);
    reopened.show();
    settle();
    check(reopened.restoreViewState(state), "saved column preference restores in a new widget");
    settle();
    auto* reopenedTree = reopened.findChild<QTreeWidget*>();
    check(reopenedTree->columnWidth(0) == 1500 &&
              reopenedTree->horizontalScrollBar()->value() == 210,
          "reopened view restores the explicit name width and horizontal position");
    check(!reopened.restoreViewState(QByteArrayLiteral("invalid")) &&
              reopenedTree->columnWidth(0) == 1500,
          "invalid saved view state is ignored without changing the current width");
    reopened.selectObjectById(leaves.at(244)->id());
    reopened.refresh();
    settle();
    check(reopenedTree->columnWidth(0) == 1500 &&
              reopenedTree->horizontalScrollBar()->value() == 210,
          "restored explicit width survives lazy selection and refresh");
    ModelTreeWidget startupView;
    startupView.resize(330, 640);
    check(startupView.restoreViewState(state),
          "view preference can be restored before a project or first show");
    startupView.setProject(&project);
    startupView.show();
    settle();
    auto* startupTree = startupView.findChild<QTreeWidget*>();
    check(startupTree->columnWidth(0) == 1500 &&
              startupTree->horizontalScrollBar()->value() == 210,
          "startup layout preserves restored name width and horizontal offset");
    std::cout << "Model tree layout failures: " << failures << std::endl;
    return failures ? 1 : 0;
}
