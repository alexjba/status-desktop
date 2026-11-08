#include <StatusReplay/fingerprint.h>
#include <StatusReplay/media.h>
#include <StatusReplay/player.h>
#include <StatusReplay/replayapp.h>
#include <StatusReplay/probe.h>
#include <StatusReplay/replayer.h>
#include <StatusReplay/settings.h>
#include <StatusReplay/scriptrunner.h>

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

using namespace StatusReplay;

extern "C" void statusq_setNetworkRequestInterceptor(void* (*interceptor)(const void* request, void* parent));

namespace {

QJsonObject readManifest(const QString& directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString argumentValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
    return {};
}

// F8 next event, F9 next moment, F10 pause/resume realtime.
class PlayerKeys : public QObject
{
public:
    explicit PlayerKeys(Player* player)
        : m_player(player)
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress) {
            switch (static_cast<QKeyEvent*>(event)->key()) {
            case Qt::Key_F8:
                m_player->stepEvent();
                return true;
            case Qt::Key_F9:
                m_player->stepToNextMoment();
                return true;
            case Qt::Key_F10:
                m_player->togglePause();
                return true;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    Player* m_player;
};

void saveScreenshot(const QString& path)
{
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto quickWindow = qobject_cast<QQuickWindow*>(window); quickWindow && quickWindow->isVisible()) {
            if (quickWindow->grabWindow().save(path))
                qInfo().noquote() << "StatusReplay: screenshot" << path;
            return;
        }
    }
}

void writeCallLog(const QString& directory, const Replayer& replayer)
{
    const auto calls = replayer.calls();
    QDir dir(directory);
    dir.mkpath(QStringLiteral("replay-logs"));
    QFile file(dir.filePath(QStringLiteral("replay-logs/%1-calls.jsonl")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))));
    if (!file.open(QIODevice::WriteOnly))
        return;
    QMap<QString, int> unmatched;
    for (const auto& call : calls) {
        QJsonObject line { { "object", call.object }, { "method", call.method }, { "matched", call.matched },
            { "args", QJsonArray::fromVariantList(call.arguments) } };
        file.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        if (!call.matched)
            ++unmatched[call.method];
    }
    qInfo().noquote() << "StatusReplay:" << calls.size() << "slot calls," << unmatched.size()
                      << "methods without an exact recorded match; log:" << file.fileName();
}

} // namespace

