#include "StatusQ/nativesound.h"

#ifdef Q_OS_ANDROID
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJniObject>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace {
Q_LOGGING_CATEGORY(lcMedia, "status.media")

// SoundPool needs a file: copy a qrc sound to the cache dir once per run
QString localSoundPath(const QUrl& source)
{
    static QHash<QUrl, QString> paths;
    if (auto it = paths.constFind(source); it != paths.cend())
        return *it;
    QString path;
    if (source.isLocalFile()) {
        path = source.toLocalFile();
    } else if (source.scheme() == QLatin1String("qrc")) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                + QStringLiteral("/sounds");
        QDir().mkpath(dir);
        path = dir + QLatin1Char('/') + source.fileName();
        QFile::remove(path);
        if (!QFile::copy(QLatin1Char(':') + source.path(), path)) {
            qCWarning(lcMedia) << "cannot extract" << source;
            path.clear();
        }
    }
    paths.insert(source, path);
    return path;
}
} // namespace
#endif

NativeSound::NativeSound(QObject* parent)
    : QObject(parent)
{}

#ifdef Q_OS_ANDROID
bool NativeSound::available() const
{
    return true;
}

void NativeSound::play(const QUrl& source, qreal volume)
{
    const QString path = localSoundPath(source);
    if (path.isEmpty())
        return;
    QJniObject::callStaticMethod<void>("app/status/mobile/SoundPlayer", "play",
                                       "(Ljava/lang/String;F)V",
                                       QJniObject::fromString(path).object<jstring>(),
                                       jfloat(volume));
}

void NativeSound::stop(const QUrl& source)
{
    const QString path = localSoundPath(source);
    if (path.isEmpty())
        return;
    QJniObject::callStaticMethod<void>("app/status/mobile/SoundPlayer", "stop",
                                       "(Ljava/lang/String;)V",
                                       QJniObject::fromString(path).object<jstring>());
}
#else
bool NativeSound::available() const
{
    return false;
}

void NativeSound::play(const QUrl&, qreal) {}

void NativeSound::stop(const QUrl&) {}
#endif
