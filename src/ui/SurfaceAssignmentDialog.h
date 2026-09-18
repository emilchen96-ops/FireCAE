#pragma once

#include <QDialog>

class FcProject;
class QCheckBox;
class QComboBox;
class QLabel;

class SurfaceAssignmentDialog final : public QDialog
{
public:
    explicit SurfaceAssignmentDialog(FcProject* project, int targetCount,
                                     QWidget* parent = nullptr);

    bool copyMode() const;
    QString sourceGeometryId() const;
    QString defaultChoice() const;
    QString faceChoice() const;
    QString faceSurfaceId() const;
    bool replaceOverrides() const;
    bool copyTopologyOverrides() const;

    static QString keepValue();
    static QString clearAllFacesValue();
    static QString allTopologyFacesValue();

protected:
    void accept() override;

private:
    void populateObjects();
    void updateMode();
    void updateSurfacePreview();

    FcProject* m_project = nullptr;
    QComboBox* m_mode = nullptr;
    QComboBox* m_source = nullptr;
    QComboBox* m_defaultSurface = nullptr;
    QComboBox* m_face = nullptr;
    QComboBox* m_faceSurface = nullptr;
    QCheckBox* m_replaceOverrides = nullptr;
    QCheckBox* m_copyTopology = nullptr;
    QLabel* m_preview = nullptr;
};
