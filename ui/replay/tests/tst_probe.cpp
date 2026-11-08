#include <StatusReplay/probe.h>

#include <QTest>

using namespace StatusReplay;

class ProbeTest : public QObject
{
    Q_OBJECT

private slots:
    // Release packages have no run-as, so the probe reads the app's own smaps: PSS + SwapPss per
    // mapped file (by name; APK-embedded libraries count under the APK) or anonymous region kind.
    void smapsAreSummedPerMapping()
    {
        const QByteArray smaps = R"(7000000000-7000100000 r-xp 00000000 fd:05 123   /data/app/~~x==/app.status.frecordr-y==/base.apk
Size:               1024 kB
Pss:                 600 kB
SwapPss:               0 kB
7000100000-7000200000 r--p 00100000 fd:05 123   /data/app/~~x==/app.status.frecordr-y==/base.apk
Pss:                 400 kB
SwapPss:              24 kB
7100000000-7100100000 r-xp 00000000 fd:05 77    /system/lib64/libc.so
Pss:                 100 kB
SwapPss:               0 kB
7200000000-7200100000 rw-p 00000000 00:00 0     [anon:scudo:primary]
Pss:                2048 kB
SwapPss:             512 kB
7300000000-7300100000 rw-p 00000000 00:00 0
Pss:                  10 kB
SwapPss:               0 kB
)";
        const QList<QPair<QString, qint64>> expected {
            { QStringLiteral("[anon:scudo:primary]"), 2560 },
            { QStringLiteral("base.apk"), 1024 },
            { QStringLiteral("libc.so"), 100 },
            { QStringLiteral("[anon]"), 10 },
        };
        QCOMPARE(pssPerMapping(smaps), expected);
    }
};

QTEST_GUILESS_MAIN(ProbeTest)
#include "tst_probe.moc"
