#include "StatusQ/mediabackend.h"

#ifdef Q_OS_ANDROID
#include <QElapsedTimer>
#include <QFile>
#include <QJniEnvironment>
#include <QLoggingCategory>

#include <dlfcn.h>
#include <jni.h>

#include <mutex>

namespace {
Q_LOGGING_CATEGORY(lcMedia, "status.media")

#if defined(__aarch64__)
constexpr auto abi = "arm64-v8a";
#elif defined(__arm__)
constexpr auto abi = "armeabi-v7a";
#elif defined(__x86_64__)
constexpr auto abi = "x86_64";
#else
constexpr auto abi = "x86";
#endif

bool avcodecMapped()
{
    QFile maps(QStringLiteral("/proc/self/maps"));
    return maps.open(QIODevice::ReadOnly) && maps.readAll().contains("libavcodec.so");
}
} // namespace

void MediaBackend::ensureLoaded()
{
    static std::once_flag once;
    std::call_once(once, [] {
        QElapsedTimer timer;
        timer.start();
        const bool mappedBefore = avcodecMapped();
        const auto name = QByteArray("libplugins_multimedia_ffmpegmediaplugin_") + abi + ".so";
        void* handle = dlopen(name.constData(), RTLD_NOW);
        if (!handle) {
            qCWarning(lcMedia) << "dlopen" << name << "failed:" << dlerror();
            return;
        }
        using OnLoad = jint (*)(JavaVM*, void*);
        auto onLoad = reinterpret_cast<OnLoad>(dlsym(handle, "JNI_OnLoad"));
        QJniEnvironment env; // attaches this thread, JNI_OnLoad needs GetEnv to succeed
        const jint rc = onLoad ? onLoad(QJniEnvironment::javaVM(), nullptr) : JNI_ERR;
        qCInfo(lcMedia) << "ffmpeg backend loaded on first use in" << timer.elapsed() << "ms,"
                        << "JNI_OnLoad" << (rc == JNI_ERR ? "failed" : "ok")
                        << ", avcodec mapped before:" << mappedBefore;
    });
}
#else
void MediaBackend::ensureLoaded() {}
#endif

MediaBackendUtils::MediaBackendUtils(QObject* parent)
    : QObject(parent)
{}

void MediaBackendUtils::ensureLoaded() const
{
    MediaBackend::ensureLoaded();
}

MediaHost::MediaHost(QQuickItem* parent)
    : QQuickItem(parent)
{
    MediaBackend::ensureLoaded();
}
