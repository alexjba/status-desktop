#pragma once

#include <StatusReplay/statusreplay.h>

#include <QList>
#include <QPair>
#include <QString>

class QQmlEngine;

namespace StatusReplay {

// Calibration probe (docs/replay-mode.md, real vs replay): at each of `seconds` after start,
// writes <directory>/probe-<n>.tsv with the composite QML types the engine loaded and
// instantiated, plus the malloc heap in use, and on Linux/Android probe-<n>-smaps.tsv from
// pssPerMapping() of the process's own smaps (readable without run-as). Works the same in the recorded app and in replay.
// PSS + SwapPss in kB per mapped file name (or anonymous region kind) of a /proc/<pid>/smaps
// text, largest first.
STATUSREPLAY_EXPORT QList<QPair<QString, qint64>> pssPerMapping(const QByteArray& smaps);

STATUSREPLAY_EXPORT void startProbe(QQmlEngine* engine, const QString& seconds, const QString& directory);

} // namespace StatusReplay
