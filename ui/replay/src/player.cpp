#include <StatusReplay/player.h>

#include <StatusReplay/replayer.h>

#include <QElapsedTimer>
#include <utility>
#include <QTimer>

namespace StatusReplay {

class PlayerPrivate
{
public:
    Replayer* replayer;
    Player::Mode mode;
    double speed;
    QTimer timer;
    QElapsedTimer clock;
    qint64 base = 0;
    qint64 pausedAt = 0;
    bool paused = false;
    bool momentReached = false;
    QString target;
    bool targetReached = false;
    QString pendingFastForward; // paused during a fast-forward: resumed by togglePause()

    qint64 now() const { return base + qint64(clock.elapsed() * speed); }
};

Player::Player(Replayer* replayer, Mode mode, double speed, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<PlayerPrivate>())
{
    d->replayer = replayer;
    d->mode = mode;
    d->speed = speed > 0 ? speed : 1.0;
    d->timer.setSingleShot(true);
    connect(&d->timer, &QTimer::timeout, this, [this] {
        if (d->paused)
            return;
        const qint64 now = d->now();
        // A pause requested from inside an event (e.g. a moment handler) stops this batch.
        while (!d->paused && !d->replayer->atEnd() && d->replayer->nextEventTime() <= now)
            d->replayer->step();
        if (d->paused)
            return;
        if (d->replayer->atEnd()) {
            emit finished();
            return;
        }
        d->timer.start(int(qMax<qint64>(1, qint64((d->replayer->nextEventTime() - now) / d->speed))));
    });
    connect(replayer, &Replayer::momentReached, this, [this](const QString& name) {
        d->momentReached = true;
        d->targetReached = d->targetReached || name == d->target;
    });
}

Player::~Player() = default;

void Player::start(const QString& fastForwardTo)
{
    if (!fastForwardTo.isEmpty()) {
        constexpr int EventsPerTurn = 20;
        d->target = fastForwardTo;
        for (int i = 0; i < EventsPerTurn && !d->paused && !d->targetReached && d->replayer->step(); ++i) { }
        if (d->paused) {
            d->pendingFastForward = fastForwardTo;
            return;
        }
        if (!d->targetReached && !d->replayer->atEnd()) {
            QTimer::singleShot(0, this, [this, fastForwardTo] { start(fastForwardTo); });
            return;
        }
        const bool reached = d->targetReached;
        d->target.clear();
        d->targetReached = false;
        emit fastForwardFinished(reached);
    }
    if (d->mode != Mode::Realtime)
        return;
    d->base = d->replayer->nextEventTime();
    d->clock.start();
    d->timer.start(0);
}

void Player::stepEvent()
{
    d->replayer->step();
}

void Player::stepToNextMoment()
{
    d->momentReached = false;
    while (!d->momentReached && d->replayer->step()) { }
}

void Player::togglePause()
{
    const bool fastForwarding = !d->target.isEmpty();
    if (d->mode != Mode::Realtime && !fastForwarding && d->pendingFastForward.isEmpty())
        return;
    d->paused = !d->paused;
    if (!d->paused && !d->pendingFastForward.isEmpty()) {
        start(std::exchange(d->pendingFastForward, QString()));
        return;
    }
    if (d->paused) {
        d->timer.stop();
        d->pausedAt = d->now();
        return;
    }
    d->base = d->pausedAt;
    d->clock.restart();
    d->timer.start(0);
}

bool Player::isPaused() const
{
    return d->paused;
}

} // namespace StatusReplay
