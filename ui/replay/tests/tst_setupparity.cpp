#include <StatusReplay/fingerprint.h>
#include <StatusReplay/replayapp.h>
#include <StatusReplay/replayer.h>

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

// Platform and machine dependent: compared by the replay at startup, not here.
const QStringList MachineKeys {
    QStringLiteral("rendering"),
    QStringLiteral("env.QT_QPA_PLATFORM"),
    QStringLiteral("env.QTWEBENGINE_CHROMIUM_FLAGS"),
    QStringLiteral("engine.mainQml"),
};

QStringList withoutMachineKeys(const QStringList& differences)
{
    QStringList result;
    for (const auto& line : differences) {
        const bool machine = std::any_of(MachineKeys.cbegin(), MachineKeys.cend(),
            [&](const QString& key) { return line.startsWith(key + QLatin1Char('.')) || line.startsWith(key + QLatin1Char(':')); });
        if (!machine)
            result.append(line);
    }
    return result;
}

} // namespace

// The replay setup must reproduce what the real app's setup looked like when the fixture was
// recorded (src/nim_status_client.nim). Re-record the fixture's fingerprint when the Nim setup changes.
class SetupParityTest : public QObject
{
    Q_OBJECT

private slots:
    void replaySetupMatchesTheRecordedApp()
    {
        QString error;
        auto replayer = Replayer::open(QStringLiteral(STATUSREPLAY_FIXTURE_DIR), &error);
        QVERIFY2(replayer, qPrintable(error));
        const QJsonObject recorded = replayer->fingerprint();
        QVERIFY(!recorded.isEmpty());

        QTemporaryDir temp;
        prepareEngine(m_engine, m_manifest, temp.path());
        // Context properties come from the recording at replay time.
        for (const auto& name : recorded["engine"].toObject()["contextProperties"].toArray())
            m_engine->rootContext()->setContextProperty(name.toString(), QVariant());

        const QStringList differences =
            withoutMachineKeys(fingerprintDifferences(recorded, setupFingerprint(m_engine)));
        QVERIFY2(differences.isEmpty(), qPrintable(differences.join(QLatin1Char('\n'))));
    }

public:
    QQmlApplicationEngine* m_engine = nullptr;
    QJsonObject m_manifest;
};

int main(int argc, char** argv)
{
    QFile file(QStringLiteral(STATUSREPLAY_FIXTURE_DIR "/manifest.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture manifest");
    QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
    // The fixture keeps checkout-relative paths for the Nim libraries' QML dirs.
    QJsonArray importPaths;
    for (const auto& path : manifest["importPaths"].toArray())
        importPaths.append(path.toString().startsWith(QLatin1String("vendor/"))
                ? QStringLiteral(STATUSREPLAY_REPO_ROOT "/") + path.toString()
                : path.toString());
    manifest["importPaths"] = importPaths;
    prepareProcess(manifest);
    QGuiApplication app(argc, argv);
    prepareApplication(manifest);
    QQmlApplicationEngine engine;
    SetupParityTest test;
    test.m_engine = &engine;
    test.m_manifest = manifest;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_setupparity.moc"
