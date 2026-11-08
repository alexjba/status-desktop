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

// Keeps the body of every successful GET response it observes in <dir>/ (index.jsonl plus one
// file per resource). Thread-safe: QML image loading runs in its own thread.
class STATUSREPLAY_EXPORT MediaRecorder
{
public:
    explicit MediaRecorder(const QString& directory);
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
    // A finished-on-the-next-loop-turn reply with the recorded body, or null when not recorded.
    QNetworkReply* reply(const QNetworkRequest& request, QObject* parent) const;

private:
    std::unique_ptr<MediaLibraryPrivate> d;
};

} // namespace StatusReplay
