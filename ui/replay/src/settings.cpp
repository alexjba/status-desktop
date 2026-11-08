#include <StatusReplay/settings.h>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>

namespace StatusReplay {

namespace {

// Public keys, key UIDs, addresses and community ids in group names.
bool keptFor(const QString& key, const QStringList& profileIds)
{
    static const QRegularExpression hexId(QStringLiteral("0x[0-9a-fA-F]+"));
    auto it = hexId.globalMatch(key);
    while (it.hasNext()) {
        if (!profileIds.contains(it.next().captured(), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

} // namespace

SettingsSnapshot::SettingsSnapshot()
{
    QSettings source;
    source.setFallbacksEnabled(false);
    const QStringList keys = source.allKeys();
    for (const auto& key : keys)
        m_values.insert(key, source.value(key));
}

bool SettingsSnapshot::write(const QString& file, const QStringList& profileIds) const
{
    QFile::remove(file);
    QSettings snapshot(file, QSettings::IniFormat);
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        if (keptFor(it.key(), profileIds))
            snapshot.setValue(it.key(), it.value());
    }
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
