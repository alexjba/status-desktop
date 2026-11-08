#include <StatusReplay/recorder.h>
#include <StatusReplay/statusreplay.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QTimer>

using StatusReplay::Recorder;

namespace {

Recorder* g_recorder = nullptr;

QString recordingDirectory()
{
    QString dir = qEnvironmentVariable("STATUS_RECORD");
    if (dir.isEmpty() || dir == QLatin1String("1"))
        dir = QStringLiteral("Status/recordings/")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(dir).absolutePath();
}

void onContextProperty(void*, const char* name, const void* value)
{
    if (g_recorder)
        g_recorder->setContextProperty(QString::fromUtf8(name), *static_cast<const QVariant*>(value));
}

void onMetacall(void* object, int call, int index, void** argv)
{
    if (g_recorder)
        g_recorder->metacallObserved(static_cast<QObject*>(object), static_cast<QMetaObject::Call>(call), index, argv);
}

} // namespace

extern "C" {

StatusReplayHooks statusreplay_recorder_attach(void* enginePtr)
{
    if (g_recorder)
        return { onContextProperty, onMetacall };

    auto recorder = new Recorder(recordingDirectory());
    if (!recorder->isValid()) {
        qWarning() << "StatusReplay: recording disabled:" << recorder->errorString();
        delete recorder;
        return { nullptr, nullptr };
    }
    g_recorder = recorder;
    qInfo() << "StatusReplay: recording into" << recorder->directory();

    auto engine = static_cast<QQmlEngine*>(enginePtr);
    recorder->setEngine(engine);

    if (auto appEngine = qobject_cast<QQmlApplicationEngine*>(engine)) {
        QObject::connect(appEngine, &QQmlApplicationEngine::objectCreated, appEngine,
            [](QObject* root, const QUrl&) {
                if (!root || !g_recorder)
                    return;
                g_recorder->markLoaded();
                const int delay = qEnvironmentVariableIntValue("STATUS_RECORD_STARTUP_DELAY_MS");
                QTimer::singleShot(delay > 0 ? delay : 5000, qApp, [] {
                    if (g_recorder)
                        g_recorder->markMoment(QStringLiteral("startup"));
                });
            },
            Qt::SingleShotConnection);
    }

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        if (!g_recorder)
            return;
        g_recorder->markMoment(QStringLiteral("exit"));
        g_recorder->flush();
        qInfo() << "StatusReplay: recording written to" << g_recorder->directory();
    });

    return { onContextProperty, onMetacall };
}

void statusreplay_recorder_markMoment(const char* name)
{
    if (g_recorder)
        g_recorder->markMoment(QString::fromUtf8(name));
}
}