extern "C" int statusreplay_main(int argc, char** argv)
{
    QString recording = argumentValue(argc, argv, "--recording");
    if (recording.isEmpty())
        recording = qEnvironmentVariable("STATUS_REPLAY");
    const QJsonObject manifest = readManifest(recording);
    if (manifest.isEmpty()) {
        qCritical().noquote() << "StatusReplay: no recording at" << recording
                              << "(pass --recording <dir> or set STATUS_REPLAY)";
        return 2;
    }

    prepareProcess(manifest);
    QGuiApplication app(argc, argv);
    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/status-replay");
    // QML `Settings` start from the recorded run's, in a store of their own (before QML loads).
    useReplaySettings(QDir(recording).filePath(QStringLiteral("settings.ini")), temp + QStringLiteral("/settings"));
    prepareApplication(manifest);

    QCommandLineParser parser;
    parser.addOption({ "recording", "Recording directory.", "dir" });
    parser.addOption({ "moment", "Moment to start from (default: the beginning).", "name" });
    parser.addOption({ "mode", "realtime, step or frozen (default: realtime).", "mode", "realtime" });
    parser.addOption({ "speed", "Realtime speed factor.", "factor", "1" });
    parser.addOption({ "quit-after", "Quit after this many ms (scripted runs).", "ms" });
    parser.addOption({ "screenshot", "Save the window to this PNG before quitting.", "path" });
    parser.addOption({ "check-setup",
        "Compare the replay setup with the recorded app's and exit (1 on any difference)." });
    parser.addOption({ "from-snapshot",
        "Start a post-load moment from its snapshot instead of playing up to it (QML's own state, "
        "such as the current page, then starts from scratch)." });
    parser.process(app);
    // Android launches without arguments: the same options come from the environment
    // (status-env.txt of debug packages), e.g. STATUS_REPLAY_MOMENT for --moment.
    auto option = [&parser](const char* name) {
        if (parser.isSet(QString::fromLatin1(name)))
            return parser.value(QString::fromLatin1(name));
        const QByteArray env = "STATUS_REPLAY_" + QByteArray(name).toUpper().replace('-', '_');
        return qEnvironmentVariableIsSet(env.constData()) ? qEnvironmentVariable(env.constData())
                                                         : parser.value(QString::fromLatin1(name));
    };

    QString error;
    auto replayer = Replayer::open(recording, &error);
    if (!replayer) {
        qCritical().noquote() << "StatusReplay:" << error;
        return 2;
    }
    const QString moment = option("moment");
    if (!replayer->startAt(moment, &error)) {
        qCritical().noquote() << "StatusReplay:" << error << "- moments:" << replayer->moments().join(", ");
        return 2;
    }
    // A moment after main.qml loaded is reached by playing up to it: QML keeps state of its own
    // (current page, popups) that only follows from the events that led there.
    QString fastForwardTo;
    if (replayer->loadReached() && option("from-snapshot").isEmpty() && !parser.isSet("from-snapshot")) {
        replayer->startAt(QString());
        fastForwardTo = moment;
    }

    QQmlApplicationEngine engine;
    prepareEngine(&engine, manifest, temp);
    if (qEnvironmentVariableIsSet("STATUS_REPLAY_PROBE_AT"))
        startProbe(&engine, qEnvironmentVariable("STATUS_REPLAY_PROBE_AT"),
            QDir(recording).filePath(QStringLiteral("replay-probe")));
    // Recorded responses answer the app's requests; anything not recorded goes to the network.
    static MediaLibrary* media = nullptr;
    MediaLibrary library(QDir(recording).filePath(QStringLiteral("media")));
    if (library.size() > 0) {
        media = &library;
        statusq_setNetworkRequestInterceptor([](const void* request, void* parent) -> void* {
            return media->reply(*static_cast<const QNetworkRequest*>(request), static_cast<QObject*>(parent));
        });
        qInfo() << "StatusReplay:" << library.size() << "recorded network responses";
    }

    QQmlContext* root = engine.rootContext();
    QObject::connect(replayer.get(), &Replayer::contextPropertyChanged, root,
        [root](const QString& name, const QVariant& value) { root->setContextProperty(name, value); });
    for (const auto& name : replayer->contextPropertyNames())
        root->setContextProperty(name, replayer->contextProperty(name));

    // Everything Nim did before loading main.qml happens before the first frame anyway.
    if (!replayer->loadReached())
        replayer->runUntilLoaded();

    if (parser.isSet("check-setup")) {
        const QStringList differences =
            fingerprintDifferences(manifest["fingerprint"].toObject(), setupFingerprint(&engine));
        for (const auto& line : differences)
            qWarning().noquote() << "StatusReplay: setup differs:" << line;
        if (differences.isEmpty())
            qInfo() << "StatusReplay: replay setup matches the recorded app";
        return differences.isEmpty() ? 0 : 1;
    }

    const QString modeName = option("mode");
    const auto mode = modeName == QLatin1String("step") ? Player::Mode::Step
        : modeName == QLatin1String("frozen")           ? Player::Mode::Frozen
                                                        : Player::Mode::Realtime;
    Player player(replayer.get(), mode, option("speed").toDouble());
    PlayerKeys keys(&player);
    QObject::connect(replayer.get(), &Replayer::momentReached, &app,
        [](const QString& name) { qInfo().noquote() << "StatusReplay: moment" << name; });
    QObject::connect(&player, &Player::fastForwardFinished, &app, [&](bool reached) {
        if (!reached)
            qWarning().noquote() << "StatusReplay: timeline ended before moment" << fastForwardTo;
    });
    QObject::connect(&player, &Player::finished, &app, [] { qInfo() << "StatusReplay: timeline finished"; });

    // Recorded UI actions (e.g. the scripted click on the wallet button) are performed again,
    // so QML's own navigation state follows the recording.
    QObject::connect(replayer.get(), &Replayer::uiAction, &app, [](const QJsonObject& step) {
        QJsonObject single = step;
        single["optional"] = true;
        // Fast-forwarded, the event can come long before QML shows its target (a popup opened by a
        // timer): wait as long as a scripted step would.
        single["timeout"] = 60000;
        auto runner = new ScriptRunner(QJsonArray { single }, qApp);
        QObject::connect(runner, &ScriptRunner::finished, runner, &QObject::deleteLater);
        runner->start();
    });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&](QObject* rootObject, const QUrl&) {
            if (!rootObject)
                return;
            const QStringList differences =
                fingerprintDifferences(manifest["fingerprint"].toObject(), setupFingerprint(&engine));
            for (const auto& line : differences)
                qWarning().noquote() << "StatusReplay: setup differs:" << line;
            player.start(fastForwardTo);
        },
        Qt::SingleShotConnection);
    // Android ends the process without aboutToQuit: scripted quits write the log first.
    bool callLogWritten = false;
    auto finish = [&] {
        if (!std::exchange(callLogWritten, true))
            writeCallLog(recording, *replayer);
    };
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, finish);
    if (const int quitAfter = option("quit-after").toInt(); quitAfter > 0) {
        QTimer::singleShot(quitAfter, &app, [&] {
            if (const QString screenshot = option("screenshot"); !screenshot.isEmpty())
                saveScreenshot(screenshot);
            finish();
            app.quit();
        });
    }

    engine.load(QUrl(QStringLiteral("qrc:///main.qml")));
    const int result = app.exec();
    statusq_setNetworkRequestInterceptor(nullptr);
    return result;
}
