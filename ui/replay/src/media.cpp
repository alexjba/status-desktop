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

// Recordings name resources by a hash of their key, not their URL (which can carry ids).
QString storedKey(const QUrl& url)
{
    return QString::fromLatin1(QCryptographicHash::hash(mediaKey(url).toUtf8(), QCryptographicHash::Sha256).toHex());
}

// Whether a GIF's first frame has a transparent index (its graphic control extension's flag):
// that decides between RGB32 and ARGB32 frames, and the header alone doesn't say.
bool gifTransparent(const QByteArray& gif)
{
    const qsizetype at = gif.indexOf(QByteArray::fromHex("21f904"));
    return at >= 0 && at + 3 < gif.size() && (gif.at(at + 3) & 0x01);
}

// Whether a paletted PNG carries transparency: a tRNS chunk before the image data. Qt reports
// Indexed8 either way.
bool pngHasTransparencyChunk(const QByteArray& png)
{
    const qsizetype data = png.indexOf("IDAT");
    const qsizetype trns = png.indexOf("tRNS");
    return trns >= 0 && (data < 0 || trns < data);
}

// Whether a WebP has alpha, from its first chunk: lossy VP8 never, VP8L its alpha_is_used bit,
// VP8X its alpha flag. Qt reports ARGB32 up front and decodes opaque ones to RGB32.
bool webpHasAlpha(const QByteArray& webp)
{
    if (webp.size() < 30 || !webp.startsWith("RIFF") || webp.mid(8, 4) != "WEBP")
        return true;
    const QByteArray chunk = webp.mid(12, 4);
    if (chunk == "VP8 ")
        return false;
    if (chunk == "VP8L")
        return (quint8(webp.at(24)) >> 4) & 0x01;
    if (chunk == "VP8X")
        return quint8(webp.at(20)) & 0x10;
    return true;
}

// What a placeholder needs, from the image header only: recording runs on QML's image reader
// thread and must not decode.
void describeImage(const QByteArray& body, QJsonObject& entry)
{
    QBuffer buffer;
    buffer.setData(body);
    QImageReader reader(&buffer);
    if (!reader.canRead())
        return;
    const QByteArray format = reader.format();
    const QSize size = reader.size();
    if (!size.isValid())
        return;
    entry["format"] = QString::fromLatin1(format);
    entry["width"] = size.width();
    entry["height"] = size.height();
    entry["frames"] = qMax(1, reader.imageCount());
    if (format == "gif") {
        entry["transparent"] = gifTransparent(body);
        return;
    }
    entry["pixelFormat"] = int(reader.imageFormat());
    if (reader.imageFormat() == QImage::Format_Indexed8)
        entry["transparent"] = pngHasTransparencyChunk(body);
    if (format == "webp" && !webpHasAlpha(body))
        entry["pixelFormat"] = int(QImage::Format_RGB32);
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
    if (format == "gif" || frames > 1)
        return solidGif(width, height, frames, entry["transparent"].toBool());
    auto pixelFormat = QImage::Format(entry["pixelFormat"].toInt(int(QImage::Format_ARGB32)));
    if (pixelFormat == QImage::Format_Invalid)
        pixelFormat = QImage::Format_ARGB32;
    const bool alpha = pixelFormat == QImage::Format_Indexed8
        ? entry["transparent"].toBool()
        : QImage::toPixelFormat(pixelFormat).alphaUsage() == QPixelFormat::UsesAlpha;
    if (format == "svg" || format == "svgz")
        return QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" width="%1" height="%2"/>)")
            .arg(width)
            .arg(height)
            .toUtf8();

    QImage image(width, height, pixelFormat);
    if (pixelFormat == QImage::Format_Indexed8) {
        // Paletted: a one-entry table (transparent if the original's was) keeps the PNG paletted;
        // without one it would be written, and read back, as opaque grayscale.
        image.setColorTable({ alpha ? qRgba(0, 0, 0, 0) : qRgb(128, 128, 128) });
        image.fill(0);
    } else {
        image.fill(alpha ? QColor(Qt::transparent) : QColor(Qt::gray));
    }
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
        // Replay looks up what the app asked for, not where a redirect ended.
        const QUrl requested = reply->request().url();
        const QString key = storedKey(requested);
        const QByteArray body = reply->peek(reply->bytesAvailable());
        // Drained by a consumer that read as data arrived (XHR): nothing left to describe.
        if (body.isEmpty())
            return;
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        QJsonObject entry { { "key", key }, { "contentType", contentType }, { "size", body.size() },
            { "status", status.isValid() ? status.toInt() : 200 } };
        describeImage(body, entry);

        QMutexLocker lock(&d->mutex);
        if (d->files.contains(key))
            return;
        QString file;
        if (d->mode == MediaRecorder::Mode::Bytes) {
            file = key + QStringLiteral(".bin");
            QFile out(d->dir.filePath(file));
            if (!out.open(QIODevice::WriteOnly))
                return;
            out.write(body);
            entry["file"] = file;
            entry["url"] = requested.toString();
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
    return d->entries.contains(storedKey(url)) || d->entries.contains(mediaKey(url));
}

QNetworkReply* MediaLibrary::reply(const QNetworkRequest& request, QObject* parent) const
{
    auto it = d->entries.constFind(storedKey(request.url()));
    // Format 1.0 recordings keyed by the plain key.
    if (it == d->entries.cend())
        it = d->entries.constFind(mediaKey(request.url()));
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
