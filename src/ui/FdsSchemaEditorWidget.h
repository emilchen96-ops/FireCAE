#pragma once

#include "fds/FdsSchema.h"

#include <QWidget>

#include <vector>

class FcFdsNamelist;
class FcProject;
class QLabel;
class QTabWidget;

enum class FdsSchemaEditorMode
{
    Basic,
    Professional
};

class FdsSchemaEditorWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit FdsSchemaEditorWidget(const FcProject* project,
                                   const FcFdsNamelist* currentObject,
                                   FdsSchemaEditorMode mode,
                                   QWidget* parent = nullptr);

    void setNamelist(const QString& keyword,
                     const std::vector<FcFdsParameter>& parameters,
                     const QString& version);

signals:
    void parameterEdited(const QString& key,
                         int parameterKind,
                         const QString& value,
                         const QStringList& targetObjectIds);

private:
    QString currentValue(const QString& key,
                         const std::vector<FcFdsParameter>& parameters) const;
    QStringList currentReferences(
        const QString& key,
        const std::vector<FcFdsParameter>& parameters) const;
    void chooseReferences(const FdsParameterSchema& definition,
                          QLabel* valueLabel,
                          QStringList& selectedIds, quint64 generation);

    const FcProject* m_project = nullptr;
    const FcFdsNamelist* m_currentObject = nullptr;
    QLabel* m_schemaLabel = nullptr;
    QTabWidget* m_categoryTabs = nullptr;
    QString m_keyword;
    quint64 m_editorGeneration = 0;
    FdsSchemaEditorMode m_mode = FdsSchemaEditorMode::Professional;
};
