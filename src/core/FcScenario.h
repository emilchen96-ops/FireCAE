#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct FcScenarioParameterOverride
{
    QString objectId;
    QString parameterKey;
    QString value;
    QStringList targetObjectIds;
    bool reference = false;
};

struct FcScenario
{
    QString id;
    QString name;
    QString chid;
    QString outputDirectory;
    QString solverBackendId = QStringLiteral("fds.serial.cpu");
    int processCount = 1;
    QSet<QString> disabledObjectIds;
    QVector<FcScenarioParameterOverride> parameterOverrides;

    static FcScenario create(const QString& name, const QString& chid = {});
    bool isValid() const;
    const FcScenarioParameterOverride* overrideFor(
        const QString& objectId, const QString& parameterKey) const;
};
