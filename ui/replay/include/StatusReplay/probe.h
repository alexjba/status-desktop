#pragma once

#include <StatusReplay/statusreplay.h>

#include <QString>

class QQmlEngine;

namespace StatusReplay {

// Calibration probe (docs/replay-mode.md, real vs replay): at each of `seconds` after start,
// writes <directory>/probe-<n>.tsv with the composite QML types the engine loaded and
// instantiated, plus the malloc heap in use. Works the same in the recorded app and in replay.
STATUSREPLAY_EXPORT void startProbe(QQmlEngine* engine, const QString& seconds, const QString& directory);

} // namespace StatusReplay
