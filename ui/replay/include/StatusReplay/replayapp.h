#pragma once

#include <StatusReplay/statusreplay.h>

#include <QJsonObject>
#include <QString>

class QQmlApplicationEngine;

namespace StatusReplay {

// Mirrors the Qt-side startup in src/nim_status_client.nim; the setup fingerprint keeps the
// two in sync. Split around QGuiApplication construction like the Nim startup.
STATUSREPLAY_EXPORT void prepareProcess(const QJsonObject& manifest);
STATUSREPLAY_EXPORT void prepareApplication(const QJsonObject& manifest);
STATUSREPLAY_EXPORT void prepareEngine(QQmlApplicationEngine* engine, const QJsonObject& manifest,
                                       const QString& tempDirectory);

} // namespace StatusReplay

extern "C" {
// Replay entry point: `--recording <dir>` (or STATUS_REPLAY), `--moment <name>`,
// `--mode realtime|step|frozen`, `--speed <factor>`.
STATUSREPLAY_EXPORT int statusreplay_main(int argc, char** argv);
}
