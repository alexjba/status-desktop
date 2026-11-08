#pragma once

#include <StatusReplay/statusreplay.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <memory>

namespace StatusReplay {

class ScriptRunnerPrivate;

// Drives the running QML app for unattended recordings (e.g. onboarding a fresh test profile
// with test credentials). Dev tooling only: it lives in libStatusReplay, which release
// builds never load.
//
// Each step waits (polling) for a visible, enabled item with the given objectName (or, as
// {"text": "..."}, the given `text` for controls without one), then acts:
//   { "click": name }                    mouse press/release at the item's centre
//   { "type": name, "text": "..." }      focuses the item's text input and types; "\n" = Enter
//   { "setChecked": name, "value": b }   clicks the item if its `checked` differs
//   { "waitFor": name }                  only waits
//   { "moment": name }                   emits momentRequested(name)
//   { "sleep": ms }
// "timeout" (ms, default 60000) bounds the wait; "optional": true skips a step whose item
// never appears instead of failing the script (logged). Skipped and failed steps are logged.
class STATUSREPLAY_EXPORT ScriptRunner : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRunner(const QJsonArray& steps, QObject* parent = nullptr);
    ~ScriptRunner() override;

    void start();
    // Queues a step after the others; an idle (finished) runner goes on with it. Its timeout
    // counts from when it becomes current; an optional one gives way when the next queued step's
    // item shows up while it waits; a failing one is logged and the rest still run (finished()
    // reports it once the queue runs dry).
    void append(const QJsonObject& step);

signals:
    void momentRequested(const QString& name);
    // After a click or setChecked was performed (what a recording replays as UI input).
    void acted(const QJsonObject& step);
    void finished(bool ok, const QString& error);

private:
    std::unique_ptr<ScriptRunnerPrivate> d;
};

} // namespace StatusReplay
