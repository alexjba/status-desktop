#include <StatusReplay/fingerprint.h>
#include <StatusReplay/media.h>
#include <StatusReplay/probe.h>
#include <StatusReplay/settings.h>
#include <StatusReplay/recorder.h>
#include <StatusReplay/scriptrunner.h>
#include <StatusReplay/statusreplay.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QWindow>
#include <QTimer>

#include <utility>

using StatusReplay::Recorder;

#ifdef STATUSREPLAY_WITH_STATUSQ
extern "C" void statusq_setNetworkReplyTap(void (*tap)(void* reply));
#endif

namespace {

Recorder* g_recorder = nullptr;
StatusReplay::MediaRecorder* g_media = nullptr;

QString recordingDirectory()
{
    QString dir = qEnvironmentVariable("STATUS_RECORD");
    if (dir.isEmpty() || dir == QLatin1String("1"))
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/recordings/")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(dir).absolutePath();
}

// Scripted runs (or STATUS_RECORD_SCREENSHOTS=1) keep a picture of every moment next to its
// snapshot, to compare with replay. Hand-driven recordings don't, unless asked: the window can
// show more than the recording itself carries.
void saveMomentScreenshot(const QString& moment)
{
    if (!g_recorder
        || (!qEnvironmentVariableIsSet("STATUS_RECORD_SCRIPT")
            && qEnvironmentVariableIntValue("STATUS_RECORD_SCREENSHOTS") != 1))
        return;
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto quickWindow = qobject_cast<QQuickWindow*>(window); quickWindow && quickWindow->isVisible()) {
            QDir dir(g_recorder->directory());
            dir.mkpath(QStringLiteral("screenshots"));
            quickWindow->grabWindow().save(dir.filePath(QStringLiteral("screenshots/%1.png").arg(moment)));
            return;
        }
    }
}

void markMoment(const QString& moment)
{
    g_recorder->markMoment(moment);
    saveMomentScreenshot(moment);
}

// Writes the exit moment once. Scripted quits write it before quitting: on Android the
// process ends without aboutToQuit.
void finishRecording()
{
    static bool finished = false;
    if (!g_recorder || std::exchange(finished, true))
        return;
    markMoment(QStringLiteral("exit"));
    g_recorder->flush();
    qInfo() << "StatusReplay: recording written to" << g_recorder->directory();
}

void quitRecording()
{
    finishRecording();
    QCoreApplication::quit();
}

// Unattended recordings: STATUS_RECORD_SCRIPT (ui/replay/scripts/*.json) drives the UI after
// the startup moment, e.g. onboarding a fresh profile with the test password. Test credentials
// only. STATUS_RECORD_QUIT_AFTER_SCRIPT_MS then quits; a failed script exits with code 3.
void runScript()
{
    const QString path = qEnvironmentVariable("STATUS_RECORD_SCRIPT");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "StatusReplay: cannot read script" << path;
        qApp->exit(3);
        return;
    }
    auto runner = new StatusReplay::ScriptRunner(QJsonDocument::fromJson(file.readAll()).array(), qApp);
    QObject::connect(runner, &StatusReplay::ScriptRunner::momentRequested, qApp, [](const QString& name) {
        if (g_recorder)
            markMoment(name);
    });
    QObject::connect(runner, &StatusReplay::ScriptRunner::acted, qApp, [](const QJsonObject& step) {
        if (g_recorder)
            g_recorder->recordUiAction(step);
    });
    QObject::connect(runner, &StatusReplay::ScriptRunner::finished, qApp, [path](bool ok, const QString& error) {
        if (!ok) {
            qWarning().noquote() << "StatusReplay: script" << path << "failed:" << error;
            qApp->exit(3);
            return;
        }
        qInfo().noquote() << "StatusReplay: script" << path << "done";
        if (const int quitAfter = qEnvironmentVariableIntValue("STATUS_RECORD_QUIT_AFTER_SCRIPT_MS"); quitAfter > 0)
            QTimer::singleShot(quitAfter, qApp, quitRecording);
    });
    runner->start();
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

void onModelData(void* model, int row, int role, const void* value)
{
    if (g_recorder)
        g_recorder->modelDataObserved(static_cast<QObject*>(model), row, role, *static_cast<const QVariant*>(value));
}

