#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class FcObject;
class FcProject;
class QFormLayout;
class QLabel;
class QScrollArea;

class PropertiesWidget final : public QWidget
{
public:
    explicit PropertiesWidget(QWidget* parent = nullptr);

    void clear();
    void showProject(const FcProject* project);
    void showObject(const FcObject* object);

private:
    void setDetails(const QList<QPair<QString, QString>>& rows);

    QLabel* m_emptyLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_detailsWidget = nullptr;
    QFormLayout* m_form = nullptr;
};
