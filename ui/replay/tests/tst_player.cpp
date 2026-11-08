#include <StatusReplay/player.h>
#include <StatusReplay/replayer.h>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

// Moments a, b, c recorded 0, 100 and 200 ms into the run.
bool writeTimeline(const QString& directory)
{
    QFile manifest(directory + QStringLiteral("/manifest.json"));
    QFile timeline(directory + QStringLiteral("/timeline.jsonl"));
    if (!manifest.open(QIODevice::WriteOnly) || !timeline.open(QIODevice::WriteOnly))
        return false;
    manifest.write(QJsonDocument(QJsonObject { { "formatVersion", "1.0" } }).toJson());
    timeline.write("{\"k\":\"moment\",\"n\":\"a\",\"q\":0,\"t\":0}\n"
                   "{\"k\":\"moment\",\"n\":\"b\",\"q\":1,\"t\":100}\n"
                   "{\"k\":\"moment\",\"n\":\"c\",\"q\":2,\"t\":200}\n");
    return true;
}

QStringList names(const QSignalSpy& spy)
{
    QStringList result;
    for (const auto& arguments : spy)
        result << arguments.at(0).toString();
    return result;
}

} // namespace

class PlayerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Replayer> m_replayer;

private slots:
    void init()
    {
        QVERIFY(writeTimeline(m_dir.path()));
        QString error;
        m_replayer = Replayer::open(m_dir.path(), &error);
        QVERIFY2(m_replayer, qPrintable(error));
        QVERIFY(m_replayer->startAt(QString()));
    }

    void cleanup() { m_replayer.reset(); }

    void realtimeFollowsTheRecordedTimestamps()
    {
        Player player(m_replayer.get(), Player::Mode::Realtime);
        QSignalSpy moments(m_replayer.get(), &Replayer::momentReached);
        QSignalSpy finished(&player, &Player::finished);
        QElapsedTimer clock;
        clock.start();
        player.start();
        QVERIFY(finished.wait(5000));
        QCOMPARE(names(moments), (QStringList { "a", "b", "c" }));
        QVERIFY2(clock.elapsed() >= 190, qPrintable(QString::number(clock.elapsed())));
    }

    // Relative to a 1x run of the same timeline, so a slow machine only makes both slower.
    void speedScalesTheTimestamps()
    {
        auto run = [this](double speed) {
            QString error;
            m_replayer = Replayer::open(m_dir.path(), &error);
            m_replayer->startAt(QString());
            Player player(m_replayer.get(), Player::Mode::Realtime, speed);
            QSignalSpy finished(&player, &Player::finished);
            QElapsedTimer clock;
            clock.start();
            player.start();
            finished.wait(5000);
            return clock.elapsed();
        };
        const qint64 normal = run(1.0);
        const qint64 fast = run(10.0);
        const QString times = QStringLiteral("1x %1 ms, 10x %2 ms").arg(normal).arg(fast);
        QVERIFY2(fast * 3 < normal, qPrintable(times));
        QVERIFY2(fast * 30 > normal, qPrintable(times));
    }

    void pauseHoldsTheTimelineUntilResumed()
    {
        Player player(m_replayer.get(), Player::Mode::Realtime);
        QSignalSpy moments(m_replayer.get(), &Replayer::momentReached);
        connect(m_replayer.get(), &Replayer::momentReached, this, [&](const QString& name) {
            if (name == QLatin1String("a"))
                player.togglePause();
        });
        player.start();
        QVERIFY(moments.wait(1000));
        QVERIFY(player.isPaused());
        QVERIFY(!moments.wait(300));
        QCOMPARE(names(moments), QStringList { "a" });

        QSignalSpy finished(&player, &Player::finished);
        player.togglePause();
        QVERIFY(finished.wait(5000));
        QCOMPARE(names(moments), (QStringList { "a", "b", "c" }));
    }

    void pauseDuringFastForwardResumesIt()
    {
        Player player(m_replayer.get(), Player::Mode::Frozen);
        QSignalSpy moments(m_replayer.get(), &Replayer::momentReached);
        connect(m_replayer.get(), &Replayer::momentReached, this, [&](const QString& name) {
            if (name == QLatin1String("a"))
                player.togglePause();
        });
        QSignalSpy done(&player, &Player::fastForwardFinished);
        player.start(QStringLiteral("c"));
        QVERIFY(player.isPaused());
        QVERIFY(!done.wait(200));
        QCOMPARE(names(moments), QStringList { "a" });

        player.togglePause();
        QVERIFY(done.count() == 1 || done.wait(1000));
        QCOMPARE(done[0][0].toBool(), true);
        QCOMPARE(names(moments), (QStringList { "a", "b", "c" }));
    }

    void stepModeMovesOnlyWhenAsked()
    {
        Player player(m_replayer.get(), Player::Mode::Step);
        QSignalSpy moments(m_replayer.get(), &Replayer::momentReached);
        player.start();
        QVERIFY(!moments.wait(150));
        player.stepEvent();
        QCOMPARE(names(moments), QStringList { "a" });
        player.stepToNextMoment();
        QCOMPARE(names(moments), (QStringList { "a", "b" }));
    }

    void fastForwardStopsAtTheMomentThenFollowsTheMode()
    {
        Player player(m_replayer.get(), Player::Mode::Frozen);
        QSignalSpy moments(m_replayer.get(), &Replayer::momentReached);
        QSignalSpy done(&player, &Player::fastForwardFinished);
        player.start(QStringLiteral("b"));
        QVERIFY(done.count() == 1 || done.wait(1000));
        QCOMPARE(done[0][0].toBool(), true);
        QCOMPARE(names(moments), (QStringList { "a", "b" }));
        QVERIFY(!moments.wait(300));
    }

    void fastForwardToAMissingMomentReportsIt()
    {
        Player player(m_replayer.get(), Player::Mode::Frozen);
        QSignalSpy done(&player, &Player::fastForwardFinished);
        player.start(QStringLiteral("nope"));
        QVERIFY(done.count() == 1 || done.wait(1000));
        QCOMPARE(done[0][0].toBool(), false);
        QVERIFY(m_replayer->atEnd());
    }
};

QTEST_GUILESS_MAIN(PlayerTest)
#include "tst_player.moc"
