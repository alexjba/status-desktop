#pragma once

#include <QObject>
#include <QQuickItem>

// On Android the QtMultimedia ffmpeg backend and the FFmpeg libs are not loaded at startup
// (see mobile/scripts/android/qt-deploy-lazy.txt). Qt dlopen()s the plugin on first use but
// does not run its JNI_OnLoad, which hands the JavaVM to FFmpeg (MediaCodec) and registers
// the camera natives. ensureLoaded() loads the plugin and runs it once; it must be called
// before the first QtMultimedia object is created. No-op on other platforms.
namespace MediaBackend {
void ensureLoaded();
}

class MediaBackendUtils : public QObject
{
    Q_OBJECT
public:
    explicit MediaBackendUtils(QObject* parent = nullptr);

    Q_INVOKABLE void ensureLoaded() const;
};

// Root item for components that declare QtMultimedia objects: the root is constructed
// before its children, so the backend is ready when they are created.
class MediaHost : public QQuickItem
{
    Q_OBJECT
public:
    explicit MediaHost(QQuickItem* parent = nullptr);
};
