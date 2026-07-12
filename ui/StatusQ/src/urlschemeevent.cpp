#include <StatusQ/urlschemeevent.h>

using namespace Status;

#include <QFileOpenEvent>

#if defined(Q_OS_ANDROID)
    #include <jni.h>
    #include <QJniObject>
#endif // Q_OS_ANDROID

#include <QDesktopServices>
#include <QGuiApplication>

void UrlSchemeEvent::registerUrlHandler()
{
#if defined(Q_OS_IOS)
    // On iOS with Qt 6, universal links are delivered via UISceneDelegate
    // which calls QDesktopServices::openUrl() instead of posting QFileOpenEvent.
    // Register a handler for "https" scheme to intercept these URLs.
    QDesktopServices::setUrlHandler("https", this, "handleUrl");
    QDesktopServices::setUrlHandler("status-app", this, "handleUrl");
#endif
}

void UrlSchemeEvent::handleUrl(const QUrl& url)
{
    emit urlActivated(url.toString());
}

bool UrlSchemeEvent::eventFilter(QObject* obj, QEvent* event)
{
#ifdef Q_OS_MACOS
    if (event->type() == QEvent::FileOpen)
    {
        QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
        if(fileEvent)
        {
            emit urlActivated(fileEvent->url().toString());
        }
    }
#endif

    return QObject::eventFilter(obj, event);
}

void UrlSchemeEvent::watchApplicationState()
{
    // Pending intake slot contract (src/app/core/intake/pending_intake_slot.nim):
    // the host takes the payload when it comes to the foreground. This covers
    // the wake-less iOS fallback for an already-running app — the share
    // extension wrote the slot but its unsupported openURL wake failed or was
    // dropped. Harmless elsewhere: consuming an inactive/empty slot is a no-op.
    // Under QCoreApplication (unit tests) there is no application state; skip.
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state == Qt::ApplicationActive)
                        emit appForegrounded();
                });
    }
}

void UrlSchemeEvent::emitAppForegroundedToQt()
{
    emit appForegrounded();
}

void UrlSchemeEvent::emitDeepLinkToQt(const QString& url)
{
    if (url.isEmpty()) return;

    emit urlActivated(url);
}

void UrlSchemeEvent::emitShareTextToQt(const QString& text)
{
    if (text.isEmpty()) return;

    emit shareTextActivated(text);
}

static UrlSchemeEvent* g_urlSchemeEventInstance = nullptr;

void UrlSchemeEvent::setInstance(UrlSchemeEvent* instance)
{
    g_urlSchemeEventInstance = instance;
}

#ifdef Q_OS_ANDROID
extern "C" JNIEXPORT void JNICALL
Java_app_status_mobile_StatusQtActivity_passDeepLinkToQt(JNIEnv* /*env*/, jclass /*clazz*/, jstring url)
{
    const QString deepLink = QJniObject(url).toString();
    if (deepLink.isEmpty()) return;

    if (g_urlSchemeEventInstance) {
        g_urlSchemeEventInstance->emitDeepLinkToQt(deepLink);
    }
}

// Share-target hand-off: text/links shared from another app. Kept separate
// from the URL channel — a shared link must launch the share flow, not URL
// routing.
extern "C" JNIEXPORT void JNICALL
Java_app_status_mobile_StatusQtActivity_passShareTextToQt(JNIEnv* /*env*/, jclass /*clazz*/, jstring text)
{
    const QString shareText = QJniObject(text).toString();
    if (shareText.isEmpty()) return;

    if (g_urlSchemeEventInstance) {
        g_urlSchemeEventInstance->emitShareTextToQt(shareText);
    }
}
#endif
