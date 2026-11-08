#pragma once

#include <StatusReplay/statusreplay.h>

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace StatusReplay {

// The app's own default QSettings (where QML `Settings` persist) as they were when taken, without
// fallbacks (organization-wide, or the macOS global domain on native settings).
class STATUSREPLAY_EXPORT SettingsSnapshot
{
public:
    SettingsSnapshot();

    // Writes the snapshot to an ini file, keeping only unkeyed groups and groups keyed by one of
    // `profileIds` (the recorded profile's public key / key UID): a developer's QSettings also hold
    // their other profiles, accounts' addresses and communities, which never go into a recording.
    bool write(const QString& file, const QStringList& profileIds) const;

private:
    QVariantMap m_values;
};

// Points the process's default QSettings at a fresh ini store in `directory` (so replay never
// touches the real app's settings) and fills it from a snapshot. Call before anything reads
// QSettings.
STATUSREPLAY_EXPORT void useReplaySettings(const QString& snapshot, const QString& directory);

} // namespace StatusReplay
