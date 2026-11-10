#include "StatusQ/webwindebug.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlEngine>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#include <malloc.h>
#include <sys/mman.h>
#ifndef M_PURGE_ALL
#define M_PURGE_ALL (-104)
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif
#endif

WebwinDebug::WebwinDebug(QQmlEngine* engine)
    : m_engine(engine)
{
#ifdef Q_OS_ANDROID
    connect(&m_timer, &QTimer::timeout, this, &WebwinDebug::poll);
    m_timer.start(1000);
    qWarning() << "[webwin] polling" << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
}

QObject* WebwinDebug::qmlInstance(QQmlEngine* engine, QJSEngine*)
{
    return new WebwinDebug(engine);
}

void WebwinDebug::poll()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/webwin-step");
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const int step = f.readAll().trimmed().toInt();
    f.close();
    f.remove();
    runStep(step);
}

void WebwinDebug::runStep(int step)
{
#ifdef Q_OS_ANDROID
    QElapsedTimer t;
    t.start();
    switch (step) {
    case 1:
        emit unloadBrowserRequested();
        m_engine->collectGarbage();
        m_engine->trimComponentCache();
        break;
    case 2:
        mallopt(M_PURGE_ALL, 0);
        break;
    case 3: {
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
            QJniObject ctx = QNativeInterface::QAndroidApplication::context();
            QJniObject app = ctx.callObjectMethod("getApplicationContext", "()Landroid/content/Context;");
            app.callMethod<void>("onTrimMemory", "(I)V", jint(15));
            QJniObject::callStaticMethod<void>("java/lang/System", "gc", "()V");
        }).waitForFinished();
        break;
    }
    case 4: {
        QFile maps(QStringLiteral("/proc/self/maps"));
        qint64 bytes = 0;
        int regions = 0;
        if (maps.open(QIODevice::ReadOnly)) {
            while (!maps.atEnd()) {
                const QByteArray line = maps.readLine();
                const QByteArray lower = line.toLower();
                if (!lower.contains("webview") && !lower.contains("trichrome") && !lower.contains("chrome"))
                    continue;
                const int dash = line.indexOf('-');
                const int sp = line.indexOf(' ');
                const quintptr a = line.left(dash).toULongLong(nullptr, 16);
                const quintptr b = line.mid(dash + 1, sp - dash - 1).toULongLong(nullptr, 16);
                if (madvise(reinterpret_cast<void*>(a), b - a, MADV_PAGEOUT) == 0) {
                    bytes += b - a;
                    ++regions;
                }
            }
        }
        qWarning() << "[webwin] MADV_PAGEOUT regions" << regions << "bytes" << bytes;
        break;
    }
    default:
        break;
    }
    qWarning() << "[webwin] step" << step << "took ms" << t.elapsed();
#else
    Q_UNUSED(step)
#endif
}
