#include <StatusReplay/settings.h>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

class SettingsTest : public QObject
{
    Q_OBJECT

private slots:
    // QML `Settings` (e.g. "notifications prompt already seen") live in the app's default
    // QSettings: replay starts from what the recorded run started from, in its own store.
    void replayStartsFromTheRecordedSettings()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StatusReplayTest"));
        QCoreApplication::setApplicationName(QStringLiteral("SettingsTest"));
        QTemporaryDir recordedStore;
        QTemporaryDir replayStore;
        QTemporaryDir recording;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, recordedStore.path());
        {
            QSettings settings;
            settings.setValue(QStringLiteral("AppMainLocalSettings_0x04/enablePushNotificationsDontAskAgain"), true);
            settings.setValue(QStringLiteral("global/scale"), 1.25);
            settings.setValue(QStringLiteral("list"), QStringList { "a", "b" });
        }
        const QString file = recording.filePath(QStringLiteral("settings.ini"));
        QVERIFY(snapshotSettings(file));

        useReplaySettings(file, replayStore.path());
        QSettings replayed;
        QCOMPARE(replayed.fileName().startsWith(replayStore.path()), true);
        QCOMPARE(replayed.value("AppMainLocalSettings_0x04/enablePushNotificationsDontAskAgain").toBool(), true);
        QCOMPARE(replayed.value("global/scale").toDouble(), 1.25);
        QCOMPARE(replayed.value("list").toStringList(), (QStringList { "a", "b" }));

        // Replay writes stay in its own store.
        replayed.setValue(QStringLiteral("global/scale"), 2.0);
        replayed.sync();
        QSettings recorded(recordedStore.filePath(QStringLiteral("StatusReplayTest/SettingsTest.ini")),
            QSettings::IniFormat);
        QCOMPARE(recorded.value("global/scale").toDouble(), 1.25);
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
