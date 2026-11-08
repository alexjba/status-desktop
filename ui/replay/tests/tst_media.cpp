#include <StatusReplay/media.h>

#include <QBuffer>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

// Three 21x13 frames, written by an independent (Python) GIF encoder.
const QByteArray AnimatedGif = QByteArray::fromBase64(
    "R0lGODlhFQANAPEAAAAAAP8AAAD/AAAA/yH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAFQANAAACDoSPqcvtD6OctNqLs8YFACH5BAAKAAAA"
    "LAAAAAAVAA0AAAIOjI+py+0Po5y02ouzxgUAIfkEAAoAAAAsAAAAABUADQAAAg6Uj6nL7Q+jnLTai7PGBQA7");

QByteArray encoded(const QImage& image, const char* format)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

QByteArray pngWithAlpha()
{
    QImage image(37, 23, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter(&image).fillRect(5, 5, 10, 10, QColor(200, 10, 10, 128));
    return encoded(image, "png");
}

QByteArray opaqueJpeg()
{
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(QColor(10, 120, 200));
    return encoded(image, "jpeg");
}

QUrl dataUrl(const char* type, const QByteArray& bytes)
{
    return QUrl(QStringLiteral("data:%1;base64,%2").arg(QLatin1String(type), QString::fromLatin1(bytes.toBase64())));
}

// data: replies report isFinished() at once but emit finished() on the next loop turn,
// like served replies; consumers (and the recorder) act on the signal.
QByteArray readAll(QNetworkReply* reply)
{
    QSignalSpy finished(reply, &QNetworkReply::finished);
    if (!finished.wait(5000))
        return {};
    return reply->readAll();
}

struct Decoded
{
    QSize size;
    QImage::Format format = QImage::Format_Invalid;
    int frames = 0;
    bool operator==(const Decoded& other) const
    {
        return size == other.size && format == other.format && frames == other.frames;
    }
};

Decoded decode(const QByteArray& bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer);
    const QImage first = reader.read();
    return { first.size(), first.format(), qMax(1, reader.imageCount()) };
}

} // namespace

class MediaTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QList<QPair<QUrl, QByteArray>> m_originals;

    void record(MediaRecorder::Mode mode)
    {
        MediaRecorder recorder(m_dir.path(), mode);
        QNetworkAccessManager manager;
        for (const auto& [url, bytes] : std::as_const(m_originals)) {
            QNetworkReply* reply = manager.get(QNetworkRequest(url));
            recorder.observe(reply);
            // The consumer still gets the whole body: recording does not take it.
            QCOMPARE(readAll(reply), bytes);
            reply->deleteLater();
        }
        QCOMPARE(recorder.count(), m_originals.size());
    }

private slots:
    void init()
    {
        m_dir.remove();
        m_dir = QTemporaryDir();
        m_originals = {
            { dataUrl("image/png", pngWithAlpha()), pngWithAlpha() },
            { dataUrl("image/jpeg", opaqueJpeg()), opaqueJpeg() },
            { dataUrl("image/gif", AnimatedGif), AnimatedGif },
            { dataUrl("application/json", "{\"price\": 1.25}"), "{\"price\": 1.25}" },
        };
    }

    void localMediaServerUrlsMatchAcrossPortsAndHosts()
    {
        // status-go's media server gets a new port every run.
        QCOMPARE(mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x1")),
            mediaKey(QUrl("https://127.0.0.1:61000/messages/images?messageId=0x1")));
        QVERIFY(mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x1"))
            != mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x2")));
        QVERIFY(mediaKey(QUrl("https://example.org:443/a.png")) != mediaKey(QUrl("https://example.org:8443/a.png")));
    }

    void defaultRecordingsKeepNoBodies()
    {
        record(MediaRecorder::Mode::Placeholders);
        QCOMPARE(QDir(m_dir.path()).entryList(QDir::Files), QStringList { "index.jsonl" });
        QFile index(m_dir.filePath("index.jsonl"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QByteArray content = index.readAll();
        QVERIFY(!content.contains("price"));
        QVERIFY(content.contains("\"width\":37"));
    }

    void placeholdersDecodeLikeTheOriginals()
    {
        record(MediaRecorder::Mode::Placeholders);
        MediaLibrary library(m_dir.path());
        QCOMPARE(library.size(), m_originals.size());
        for (const auto& [url, bytes] : std::as_const(m_originals)) {
            std::unique_ptr<QNetworkReply> served(library.reply(QNetworkRequest(url), nullptr));
            QVERIFY(served);
            const QByteArray placeholder = readAll(served.get());
            QCOMPARE(served->error(), QNetworkReply::NoError);
            QVERIFY(placeholder != bytes);
            if (url.toString().startsWith(QLatin1String("data:image"))) {
                const Decoded original = decode(bytes);
                const Decoded replayed = decode(placeholder);
                QVERIFY2(replayed == original,
                    qPrintable(QStringLiteral("%1: %2x%3 fmt %4 frames %5, original %6x%7 fmt %8 frames %9")
                                   .arg(url.toString().left(20))
                                   .arg(replayed.size.width())
                                   .arg(replayed.size.height())
                                   .arg(int(replayed.format))
                                   .arg(replayed.frames)
                                   .arg(original.size.width())
                                   .arg(original.size.height())
                                   .arg(int(original.format))
                                   .arg(original.frames)));
            } else {
                QCOMPARE(placeholder.size(), bytes.size());
                QCOMPARE(served->header(QNetworkRequest::ContentTypeHeader).toString(),
                    QStringLiteral("application/json"));
            }
        }
        QVERIFY(!library.reply(QNetworkRequest(QUrl("https://localhost:1/unknown.png")), nullptr));
    }

    void bytesModeServesTheOriginals()
    {
        record(MediaRecorder::Mode::Bytes);
        MediaLibrary library(m_dir.path());
        for (const auto& [url, bytes] : std::as_const(m_originals)) {
            std::unique_ptr<QNetworkReply> served(library.reply(QNetworkRequest(url), nullptr));
            QVERIFY(served);
            QCOMPARE(readAll(served.get()), bytes);
        }
    }
};

QTEST_GUILESS_MAIN(MediaTest)
#include "tst_media.moc"
