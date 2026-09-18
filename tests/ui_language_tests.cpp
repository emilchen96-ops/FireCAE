#include "ui/UiLanguage.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>

namespace
{
int failures = 0;
void trace(const char* stage)
{
    std::fprintf(stderr, "UI_LANGUAGE_STAGE: %s\n", stage);
    std::fflush(stderr);
}
void check(bool condition, const char* message)
{
    QTextStream(condition ? stdout : stderr)
        << (condition ? "PASS: " : "FAIL: ") << message << Qt::endl;
    if (!condition) ++failures;
}
QString withoutMnemonic(QString value)
{
    return value.remove(QLatin1Char('&'));
}
void deliverLanguageChanges()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents();
}
}

int main(int argc, char** argv)
{
    // This acceptance test never shows or activates a window. CTest also sets
    // QT_QPA_PLATFORM=offscreen, keeping the user's keyboard/mouse untouched.
    trace("before QApplication");
    QApplication app(argc, argv);
    trace("after QApplication");
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAEUiLanguageTests"));
    QCoreApplication::setApplicationName(QStringLiteral("IsolatedTranslationTest"));
    trace("before isolated settings directory");
    QTemporaryDir directory;
    check(directory.isValid(), "isolated settings directory exists");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    qunsetenv("FIRECAE_UI_LANGUAGE");
    trace("before selecting English and translator initialization");
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);
    UiLanguageManager::initialize();
    trace("after translator initialization");

    QDialogButtonBox existing(QDialogButtonBox::Save | QDialogButtonBox::Discard |
                             QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    QLineEdit userName(QStringLiteral("Save"));
    check(withoutMnemonic(existing.button(QDialogButtonBox::Save)->text()) ==
              QStringLiteral("Save"), "initial English Save label");
    trace("before selecting Chinese");
    UiLanguageManager::setCurrentLanguage(UiLanguage::ChineseSimplified);
    trace("before delivering Chinese LanguageChange events");
    deliverLanguageChanges();
    trace("after delivering Chinese LanguageChange events");
    const QList<QPair<QDialogButtonBox::StandardButton, QString>> expected = {
        {QDialogButtonBox::Save, QStringLiteral("保存")},
        {QDialogButtonBox::Discard, QStringLiteral("不保存")},
        {QDialogButtonBox::Cancel, QStringLiteral("取消")},
        {QDialogButtonBox::Ok, QStringLiteral("确定")}
    };
    for (const auto& item : expected) {
        check(withoutMnemonic(existing.button(item.first)->text()) == item.second,
              "existing standard button updates to Chinese");
    }
    QMessageBox box(QMessageBox::Warning, QStringLiteral("工程尚未保存"),
                    UiLanguageManager::text(QStringLiteral(
                        "The current project has unsaved changes. Save them before %1?"))
                        .arg(UiLanguageManager::text(QStringLiteral("opening another project"))),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    check(box.text() == QStringLiteral("当前工程有未保存的修改。是否在打开另一个工程前保存？"),
          "unsaved-project template and operation are translated together");
    check(withoutMnemonic(box.button(QMessageBox::Save)->text()) == QStringLiteral("保存"),
          "QMessageBox Save is Chinese");
    check(withoutMnemonic(box.button(QMessageBox::Discard)->text()) == QStringLiteral("不保存"),
          "QMessageBox Discard is Chinese");
    check(withoutMnemonic(box.button(QMessageBox::Cancel)->text()) == QStringLiteral("取消"),
          "QMessageBox Cancel is Chinese");
    check(UiLanguageManager::text(QStringLiteral("Time (s)")) == QStringLiteral("时间（s）"),
          "fire preview time axis is translated");
    check(UiLanguageManager::text(QStringLiteral("HRR (kW)")) == QStringLiteral("热释放率（kW）"),
          "fire preview HRR axis is translated");
    check(userName.text() == QStringLiteral("Save"), "user-entered names are untouched");
    check(QCoreApplication::translate("UserObject", "Save") == QStringLiteral("Save"),
          "fallback translator is restricted to Qt standard-button contexts");
    check(UiLanguageManager::text(QStringLiteral("SURF_ID")) == QStringLiteral("SURF_ID"),
          "FDS keyword is untouched");
    check(QSettings().value(QStringLiteral("interface/language")).toString() ==
              QStringLiteral("zh_CN"), "Chinese selection is saved for next startup");

    UiLanguageManager::initialize();
    UiLanguageManager::initialize();
    deliverLanguageChanges();
    check(withoutMnemonic(existing.button(QDialogButtonBox::Ok)->text()) == QStringLiteral("确定"),
          "repeated initialization is idempotent");
    UiLanguageManager::setCurrentLanguage(UiLanguage::English);
    deliverLanguageChanges();
    check(withoutMnemonic(existing.button(QDialogButtonBox::Save)->text()) == QStringLiteral("Save"),
          "existing standard buttons return to English");
    check(withoutMnemonic(box.button(QMessageBox::Cancel)->text()) == QStringLiteral("Cancel"),
          "existing message-box button returns to English");
    check(UiLanguageManager::text(QStringLiteral("Time (s)")) == QStringLiteral("Time (s)"),
          "English application strings are unchanged");
    qputenv("FIRECAE_UI_LANGUAGE", "zh_CN");
    UiLanguageManager::initialize();
    deliverLanguageChanges();
    check(UiLanguageManager::currentLanguage() == UiLanguage::ChineseSimplified &&
              withoutMnemonic(existing.button(QDialogButtonBox::Save)->text()) == QStringLiteral("保存"),
          "startup language override updates both application and Qt controls");
    QTextStream(stdout) << "Translation failures: " << failures << Qt::endl;
    trace("test body complete; starting local destruction");
    return failures ? 1 : 0;
}
