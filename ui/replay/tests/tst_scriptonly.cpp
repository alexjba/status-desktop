#include <StatusReplay/statusreplay.h>

#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class ScriptOnlyTest : public QObject
{
    Q_OBJECT

private slots:
    // Measuring the real app needs the scripted login without the recorder's cost: with only
    // STATUS_RECORD_SCRIPT set, the script drives the UI and nothing is observed or written.
    void scriptRunsWithoutRecording()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("script.json"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"json([{ "setChecked": "agree", "value": true }])json");
        file.close();
        qunsetenv("STATUS_RECORD");
        qputenv("STATUS_RECORD_SCRIPT", script.toUtf8());
        qputenv("STATUS_RECORD_STARTUP_DELAY_MS", "1");
        qputenv("STATUS_REPLAY_PROBE_AT", "1");
        // Android packages export their external files dir, which adb can read without run-as.
        qputenv("STATUS_EXTERNAL_FILES_DIR", dir.path().toUtf8());
        qputenv("STATUS_RECORD_QUIT_AFTER_MS", "3000");


        const QDir recordings(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/recordings"));
        const QStringList before = recordings.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        QQmlApplicationEngine engine;
        const StatusReplayHooks hooks = statusreplay_recorder_attach(&engine);
        QVERIFY(!hooks.contextProperty && !hooks.metacall && !hooks.modelData);

        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    visible: true
    width: 100; height: 100
    readonly property bool agreed: agree.checked
    Switch { id: agree; objectName: "agree" }
}
)");
        QVERIFY(!engine.rootObjects().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(engine.rootObjects().first()->property("agreed").toBool(), 5000);
        QCOMPARE(recordings.entryList(QDir::Dirs | QDir::NoDotAndDotDot), before);
        // The calibration probe still runs.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath(QStringLiteral("probe/probe-1.tsv"))), 5000);
        // And the run ends itself when asked to: quitting ends a running event loop early.
        QEventLoop loop;
        QTimer::singleShot(10000, &loop, [&loop] { loop.exit(1); });
        QCOMPARE(loop.exec(), 0);
    }
};

QTEST_MAIN(ScriptOnlyTest)
#include "tst_scriptonly.moc"
