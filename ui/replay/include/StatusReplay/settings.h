#pragma once

#include <StatusReplay/statusreplay.h>

#include <QString>

namespace StatusReplay {

// Copies the app's default QSettings (where QML `Settings` persist) into an ini file.
STATUSREPLAY_EXPORT bool snapshotSettings(const QString& file);

// Points the process's default QSettings at a fresh ini store in `directory` (so replay never
// touches the real app's settings) and fills it from a snapshot. Call before anything reads
// QSettings.
STATUSREPLAY_EXPORT void useReplaySettings(const QString& snapshot, const QString& directory);

} // namespace StatusReplay
