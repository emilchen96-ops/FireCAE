#pragma once

#include "fds/FcFdsModel.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <vector>

enum class FdsSchemaValueType
{
    Boolean,
    Integer,
    Real,
    String,
    Enumeration,
    IntegerArray,
    RealArray,
    ObjectReference
};

struct FdsParameterSchema
{
    QString name;
    QString category;
    FdsSchemaValueType type = FdsSchemaValueType::String;
    QString unit;
    QString defaultValue;
    bool required = false;
    std::optional<double> minimum;
    std::optional<double> maximum;
    int arrayLength = 0;
    QStringList enumValues;
    QStringList referenceKeywords;
    QString sinceVersion = QStringLiteral("6.7");
    QString editorHint;
};

struct FdsNamelistSchema
{
    QString keyword;
    QString title;
    QString sinceVersion = QStringLiteral("6.7");
    bool idRequired = false;
    QVector<FdsParameterSchema> parameters;
};

class FdsSchemaRegistry final
{
public:
    static QString defaultVersion();
    static QStringList supportedVersions();
    static QString versionFromRevision(const QString& revision);
    static QString compatibleVersionForRevision(const QString& revision);
    static bool isSupportedVersion(const QString& version);
    static QStringList keywords(const QString& version = defaultVersion());
    static const FdsNamelistSchema* namelist(
        const QString& keyword,
        const QString& version = defaultVersion());
    static const FdsParameterSchema* parameter(
        const QString& keyword,
        const QString& parameterName,
        const QString& version = defaultVersion());
    static std::vector<FcFdsParameter> defaultParameters(
        const QString& keyword,
        const QString& version = defaultVersion());
    static std::vector<FcFdsParameter> recommendedParameters(
        const QString& keyword,
        const QString& version = defaultVersion());
    static QStringList allowedReferenceKeywords(
        const QString& keyword,
        const QString& parameterName,
        const QString& version = defaultVersion());
    static QStringList validate(
        const QString& keyword,
        const QString& fdsId,
        const std::vector<FcFdsParameter>& parameters,
        const QString& version = defaultVersion());
    static QString valueTypeName(FdsSchemaValueType type);
};
