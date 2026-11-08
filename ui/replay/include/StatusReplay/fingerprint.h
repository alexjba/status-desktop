#pragma once

#include <StatusReplay/statusreplay.h>

#include <QJsonObject>
#include <QStringList>

class QQmlEngine;

namespace StatusReplay {

// The process and engine setup QML depends on (app identity, env, import paths, engine
// helpers, rendering). The recorder stores the real app's; replay compares its own against it.
STATUSREPLAY_EXPORT QJsonObject setupFingerprint(QQmlEngine* engine);

// Absolute import paths the app added itself (Nim libraries' QML dirs), which replay re-adds.
STATUSREPLAY_EXPORT QStringList extraImportPaths(QQmlEngine* engine);

// One line per differing key, empty when both match.
STATUSREPLAY_EXPORT QStringList fingerprintDifferences(const QJsonObject& recorded, const QJsonObject& actual);

} // namespace StatusReplay
