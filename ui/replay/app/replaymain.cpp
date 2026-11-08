#include <StatusReplay/fingerprint.h>
#include <StatusReplay/replayapp.h>
#include <StatusReplay/replayer.h>

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QQuickWindow>
#include <QTimer>

using namespace StatusReplay;

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

// Plays the timeline: realtime follows recorded timestamps, step waits for F8 (next event)
// and F9 (next moment), frozen keeps the starting state. F10 pauses/resumes realtime.
class Player : public QObject
{
public:
    enum class Mode
    {
        Realtime,
        Step,
        Frozen,
    };

    Player(Replayer* replayer, Mode mode, double speed)
        : m_replayer(replayer)
        , m_mode(mode)
        , m_speed(speed > 0 ? speed : 1.0)
    {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { advance(); });
        connect(replayer, &Replayer::momentReached, this, [this](const QString& name) {
            m_momentReached = true;
            qInfo().noquote() << "StatusReplay: moment" << name;
        });
        qApp->installEventFilter(this);
    }

    void start()
    {
        if (m_mode != Mode::Realtime)
            return;
        m_base = m_replayer->nextEventTime();
        m_clock.start();
        advance();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QObject::eventFilter(watched, event);
        switch (static_cast<QKeyEvent*>(event)->key()) {
        case Qt::Key_F8:
            m_replayer->step();
            return true;
        case Qt::Key_F9:
            m_momentReached = false;
            while (!m_momentReached && m_replayer->step()) { }
            return true;
        case Qt::Key_F10:
            togglePause();
            return true;
        default:
            return QObject::eventFilter(watched, event);
        }
    }

private:
    void advance()
    {
        if (m_paused)
            return;
        const qint64 now = m_base + qint64(m_clock.elapsed() * m_speed);
        while (!m_replayer->atEnd() && m_replayer->nextEventTime() <= now)
            m_replayer->step();
        if (m_replayer->atEnd()) {
            qInfo() << "StatusReplay: timeline finished";
            return;
        }
        m_timer.start(int(qMax<qint64>(1, (m_replayer->nextEventTime() - now) / m_speed)));
    }

    void togglePause()
    {
        if (m_mode != Mode::Realtime)
            return;
        m_paused = !m_paused;
        if (m_paused) {
            m_timer.stop();
            m_pausedAt = m_base + qint64(m_clock.elapsed() * m_speed);
        } else {
            m_base = m_pausedAt;
            m_clock.restart();
            advance();
        }
    }

    Replayer* m_replayer;
    Mode m_mode;
    double m_speed;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_base = 0;
    qint64 m_pausedAt = 0;
    bool m_paused = false;
    bool m_momentReached = false;
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
    prepareApplication(manifest);

    QCommandLineParser parser;
    parser.addOption({ "recording", "Recording directory.", "dir" });
    parser.addOption({ "moment", "Moment to start from (default: the beginning).", "name" });
    parser.addOption({ "mode", "realtime, step or frozen (default: realtime).", "mode", "realtime" });
    parser.addOption({ "speed", "Realtime speed factor.", "factor", "1" });
    parser.addOption({ "quit-after", "Quit after this many ms (scripted runs).", "ms" });
    parser.addOption({ "screenshot", "Save the window to this PNG before quitting.", "path" });
    parser.process(app);

    QString error;
    auto replayer = Replayer::open(recording, &error);
    if (!replayer) {
        qCritical().noquote() << "StatusReplay:" << error;
        return 2;
    }
    const QString moment = parser.value("moment");
    if (!replayer->startAt(moment, &error)) {
        qCritical().noquote() << "StatusReplay:" << error << "- moments:" << replayer->moments().join(", ");
        return 2;
    }

    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/status-replay");
    QQmlApplicationEngine engine;
    prepareEngine(&engine, manifest, temp);

    QQmlContext* root = engine.rootContext();
    QObject::connect(replayer.get(), &Replayer::contextPropertyChanged, root,
        [root](const QString& name, const QVariant& value) { root->setContextProperty(name, value); });
    for (const auto& name : replayer->contextPropertyNames())
        root->setContextProperty(name, replayer->contextProperty(name));

    // Everything Nim did before loading main.qml happens before the first frame anyway.
    if (!replayer->loadReached())
        replayer->runUntilLoaded();

    const QString modeName = parser.value("mode");
    const auto mode = modeName == QLatin1String("step") ? Player::Mode::Step
        : modeName == QLatin1String("frozen")           ? Player::Mode::Frozen
                                                        : Player::Mode::Realtime;
    Player player(replayer.get(), mode, parser.value("speed").toDouble());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&](QObject* rootObject, const QUrl&) {
            if (!rootObject)
                return;
            const QStringList differences =
                fingerprintDifferences(manifest["fingerprint"].toObject(), setupFingerprint(&engine));
            for (const auto& line : differences)
                qWarning().noquote() << "StatusReplay: setup differs:" << line;
            player.start();
        },
        Qt::SingleShotConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { writeCallLog(recording, *replayer); });
    if (const int quitAfter = parser.value("quit-after").toInt(); quitAfter > 0) {
        QTimer::singleShot(quitAfter, &app, [&] {
            if (parser.isSet("screenshot"))
                saveScreenshot(parser.value("screenshot"));
            app.quit();
        });
    }

    engine.load(QUrl(QStringLiteral("qrc:///main.qml")));
    return app.exec();
}
