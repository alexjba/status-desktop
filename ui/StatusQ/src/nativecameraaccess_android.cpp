#include "StatusQ/nativecameraaccess.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QLoggingCategory>
#include <QPermissions>

namespace {
Q_LOGGING_CATEGORY(lcCamAccess, "status.camaccess")
}

NativeCameraAccess::NativeCameraAccess(QObject* parent)
    : QObject(parent)
    , m_permission(qApp->checkPermission(QCameraPermission{}))
{
    // Back from the settings page: the user may have granted access there
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive)
            refreshCameraPermission();
    });
}

QString NativeCameraAccess::cameraPermission() const
{
    switch (m_permission) {
    case Qt::PermissionStatus::Granted:
        return QStringLiteral("granted");
    case Qt::PermissionStatus::Denied:
        return QStringLiteral("denied");
    default:
        return QStringLiteral("undetermined");
    }
}

void NativeCameraAccess::setPermission(Qt::PermissionStatus status)
{
    if (status == m_permission)
        return;
    m_permission = status;
    emit cameraPermissionChanged();
}

void NativeCameraAccess::refreshCameraPermission()
{
    setPermission(qApp->checkPermission(QCameraPermission{}));
}

void NativeCameraAccess::requestCameraPermission()
{
    qApp->requestPermission(QCameraPermission{}, this,
                            [this](const QPermission& p) { setPermission(p.status()); });
}

// This app's details page, where Android lists its permissions
bool NativeCameraAccess::openCameraSettings()
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return false;
    QJniEnvironment env;
    QJniObject pkg = ctx.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "fromParts",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(QStringLiteral("package")).object<jstring>(),
        pkg.object<jstring>(), static_cast<jstring>(nullptr));
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
        QJniObject::fromString(QStringLiteral("android.settings.APPLICATION_DETAILS_SETTINGS"))
            .object<jstring>(),
        uri.object());
    if (!pkg.isValid() || !uri.isValid() || !intent.isValid()) {
        qCWarning(lcCamAccess) << "could not build the app-settings intent";
        return false;
    }
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x10000000)); // NEW_TASK
    // Raw JNI so an ActivityNotFoundException/SecurityException is still pending to check
    jmethodID startActivity =
        env.findMethod(ctx.objectClass(), "startActivity", "(Landroid/content/Intent;)V");
    if (!startActivity) {
        env.checkAndClearExceptions();
        return false;
    }
    env->CallVoidMethod(ctx.object(), startActivity, intent.object());
    if (env.checkAndClearExceptions()) {
        qCWarning(lcCamAccess) << "could not open the app settings";
        return false;
    }
    return true;
}
