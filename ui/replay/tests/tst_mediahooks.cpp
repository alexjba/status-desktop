#include <StatusReplay/media.h>

#include <StatusQ/networkaccessfactory.h>

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

extern "C" void statusq_setNetworkReplyTap(void (*tap)(void* reply));
extern "C" void statusq_setNetworkRequestInterceptor(void* (*interceptor)(const void* request, void* parent));

using namespace StatusReplay;

namespace {

StatusReplay::MediaRecorder* s_recorder = nullptr;
const StatusReplay::MediaLibrary* s_library = nullptr;

QByteArray png()
{
    QImage image(23, 17, QImage::Format_ARGB32);
    image.fill(QColor(10, 200, 10, 100));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    return bytes;
}

QUrl dataUrl(const QByteArray& bytes)
{
    return QUrl(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64()));
}

// As QML does: gets the reply from the manager, then connects to it.
QByteArray fetch(QNetworkAccessManager* manager, const QUrl& url)
{
    std::unique_ptr<QNetworkReply> reply(manager->get(QNetworkRequest(url)));
    QSignalSpy finished(reply.get(), &QNetworkReply::finished);
    if (!finished.wait(5000))
        return {};
    return reply->readAll();
}

QSize decodedSize(const QByteArray& bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    return QImageReader(&buffer).read().size();
}

} // namespace

// The recorder and replay hooked into StatusQ's network managers, the way the app runs them.
class MediaHooksTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_cache;
    QTemporaryDir m_media;
    std::unique_ptr<Status::NetworkAccessFactory> m_factory;
    std::unique_ptr<QNetworkAccessManager> m_manager;

private slots:
    void init()
    {
        m_factory = std::make_unique<Status::NetworkAccessFactory>(m_cache.path(), 1 << 20);
        m_manager.reset(m_factory->create(nullptr));
    }

    void cleanup()
    {
        statusq_setNetworkReplyTap(nullptr);
        statusq_setNetworkRequestInterceptor(nullptr);
        m_manager.reset();
    }

    void withoutHooksRepliesAreTheNetworksAndNothingIsRecorded()
    {
        MediaRecorder recorder(m_media.path());
        QCOMPARE(fetch(m_manager.get(), dataUrl(png())), png());
        QCOMPARE(recorder.count(), 0);
    }

    // The tap runs while the manager creates the reply, before QML connects to it, so the
    // recorder still sees the whole body and QML still reads all of it.
    void theTapRecordsBeforeTheConsumerReads()
    {
        MediaRecorder recorder(m_media.path());
        s_recorder = &recorder;
        statusq_setNetworkReplyTap([](void* reply) { s_recorder->observe(static_cast<QNetworkReply*>(reply)); });
        QCOMPARE(fetch(m_manager.get(), dataUrl(png())), png());
        QCOMPARE(recorder.count(), 1);
        statusq_setNetworkReplyTap(nullptr);
        s_recorder = nullptr;
    }

    void theInterceptorAnswersRecordedGetsOnly()
    {
        {
            MediaRecorder recorder(m_media.path());
            s_recorder = &recorder;
            statusq_setNetworkReplyTap([](void* reply) { s_recorder->observe(static_cast<QNetworkReply*>(reply)); });
            fetch(m_manager.get(), dataUrl(png()));
            statusq_setNetworkReplyTap(nullptr);
            s_recorder = nullptr;
        }
        MediaLibrary library(m_media.path());
        s_library = &library;
        statusq_setNetworkRequestInterceptor([](const void* request, void* parent) -> void* {
            return s_library->reply(*static_cast<const QNetworkRequest*>(request), static_cast<QObject*>(parent));
        });

        const QByteArray served = fetch(m_manager.get(), dataUrl(png()));
        QVERIFY(served != png());
        QCOMPARE(decodedSize(served), QSize(23, 17));
        // Not recorded: the network answers.
        QImage other(5, 5, QImage::Format_RGB32);
        other.fill(Qt::red);
        QByteArray otherPng;
        QBuffer buffer(&otherPng);
        buffer.open(QIODevice::WriteOnly);
        other.save(&buffer, "png");
        QCOMPARE(fetch(m_manager.get(), dataUrl(otherPng)), otherPng);

        // Removed: recorded URLs go to the network again.
        statusq_setNetworkRequestInterceptor(nullptr);
        s_library = nullptr;
        QCOMPARE(fetch(m_manager.get(), dataUrl(png())), png());
    }
};

QTEST_GUILESS_MAIN(MediaHooksTest)
#include "tst_mediahooks.moc"
