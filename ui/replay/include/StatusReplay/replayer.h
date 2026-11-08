#pragma once

#include <StatusReplay/statusreplay.h>

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include <memory>

namespace StatusReplay {

class ReplayerPrivate;

// Rebuilds the recorded objects and models and plays the recorded timeline on them.
class STATUSREPLAY_EXPORT Replayer : public QObject
{
    Q_OBJECT

public:
    ~Replayer() override;

    static std::unique_ptr<Replayer> open(const QString& directory, QString* error = nullptr);

    QStringList moments() const;
    QJsonObject fingerprint() const;

    // Builds the state recorded at the moment; an empty name starts before the first event.
    bool startAt(const QString& moment, QString* error = nullptr);

    QStringList contextPropertyNames() const;
    QVariant contextProperty(const QString& name) const;

    bool atEnd() const;
    bool loadReached() const;
    // Timestamp (ms since recording start) of the next event, -1 at the end.
    qint64 nextEventTime() const;
    // Applies the next timeline event. Returns false at the end.
    bool step();
    // Steps until the moment's marker was applied. Returns false if the timeline ended first.
    bool runUntilMoment(const QString& moment);
    // Steps until the main-document load marker was applied (no-op when already past it).
    bool runUntilLoaded();

    // Slot calls received from QML, with whether a recorded return value matched.
    struct Call
    {
        QString object;
        QString method;
        QVariantList arguments;
        bool matched = false;
    };
    QList<Call> calls() const;

signals:
    void contextPropertyChanged(const QString& name, const QVariant& value);
    void momentReached(const QString& name);
    void loaded();
    // A recorded UI action (ScriptRunner step) to perform now.
    void uiAction(const QJsonObject& step);

private:
    Replayer();
    std::unique_ptr<ReplayerPrivate> d;
};

} // namespace StatusReplay
