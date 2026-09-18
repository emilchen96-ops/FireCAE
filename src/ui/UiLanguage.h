#pragma once

#include <QString>

enum class UiLanguage
{
    English,
    ChineseSimplified
};

class UiLanguageManager final
{
public:
    // Call after QApplication and its organization/application names exist.
    // Idempotent; also installs Qt's standard-widget translations.
    static void initialize();
    static UiLanguage currentLanguage();
    static void setCurrentLanguage(UiLanguage language);
    static QString text(const QString& englishText);
    static QString languageCode(UiLanguage language);
};
