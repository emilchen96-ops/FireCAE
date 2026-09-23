#include "ui/GeometryImportWizard.h"
#include "ui/UiLanguage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextOption>
#include <QTimer>
#include <QWizardPage>

#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const QString& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message.toStdString() << '\n';
    }
}

void settle()
{
    // Process queued layout requests, including height-for-width recalculation.
    for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
}

QRect inWizard(const QWidget* widget, const QWizard& wizard)
{
    return QRect(widget->mapTo(&wizard, QPoint()), widget->size());
}

void checkLabel(QLabel* label, const QWizard& wizard, const QString& context)
{
    check(label && label->isVisible(), context + QStringLiteral(": label visible"));
    if (!label || !label->isVisible()) return;
    check(wizard.rect().contains(inWizard(label, wizard)),
          context + QStringLiteral(": label within wizard"));
    const int requiredHeight = label->hasHeightForWidth()
        ? label->heightForWidth(label->width()) : label->sizeHint().height();
    check(label->height() >= requiredHeight,
          context + QStringLiteral(": full label height (actual %1, required %2, width %3, text %4)")
                        .arg(label->height()).arg(requiredHeight).arg(label->width()).arg(label->text()));
    if (!label->wordWrap()) {
        check(label->contentsRect().width() >= label->fontMetrics().horizontalAdvance(label->text()),
              context + QStringLiteral(": unwrapped text fits"));
    }
}

void checkPage(GeometryImportWizard& wizard, const QSize& requested, const QString& context)
{
    wizard.resize(requested);
    settle();
    check(wizard.width() <= requested.width() && wizard.height() <= requested.height(),
          context + QStringLiteral(": content does not force a larger window (actual %1x%2)")
                        .arg(wizard.width()).arg(wizard.height()));
    for (const QString& text : {wizard.currentPage()->title(), wizard.currentPage()->subTitle()}) {
        if (text.isEmpty()) continue;
        QLabel* matching = nullptr;
        for (QLabel* label : wizard.findChildren<QLabel*>()) {
            if (label->isVisible() && label->text() == text) {
                matching = label;
                break;
            }
        }
        checkLabel(matching, wizard, context + QStringLiteral(": title/subtitle"));
        if (matching) {
            check(!inWizard(matching, wizard).intersects(inWizard(wizard.currentPage(), wizard)),
                  context + QStringLiteral(": header does not overlap page"));
        }
    }
    if (wizard.currentId() == 0 || wizard.currentId() == 1) {
        const QString name = wizard.currentId() == 0
            ? QStringLiteral("GeometryImportFileDescription")
            : QStringLiteral("GeometryImportCoordinateDescription");
        checkLabel(wizard.currentPage()->findChild<QLabel*>(name), wizard,
                   context + QStringLiteral(": page description"));
    }
    for (const QWizard::WizardButton role : {QWizard::BackButton, QWizard::NextButton,
                                          QWizard::FinishButton, QWizard::CancelButton}) {
        QAbstractButton* button = wizard.button(role);
        if (!button || !button->isVisible()) continue;
        check(wizard.rect().contains(inWizard(button, wizard)),
              context + QStringLiteral(": navigation button within wizard"));
        check(button->size().expandedTo(button->minimumSizeHint()) == button->size(),
              context + QStringLiteral(": navigation text has room"));
        check(!inWizard(button, wizard).intersects(inWizard(wizard.currentPage(), wizard)),
              context + QStringLiteral(": navigation outside page content"));
    }
}

void checkSizes(GeometryImportWizard& wizard, const QString& context)
{
    for (const QSize& size : {QSize(640, 480), QSize(960, 700), QSize(760, 560), QSize(640, 480)})
        checkPage(wizard, size, context + QStringLiteral(" %1x%2").arg(size.width()).arg(size.height()));
}

void waitForPreview(GeometryImportWizard& wizard)
{
    auto* cancel = wizard.findChild<QPushButton*>(QStringLiteral("GeometryImportCancelButton"));
    if (!cancel || !cancel->isEnabled()) return;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (!cancel->isEnabled()) loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    check(!cancel->isEnabled(), QStringLiteral("Background preview completed before timeout"));
    settle();
}
}

