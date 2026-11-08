#include <StatusReplay/media.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace StatusReplay {

namespace {

const QString IndexFile = QStringLiteral("index.jsonl");

QString fileFor(const QString& key)
{
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex())
        + QStringLiteral(".bin");
}

// A finished reply with a recorded body. Signals go out on the next loop turn, like a real
// reply's, after the caller connected to them.
class RecordedReply : public QNetworkReply
{
public:
    RecordedReply(const QNetworkRequest& request, const QByteArray& body, const QString& contentType, QObject* parent)
        : QNetworkReply(parent)
        , m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        setHeader(QNetworkRequest::ContentLengthHeader, m_body.size());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit metaDataChanged();
            emit downloadProgress(m_body.size(), m_body.size());
            emit readyRead();
            emit finished();
        });
    }

    void abort() override { }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 size = qMin(maxSize, qint64(m_body.size()) - m_offset);
        if (size <= 0)
            return m_offset >= m_body.size() ? -1 : 0;
        memcpy(data, m_body.constData() + m_offset, size_t(size));
        m_offset += size;
        return size;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

} // namespace

QString mediaKey(const QUrl& url)
{
    const QString host = url.host();
    const bool local = host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1");
    QUrl key = url;
    key.setUserInfo({});
    if (local) {
        key.setHost(QStringLiteral("local"));
        key.setPort(-1);
    }
    return key.toString(QUrl::FullyEncoded);
}

class MediaRecorderPrivate
{
public:
    QDir dir;
    QFile index;
    QMutex mutex;
    QHash<QString, QString> files;
};

MediaRecorder::MediaRecorder(const QString& directory)
    : d(std::make_unique<MediaRecorderPrivate>())
{
    d->dir.setPath(directory);
    d->dir.mkpath(QStringLiteral("."));
    d->index.setFileName(d->dir.filePath(IndexFile));
    d->index.open(QIODevice::WriteOnly | QIODevice::Append);
}

MediaRecorder::~MediaRecorder() = default;

void MediaRecorder::observe(QNetworkReply* reply)
{
    if (!reply || reply->operation() != QNetworkAccessManager::GetOperation)
        return;
    // Connected before the consumer gets the reply, so this runs first and can peek.
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply] {
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QString key = mediaKey(reply->url());
        const QByteArray body = reply->peek(reply->bytesAvailable());
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

        QMutexLocker lock(&d->mutex);
        if (d->files.contains(key))
            return;
        const QString file = fileFor(key);
        QFile out(d->dir.filePath(file));
        if (!out.open(QIODevice::WriteOnly))
            return;
        out.write(body);
        d->files.insert(key, file);
        const QJsonObject entry { { "key", key }, { "url", reply->url().toString() }, { "file", file },
            { "contentType", contentType }, { "size", body.size() } };
        d->index.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
        d->index.flush();
    }, Qt::DirectConnection);
}

int MediaRecorder::count() const
{
    QMutexLocker lock(&d->mutex);
    return d->files.size();
}

class MediaLibraryPrivate
{
public:
    struct Entry
    {
        QString file;
        QString contentType;
    };
    QDir dir;
    QHash<QString, Entry> entries;
};

MediaLibrary::MediaLibrary(const QString& directory)
    : d(std::make_unique<MediaLibraryPrivate>())
{
    d->dir.setPath(directory);
    QFile index(d->dir.filePath(IndexFile));
    if (!index.open(QIODevice::ReadOnly))
        return;
    while (!index.atEnd()) {
        const QJsonObject entry = QJsonDocument::fromJson(index.readLine()).object();
        if (!entry.isEmpty())
            d->entries.insert(entry["key"].toString(), { entry["file"].toString(), entry["contentType"].toString() });
    }
}

MediaLibrary::~MediaLibrary() = default;

int MediaLibrary::size() const
{
    return d->entries.size();
}

bool MediaLibrary::contains(const QUrl& url) const
{
    return d->entries.contains(mediaKey(url));
}

QNetworkReply* MediaLibrary::reply(const QNetworkRequest& request, QObject* parent) const
{
    const auto it = d->entries.constFind(mediaKey(request.url()));
    if (it == d->entries.cend())
        return nullptr;
    QFile file(d->dir.filePath(it->file));
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    return new RecordedReply(request, file.readAll(), it->contentType, parent);
}

} // namespace StatusReplay
