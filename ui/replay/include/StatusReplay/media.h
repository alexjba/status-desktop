#pragma once

#include <StatusReplay/statusreplay.h>

#include <QString>
#include <QUrl>

#include <memory>

class QNetworkReply;
class QNetworkRequest;
class QObject;

namespace StatusReplay {

// Identity of a network resource across runs: status-go's media server (localhost /
// 127.0.0.1) gets a new port every run, so local URLs drop host and port.
STATUSREPLAY_EXPORT QString mediaKey(const QUrl& url);

class MediaRecorderPrivate;

// Describes every successful GET response it observes in <dir>/index.jsonl: status, content
// type, size and, for images, pixel size, format, decoded pixel format and frame count. Replay
// serves same-size placeholders from that, so decoded images and textures match without the
// recording holding anyone's pictures. Mode::Bytes also keeps the bodies (one file each) for
// visual checks; such recordings contain personal images. Thread-safe: QML image loading runs
// in its own thread.
class STATUSREPLAY_EXPORT MediaRecorder
{
public:
    enum class Mode
    {
        Placeholders,
        Bytes,
    };

    explicit MediaRecorder(const QString& directory, Mode mode = Mode::Placeholders);
    ~MediaRecorder();

    // Call when the reply is created: the body is peeked at when it finishes, so the
    // consumer still reads all of it.
    void observe(QNetworkReply* reply);
    int count() const;

private:
    std::unique_ptr<MediaRecorderPrivate> d;
};

class MediaLibraryPrivate;

// Serves recorded responses in replay.
class STATUSREPLAY_EXPORT MediaLibrary
{
public:
    explicit MediaLibrary(const QString& directory);
    ~MediaLibrary();

    int size() const;
    bool contains(const QUrl& url) const;
    // A reply finishing on the next loop turn with the recorded body, or a generated one of the
    // same size, format and frame count; null when the URL was not recorded.
    QNetworkReply* reply(const QNetworkRequest& request, QObject* parent) const;

private:
    std::unique_ptr<MediaLibraryPrivate> d;
};

} // namespace StatusReplay
