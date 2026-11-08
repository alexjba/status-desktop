#pragma once

#include <StatusReplay/statusreplay.h>

#include <QJsonObject>
#include <QMetaObject>
#include <QString>
#include <QVariant>

#include <memory>

class QQmlEngine;

namespace StatusReplay {

class RecorderPrivate;

// Writes a recording directory (see docs/replay-mode.md) from the objects handed to QML.
// Everything reachable from the context properties is discovered and followed generically;
// slot calls and property writes from QML are reported through metacallObserved().
class STATUSREPLAY_EXPORT Recorder
{
public:
    explicit Recorder(const QString& directory);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool isValid() const;
    QString errorString() const;
    QString directory() const;

    // Root context properties set outside the hook are picked up from the engine at each moment.
    void setEngine(QQmlEngine* engine);
    void setContextProperty(const QString& name, const QVariant& value);

    // Called after the object finished handling the metacall, so argv[0] holds a slot's return value.
    void metacallObserved(QObject* object, QMetaObject::Call call, int index, void** argv);

    // Marks the main document load: replay loads QML at the same point of the timeline.
    void markLoaded();
    void markMoment(const QString& name);
    void setFingerprint(const QJsonObject& fingerprint);

    void flush();

private:
    std::unique_ptr<RecorderPrivate> d;
};

} // namespace StatusReplay
