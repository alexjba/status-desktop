#include <StatusReplay/media.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

const QByteArray PngBytes = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");

// Both data: replies and served ones report isFinished() at once but emit finished() on the
// next loop turn; consumers (and the recorder) act on the signal.
QByteArray readAll(QNetworkReply* reply)
{
    QSignalSpy finished(reply, &QNetworkReply::finished);
    if (!finished.wait(5000))
        return {};
    return reply->readAll();
}

} // namespace

class MediaTest : public QObject
{
    Q_OBJECT

private slots:
    void localMediaServerUrlsMatchAcrossPortsAndHosts()
    {
        // status-go's media server gets a new port every run.
        QCOMPARE(mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x1")),
            mediaKey(QUrl("https://127.0.0.1:61000/messages/images?messageId=0x1")));
        QVERIFY(mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x1"))
            != mediaKey(QUrl("https://localhost:54321/messages/images?messageId=0x2")));
        QVERIFY(mediaKey(QUrl("https://example.org:443/a.png")) != mediaKey(QUrl("https://example.org:8443/a.png")));
    }

    void recordedResponsesAreServedBack()
    {
        QTemporaryDir dir;
        const QUrl url(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(PngBytes.toBase64()));
        {
            MediaRecorder recorder(dir.path());
            QNetworkAccessManager manager;
            QNetworkReply* reply = manager.get(QNetworkRequest(url));
            recorder.observe(reply);
            // The consumer still gets the whole body: recording does not take it.
            QCOMPARE(readAll(reply), PngBytes);
            QCOMPARE(recorder.count(), 1);
            reply->deleteLater();
        }

        MediaLibrary library(dir.path());
        QCOMPARE(library.size(), 1);
        QVERIFY(library.contains(url));
        std::unique_ptr<QNetworkReply> served(library.reply(QNetworkRequest(url), nullptr));
        QVERIFY(served);
        QCOMPARE(readAll(served.get()), PngBytes);
        QCOMPARE(served->error(), QNetworkReply::NoError);
        QCOMPARE(served->header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("image/png"));

        QVERIFY(!library.reply(QNetworkRequest(QUrl("https://localhost:1/unknown.png")), nullptr));
    }
};

QTEST_GUILESS_MAIN(MediaTest)
#include "tst_media.moc"
