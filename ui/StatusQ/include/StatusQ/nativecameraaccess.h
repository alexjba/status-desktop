#pragma once

#include <QObject>
#include <QString>

// Camera permission backend for NimQtCamera's CameraAccess on Android, where
// nim-qtcamera (QtMultimedia) is not linked: the same properties and slots as its
// `nimQtCameraBackend` context property, minus the frame taps (the native camera
// feeds the QR scanner there).
class NativeCameraAccess : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString cameraPermission READ cameraPermission NOTIFY cameraPermissionChanged)
    Q_PROPERTY(bool canOpenCameraSettings READ canOpenCameraSettings CONSTANT)
public:
    explicit NativeCameraAccess(QObject* parent = nullptr);

    QString cameraPermission() const;
    bool canOpenCameraSettings() const { return true; }

    Q_INVOKABLE void refreshCameraPermission();
    Q_INVOKABLE void requestCameraPermission();
    Q_INVOKABLE bool openCameraSettings();

signals:
    void cameraPermissionChanged();

private:
    void setPermission(Qt::PermissionStatus status);

    Qt::PermissionStatus m_permission;
};
