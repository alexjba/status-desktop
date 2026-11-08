#include <StatusReplay/media.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
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

// GIF with `frames` solid frames (palette index 0), the shape QGifHandler decodes to frames of
// `width` x `height`; the index is transparent when the original's frames had alpha. Qt can read
// GIFs but not write them.
QByteArray solidGif(int width, int height, int frames, bool transparent)
{
    QByteArray gif("GIF89a");
    auto u16 = [&gif](int v) {
        gif.append(char(v & 0xff));
        gif.append(char((v >> 8) & 0xff));
    };
    u16(width);
    u16(height);
    gif.append(char(0xF0)); // global color table, 2 entries
    gif.append('\0');
    gif.append('\0');
    gif.append(QByteArray(6, char(0x80)));
    if (frames > 1)
        gif.append(QByteArray::fromHex("21ff0b4e45545343415045322e300301000000"));

    // LZW with 2-bit minimum code size: emit "clear" often enough that codes stay 3 bits wide.
    QByteArray data;
    quint32 bits = 0;
    int bitCount = 0;
    auto emitCode = [&](int code) {
        bits |= quint32(code) << bitCount;
        bitCount += 3;
        while (bitCount >= 8) {
            data.append(char(bits & 0xff));
            bits >>= 8;
            bitCount -= 8;
        }
    };
    const qint64 pixels = qint64(width) * height;
    for (qint64 i = 0; i < pixels; ++i) {
        if (i % 2 == 0)
            emitCode(4); // clear
        emitCode(0);
    }
    emitCode(5); // end of information
    if (bitCount > 0)
        data.append(char(bits & 0xff));

    for (int f = 0; f < frames; ++f) {
        gif.append(QByteArray::fromHex("21f904"));
        gif.append(char(transparent ? 0x09 : 0x08)); // dispose to background (+transparency)
        u16(10);
        gif.append('\0'); // transparent index
        gif.append('\0');
        gif.append(',');
        u16(0);
        u16(0);
        u16(width);
        u16(height);
        gif.append('\0');
        gif.append(char(2));
        for (qsizetype at = 0; at < data.size(); at += 255) {
            const QByteArray chunk = data.mid(at, 255);
            gif.append(char(chunk.size()));
            gif.append(chunk);
        }
        gif.append('\0');
    }
    gif.append(';');
    return gif;
}

// An image that decodes to the same size, pixel format and frame count as the recorded one.
QByteArray placeholderImage(const QJsonObject& entry)
{
    const int width = entry["width"].toInt();
    const int height = entry["height"].toInt();
    const int frames = entry["frames"].toInt(1);
    const QByteArray format = entry["format"].toString().toLatin1();
    const auto pixelFormat = QImage::Format(entry["pixelFormat"].toInt(int(QImage::Format_ARGB32)));
    const bool alpha = QImage::toPixelFormat(pixelFormat).alphaUsage() == QPixelFormat::UsesAlpha;

    if (format == "gif" || frames > 1)
        return solidGif(width, height, frames, alpha);
    if (format == "svg" || format == "svgz")
        return QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" width="%1" height="%2"/>)")
            .arg(width)
            .arg(height)
            .toUtf8();

    QImage image(width, height, pixelFormat == QImage::Format_Invalid ? QImage::Format_ARGB32 : pixelFormat);
    image.fill(alpha ? QColor(Qt::transparent) : QColor(Qt::gray));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const bool writable = QImageWriter::supportedImageFormats().contains(format);
    image.save(&buffer, writable ? format.constData() : "png");
    return bytes;
}

// A finished reply with a recorded body. Signals go out on the next loop turn, like a real
// reply's, after the caller connected to them.
class RecordedReply : public QNetworkReply
{
public:
    RecordedReply(const QNetworkRequest& request, const QByteArray& body, const QString& contentType, int status,
        QObject* parent)
        : QNetworkReply(parent)
        , m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        setHeader(QNetworkRequest::ContentLengthHeader, m_body.size());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
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
    MediaRecorder::Mode mode;
    QDir dir;
    QFile index;
    QMutex mutex;
    QHash<QString, QString> files;
};

MediaRecorder::MediaRecorder(const QString& directory, Mode mode)
    : d(std::make_unique<MediaRecorderPrivate>())
{
    d->mode = mode;
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
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        QJsonObject entry { { "key", key }, { "contentType", contentType }, { "size", body.size() },
            { "status", status.isValid() ? status.toInt() : 200 } };
        QBuffer buffer;
        buffer.setData(body);
        QImageReader reader(&buffer);
        if (reader.canRead()) {
            entry["format"] = QString::fromLatin1(reader.format());
            entry["frames"] = qMax(1, reader.imageCount());
            // Decoded, not declared: handlers report e.g. ARGB32 up front and decode to RGB32.
            const QImage first = reader.read();
            entry["width"] = first.width();
            entry["height"] = first.height();
            entry["pixelFormat"] = int(first.format());
        }

        QMutexLocker lock(&d->mutex);
        if (d->files.contains(key))
            return;
        QString file;
        if (d->mode == MediaRecorder::Mode::Bytes) {
            file = fileFor(key);
            QFile out(d->dir.filePath(file));
            if (!out.open(QIODevice::WriteOnly))
                return;
            out.write(body);
            entry["file"] = file;
            entry["url"] = reply->url().toString();
        }
        d->files.insert(key, file);
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
    QDir dir;
    QHash<QString, QJsonObject> entries;
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
            d->entries.insert(entry["key"].toString(), entry);
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
    const QJsonObject& entry = *it;
    QByteArray body;
    if (const QString file = entry["file"].toString(); !file.isEmpty()) {
        QFile in(d->dir.filePath(file));
        if (in.open(QIODevice::ReadOnly))
            body = in.readAll();
    } else if (entry.contains("width")) {
        body = placeholderImage(entry);
    } else {
        body = QByteArray(entry["size"].toInt(), ' ');
    }
    return new RecordedReply(request, body, entry["contentType"].toString(), entry["status"].toInt(200), parent);
}

} // namespace StatusReplay
