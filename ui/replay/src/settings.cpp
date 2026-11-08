#include <StatusReplay/settings.h>

#include <QDir>
#include <QFile>
#include <QSettings>

namespace StatusReplay {

bool snapshotSettings(const QString& file)
{
    QFile::remove(file);
    QSettings source;
    QSettings snapshot(file, QSettings::IniFormat);
    const QStringList keys = source.allKeys();
    for (const auto& key : keys)
        snapshot.setValue(key, source.value(key));
    snapshot.sync();
    return snapshot.status() == QSettings::NoError;
}

void useReplaySettings(const QString& snapshot, const QString& directory)
{
    QDir(directory).removeRecursively();
    QDir().mkpath(directory);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    if (!QFile::exists(snapshot))
        return;
    const QSettings recorded(snapshot, QSettings::IniFormat);
    QSettings target;
    const QStringList keys = recorded.allKeys();
    for (const auto& key : keys)
        target.setValue(key, recorded.value(key));
}

} // namespace StatusReplay
