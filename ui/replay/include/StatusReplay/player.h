#pragma once

#include <StatusReplay/statusreplay.h>

#include <QObject>

#include <memory>

namespace StatusReplay {

class PlayerPrivate;
class Replayer;

// Drives a Replayer's timeline: Realtime follows the recorded timestamps (scaled by `speed`),
// Step only moves on stepEvent()/stepToNextMoment(), Frozen keeps the starting state.
class STATUSREPLAY_EXPORT Player : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        Realtime,
        Step,
        Frozen,
    };

    Player(Replayer* replayer, Mode mode, double speed = 1.0, QObject* parent = nullptr);
    ~Player() override;

    // With a target moment, first plays up to it as fast as QML keeps up (a batch of events per
    // event-loop turn), so QML walks through the same states as in the recording; then plays on
    // in the player's mode.
    void start(const QString& fastForwardTo = {});

    void stepEvent();
    void stepToNextMoment();
    // Realtime only.
    void togglePause();
    bool isPaused() const;

signals:
    void fastForwardFinished(bool reached);
    void finished();

private:
    std::unique_ptr<PlayerPrivate> d;
};

} // namespace StatusReplay
