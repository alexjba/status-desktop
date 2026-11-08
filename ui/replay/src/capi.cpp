#include <StatusReplay/fingerprint.h>
#include <StatusReplay/recorder.h>
#include <StatusReplay/statusreplay.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>
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

// Scripted recordings: types STATUS_RECORD_TYPE into the focused window after the startup
// moment ("\n" is Enter), e.g. a throwaway test profile's password at the login screen.
void typeStartupText()
{
    const QString text = qEnvironmentVariable("STATUS_RECORD_TYPE").replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    if (text.isEmpty())
        return;
    // A window launched in the background (or on a locked screen) is not active and has no
    // active focus item, so follow the focus scopes to the item QML gave focus to.
    QQuickItem* target = nullptr;
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        auto quickWindow = qobject_cast<QQuickWindow*>(window);
        if (!quickWindow || !quickWindow->isVisible())
            continue;
        target = quickWindow->activeFocusItem();
        if (!target) {
            for (QQuickItem* scope = quickWindow->contentItem(); scope; scope = scope->scopedFocusItem())
                target = scope;
        }
        break;
    }
    if (!target) {
        qWarning() << "StatusReplay: no focused item to type into";
        return;
    }
    qInfo() << "StatusReplay: typing into" << target;
    for (const QChar c : text) {
        const bool enter = c == QLatin1Char('\n');
        const int key = enter ? Qt::Key_Return : 0;
        const QString keyText = enter ? QStringLiteral("\r") : QString(c);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(target, &release);
    }
}

// Record mode keeps a picture of every moment next to its snapshot, to compare with replay.
void saveMomentScreenshot(const QString& moment)
{
    if (!g_recorder)
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
            [appEngine](QObject* root, const QUrl&) {
                if (!root || !g_recorder)
                    return;
                g_recorder->markLoaded();
                g_recorder->setFingerprint(StatusReplay::setupFingerprint(appEngine));
                const int delay = qEnvironmentVariableIntValue("STATUS_RECORD_STARTUP_DELAY_MS");
                QTimer::singleShot(delay > 0 ? delay : 5000, qApp, [] {
                    if (!g_recorder)
                        return;
                    markMoment(QStringLiteral("startup"));
                    typeStartupText();
                });
            },
            Qt::SingleShotConnection);
    }

    // Scripted recordings end themselves so the exit moment is written.
    if (const int quitAfter = qEnvironmentVariableIntValue("STATUS_RECORD_QUIT_AFTER_MS"); quitAfter > 0)
        QTimer::singleShot(quitAfter, qApp, &QCoreApplication::quit);

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        if (!g_recorder)
            return;
        markMoment(QStringLiteral("exit"));
        g_recorder->flush();
        qInfo() << "StatusReplay: recording written to" << g_recorder->directory();
    });

    return { onContextProperty, onMetacall };
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
