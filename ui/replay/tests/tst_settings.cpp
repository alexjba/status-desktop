#include <StatusReplay/settings.h>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

class SettingsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_recordedStore;
    QTemporaryDir m_replayStore;
    QTemporaryDir m_recording;

    QString snapshotFile() const { return m_recording.filePath(QStringLiteral("settings.ini")); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StatusReplayTest"));
        QCoreApplication::setApplicationName(QStringLiteral("SettingsTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_recordedStore.path());
        QSettings settings;
        settings.setValue(QStringLiteral("AppMainLocalSettings_0x04aa11/enablePushNotificationsDontAskAgain"), true);
        settings.setValue(QStringLiteral("walletSettings-0x04aa11/buyBannerEnabled"), false);
        settings.setValue(QStringLiteral("global/scale"), 1.25);
        settings.setValue(QStringLiteral("list"), QStringList { "a", "b" });
        // Other profiles of the same developer, an address and a community.
        settings.setValue(QStringLiteral("AppMainLocalSettings_0x04bb22/theme"), 2);
        settings.setValue(QStringLiteral("AssetsViewSortSettings-0xf36c1ef1977faa929ccf545a4073f3198e09f797/order"), 1);
        settings.setValue(QStringLiteral("BannerSettings_0x03cc33/hidden"), true);
        // The organization-wide fallback (the macOS global domain on native settings).
        QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("StatusReplayTest"))
            .setValue(QStringLiteral("orgWide/secret"), QStringLiteral("x"));
    }

    // QML `Settings` (e.g. "notifications prompt already seen") live in the app's default
    // QSettings: replay starts from what the recorded run started from, in its own store.
    void replayStartsFromTheRecordedSettings()
    {
        SettingsSnapshot snapshot;
        QVERIFY(snapshot.write(snapshotFile(), { QStringLiteral("0x04aa11") }));

        useReplaySettings(snapshotFile(), m_replayStore.path());
        QSettings replayed;
        QCOMPARE(replayed.fileName().startsWith(m_replayStore.path()), true);
        QCOMPARE(replayed.value("AppMainLocalSettings_0x04aa11/enablePushNotificationsDontAskAgain").toBool(), true);
        QCOMPARE(replayed.value("walletSettings-0x04aa11/buyBannerEnabled").toBool(), false);
        QCOMPARE(replayed.value("global/scale").toDouble(), 1.25);
        QCOMPARE(replayed.value("list").toStringList(), (QStringList { "a", "b" }));

        // Replay writes stay in its own store.
        replayed.setValue(QStringLiteral("global/scale"), 2.0);
        replayed.sync();
        QSettings recorded(m_recordedStore.filePath(QStringLiteral("StatusReplayTest/SettingsTest.ini")),
            QSettings::IniFormat);
        QCOMPARE(recorded.value("global/scale").toDouble(), 1.25);
    }

    // Only the recorded profile's settings: no other profile, address or community of the
    // developer, and nothing from fallback (organization-wide or global) settings.
    void othersSettingsStayOut()
    {
        SettingsSnapshot snapshot;
        QVERIFY(snapshot.write(snapshotFile(), { QStringLiteral("0x04aa11") }));
        const QSettings written(snapshotFile(), QSettings::IniFormat);
        const QStringList keys = written.allKeys();
        for (const auto& key : keys) {
            QVERIFY2(!key.contains(QLatin1String("0x04bb22")), qPrintable(key));
            QVERIFY2(!key.contains(QLatin1String("0xf36c")), qPrintable(key));
            QVERIFY2(!key.contains(QLatin1String("0x03cc33")), qPrintable(key));
            QVERIFY2(!key.startsWith(QLatin1String("orgWide")), qPrintable(key));
        }
        QVERIFY(keys.contains(QStringLiteral("global/scale")));

        // Before login the profile is unknown: no keyed group at all.
        QVERIFY(snapshot.write(snapshotFile(), {}));
        const QSettings beforeLogin(snapshotFile(), QSettings::IniFormat);
        QVERIFY(!beforeLogin.allKeys().join(' ').contains(QLatin1String("0x")));
        QVERIFY(beforeLogin.allKeys().contains(QStringLiteral("global/scale")));
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