int main(int argc, char** argv)
{
    // This target never opens a desktop window or takes mouse/keyboard focus.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAELayoutTests"));
    QCoreApplication::setApplicationName(QStringLiteral("GeometryImport"));
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    const QString source = directory.filePath(QStringLiteral("geometry_layout_tetra.stl"));
    QFile fixture(source);
    const QByteArray tetra(
        "solid tetra\n"
        "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n"
        "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 0 0 1\nvertex 1 0 0\nendloop\nendfacet\n"
        "facet normal -1 0 0\nouter loop\nvertex 0 0 0\nvertex 0 1 0\nvertex 0 0 1\nendloop\nendfacet\n"
        "facet normal 1 1 1\nouter loop\nvertex 1 0 0\nvertex 0 0 1\nvertex 0 1 0\nendloop\nendfacet\nendsolid tetra\n");
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write(tetra) != tetra.size()) return 1;
    fixture.close();

    const QFont normalFont = application.font();
    for (const UiLanguage language : {UiLanguage::English, UiLanguage::ChineseSimplified}) {
        UiLanguageManager::setCurrentLanguage(language);
        for (const qreal fontScale : {1.0, 1.5}) {
            QFont font = normalFont;
            if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * fontScale);
            else font.setPixelSize(qRound(font.pixelSize() * fontScale));
            application.setFont(font);
            const QString context = QStringLiteral("%1 font=%2")
                .arg(UiLanguageManager::languageCode(language)).arg(fontScale);
            std::cout << "Checking " << context.toStdString()
                      << ", scale factor " << qgetenv("QT_SCALE_FACTOR").constData() << '\n';

            GeometryImportWizard wizard(source);
            check(wizard.wizardStyle() == QWizard::ModernStyle,
                  context + QStringLiteral(": layout-managed header selected"));
            wizard.show();
            settle();
            checkSizes(wizard, context + QStringLiteral(" file"));
            // A long, unbroken path must stay editable without expanding the window.
            auto* fileEdit = wizard.findChild<QLineEdit*>(QStringLiteral("GeometryImportFileEdit"));
            fileEdit->setText(QStringLiteral("C:/") + QStringLiteral("中文很长的几何文件路径").repeated(30) + QStringLiteral(".stl"));
            checkSizes(wizard, context + QStringLiteral(" long path"));
            fileEdit->setText(source);

            wizard.next();
            check(wizard.currentId() == 1, context + QStringLiteral(": coordinate page reachable"));
            auto* scale = wizard.findChild<QDoubleSpinBox*>(QStringLiteral("GeometryImportScaleSpin"));
            scale->setValue(2.5);
            checkSizes(wizard, context + QStringLiteral(" coordinates"));
            auto* coordinateScroll = wizard.findChild<QScrollArea*>(QStringLiteral("GeometryImportCoordinateScroll"));
            auto* originZ = wizard.findChild<QDoubleSpinBox*>(QStringLiteral("GeometryImportOriginZSpin"));
            check(coordinateScroll && originZ, context + QStringLiteral(": coordinate fields can scroll"));
            if (coordinateScroll && originZ) {
                // Exercise the user's scroll-to-bottom path. ensureWidgetVisible
                // may expose a spin box's input cursor without its entire frame.
                coordinateScroll->verticalScrollBar()->setValue(
                    coordinateScroll->verticalScrollBar()->maximum());
                settle();
                const QRect originRect(originZ->mapTo(coordinateScroll->viewport(), QPoint()), originZ->size());
                check(originRect.top() >= 0 && originRect.bottom() < coordinateScroll->viewport()->height(),
                      context + QStringLiteral(": last coordinate field stays reachable (top %1, bottom %2, viewport %3, scroll %4/%5, content %6)")
                                    .arg(originRect.top()).arg(originRect.bottom())
                                    .arg(coordinateScroll->viewport()->height())
                                    .arg(coordinateScroll->verticalScrollBar()->value())
                                    .arg(coordinateScroll->verticalScrollBar()->maximum())
                                    .arg(coordinateScroll->widget()->height()));
            }
            wizard.next();
            check(wizard.currentId() == 2, context + QStringLiteral(": quality page reachable"));
            // Exercise the taller IFC layout without launching an IFC converter.
            auto* ifcGroup = wizard.findChild<QGroupBox*>(QStringLiteral("IfcImportOptionsGroup"));
            auto* qualityScroll = wizard.findChild<QScrollArea*>(QStringLiteral("GeometryImportQualityScroll"));
            check(ifcGroup && qualityScroll, context + QStringLiteral(": IFC options can scroll"));
            if (ifcGroup && qualityScroll) {
                ifcGroup->show();
                checkSizes(wizard, context + QStringLiteral(" tall IFC options"));
                check(qualityScroll->verticalScrollBar()->maximum() > 0,
                      context + QStringLiteral(": overflowing options have a vertical scrollbar"));
                auto* lastOption = wizard.findChild<QWidget*>(QStringLiteral("IfcImportVisibleCheck"));
                qualityScroll->ensureWidgetVisible(lastOption);
                settle();
                const QRect lastRect(lastOption->mapTo(qualityScroll->viewport(), QPoint()), lastOption->size());
                check(lastRect.top() >= 0 && lastRect.bottom() < qualityScroll->viewport()->height(),
                      context + QStringLiteral(": last IFC option is reachable by scrolling"));
                ifcGroup->hide();
            }
            wizard.next();
            check(wizard.currentId() == 3, context + QStringLiteral(": report page reachable"));
            waitForPreview(wizard);
            check(wizard.importResult().success() && wizard.importResult().quality.triangles == 4,
                  context + QStringLiteral(": real tetrahedron preview succeeds"));
            check(wizard.button(QWizard::FinishButton)->isEnabled(),
                  context + QStringLiteral(": successful preview enables Finish"));
            checkSizes(wizard, context + QStringLiteral(" preview"));

            wizard.currentPage()->setTitle(QStringLiteral("导入预览与质量报告：包含很长中文名称的建筑构件、材料、坐标以及几何质量检查结果，请核对后完成导入"));
            auto* status = wizard.findChild<QLabel*>(QStringLiteral("GeometryImportProgressStage"));
            auto* progress = wizard.findChild<QProgressBar*>(QStringLiteral("GeometryImportProgressBar"));
            auto* cancel = wizard.findChild<QPushButton*>(QStringLiteral("GeometryImportCancelButton"));
            auto* report = wizard.findChild<QPlainTextEdit*>(QStringLiteral("GeometryImportPreviewReport"));
            status->setText(QStringLiteral("IFC 预览已就绪，长中文构件名称与状态说明需要换行显示，以保证进度、取消导入和完成按钮都可以阅读。"));
            report->setPlainText((QStringLiteral("报告：") + QStringLiteral("非常长的中文构件路径").repeated(40) + QLatin1Char('\n')).repeated(100));
            checkSizes(wizard, context + QStringLiteral(" long report title"));
            checkLabel(status, wizard, context + QStringLiteral(": wrapped status"));
            check(inWizard(status, wizard).bottom() < inWizard(progress, wizard).top(),
                  context + QStringLiteral(": status and progress do not overlap"));
            check(!inWizard(progress, wizard).intersects(inWizard(cancel, wizard)),
                  context + QStringLiteral(": progress and cancel do not overlap"));
            check(wizard.rect().contains(inWizard(cancel, wizard)) &&
                      cancel->size().expandedTo(cancel->minimumSizeHint()) == cancel->size(),
                  context + QStringLiteral(": import cancel text has room"));
            check(report->isVisible() && report->viewport()->height() >= report->fontMetrics().lineSpacing() * 2,
                  context + QStringLiteral(": report retains readable space"));
            check(report->verticalScrollBar()->maximum() > 0 && report->horizontalScrollBar()->maximum() == 0,
                  context + QStringLiteral(": long report wraps and scrolls vertically"));

            wizard.back();
            check(wizard.currentId() == 2, context + QStringLiteral(": Back returns to quality page"));
            wizard.next();
            waitForPreview(wizard);
            checkSizes(wizard, context + QStringLiteral(" repeated preview"));
            check(wizard.options().customScale == 2.5 && wizard.importResult().success(),
                  context + QStringLiteral(": resize and navigation preserve import settings"));
            wizard.reject();
        }
    }
    std::cout << "Geometry import layout failures: " << failures << '\n';
    return failures ? 1 : 0;
}