// Runs the script once the main window is up: after the startup moment when recording.
void runScriptAfterLoad(QQmlEngine* engine)
{
    auto appEngine = qobject_cast<QQmlApplicationEngine*>(engine);
    if (!appEngine)
        return;
    QObject::connect(appEngine, &QQmlApplicationEngine::objectCreated, appEngine,
        [appEngine](QObject* root, const QUrl&) {
            if (!root)
                return;
            if (g_recorder) {
                g_recorder->markLoaded();
                g_recorder->setFingerprint(StatusReplay::setupFingerprint(appEngine));
            }
            const int delay = qEnvironmentVariableIntValue("STATUS_RECORD_STARTUP_DELAY_MS");
            QTimer::singleShot(delay > 0 ? delay : 5000, qApp, [] {
                if (g_recorder)
                    markMoment(QStringLiteral("startup"));
                runScript();
            });
        },
        Qt::SingleShotConnection);
}

void startProbeIfAsked(QQmlEngine* engine, const QString& directory)
{
    if (qEnvironmentVariableIsSet("STATUS_REPLAY_PROBE_AT"))
        StatusReplay::startProbe(engine, qEnvironmentVariable("STATUS_REPLAY_PROBE_AT"), directory);
}

} // namespace

extern "C" {

StatusReplayHooks statusreplay_recorder_attach(void* enginePtr)
{
    if (g_recorder)
        return { onContextProperty, onMetacall, onModelData };

    auto engine = static_cast<QQmlEngine*>(enginePtr);
    // Script only (STATUS_RECORD_SCRIPT without STATUS_RECORD): drives the app, e.g. to measure
    // the real app at the moments a recording has, with nothing observed or written.
    if (!qEnvironmentVariableIsSet("STATUS_RECORD")) {
        qInfo() << "StatusReplay: running" << qEnvironmentVariable("STATUS_RECORD_SCRIPT") << "without recording";
        startProbeIfAsked(engine,
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/probe"));
        runScriptAfterLoad(engine);
        return { nullptr, nullptr, nullptr };
    }

    auto recorder = new Recorder(recordingDirectory());
    if (!recorder->isValid()) {
        qWarning() << "StatusReplay: recording disabled:" << recorder->errorString();
        delete recorder;
        return { nullptr, nullptr, nullptr };
    }
    g_recorder = recorder;
    qInfo() << "StatusReplay: recording into" << recorder->directory();

    recorder->setEngine(engine);
    startProbeIfAsked(engine, recorder->directory() + QStringLiteral("/probe"));
    // What QML `Settings` start from in this run; replay restores it into its own store.
    StatusReplay::snapshotSettings(recorder->directory() + QStringLiteral("/settings.ini"));

#ifdef STATUSREPLAY_WITH_STATUSQ
    // Images and other responses the app loads (status-go's media server included), described so
    // replay serves same-size placeholders. STATUS_RECORD_MEDIA=bytes keeps the real bodies
    // (personal images end up in the recording), =0 leaves media out.
    const QString media = qEnvironmentVariable("STATUS_RECORD_MEDIA");
    if (media != QLatin1String("0")) {
        g_media = new StatusReplay::MediaRecorder(recorder->directory() + QStringLiteral("/media"),
            media == QLatin1String("bytes") ? StatusReplay::MediaRecorder::Mode::Bytes
                                            : StatusReplay::MediaRecorder::Mode::Placeholders);
        statusq_setNetworkReplyTap([](void* reply) { g_media->observe(static_cast<QNetworkReply*>(reply)); });
    }
#endif

    runScriptAfterLoad(engine);

    // Scripted recordings end themselves so the exit moment is written.
    if (const int quitAfter = qEnvironmentVariableIntValue("STATUS_RECORD_QUIT_AFTER_MS"); quitAfter > 0)
        QTimer::singleShot(quitAfter, qApp, quitRecording);

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, finishRecording);

    return { onContextProperty, onMetacall, onModelData };
}

// App-defined moments also get a '<name>-settled' snapshot once async loading has had time to finish.
void statusreplay_recorder_markMoment(const char* name)
{
    if (!g_recorder)
        return;
    const QString moment = QString::fromUtf8(name);
    markMoment(moment);
    const int delay = qEnvironmentVariableIntValue("STATUS_RECORD_SETTLE_MS");
    QTimer::singleShot(delay > 0 ? delay : 60000, qApp, [moment] {
        if (g_recorder)
            markMoment(moment + QStringLiteral("-settled"));
    });
}
}
