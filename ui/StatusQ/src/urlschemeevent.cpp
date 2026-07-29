#include <StatusQ/urlschemeevent.h>

using namespace Status;

#include <QFileOpenEvent>

#if defined(Q_OS_ANDROID)
    #include <jni.h>
    #include <QJniObject>
#endif // Q_OS_ANDROID

#include <QDebug>
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
    // appBackgrounded/appForegrounded drive the iOS pausable-services
    // bridge (src/app/core/services_pause_bridge.nim): pause on suspension,
    // resume — and media-server rebind — on return to the foreground.
    // Under QCoreApplication (unit tests) there is no application state; skip.
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    switch (state) {
                    case Qt::ApplicationActive:
                        emit appForegrounded();
                        break;
                    case Qt::ApplicationSuspended:
                        emit appBackgrounded();
                        break;
                    case Qt::ApplicationInactive:
                        // Transient dip (share sheets, system alerts, app
                        // switcher) — deliberately not a backgrounding.
                        break;
                    default:
                        qWarning() << "Unhandled application state:" << state;
                        break;
                    }
                });
    }
}

void UrlSchemeEvent::emitAppForegroundedToQt()
{
    emit appForegrounded();
}

void UrlSchemeEvent::emitAppBackgroundedToQt()
{
    emit appBackgrounded();
}

void UrlSchemeEvent::emitDeepLinkToQt(const QString& url)
{
    if (url.isEmpty()) return;

    emit urlActivated(url);
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
#endif
