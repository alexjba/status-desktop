#pragma once

#include <QString>

namespace StatusReplay::Format {

// Major changes break readers; minor ones add fields or change what readers still accept.
// 1.1: media keys are SHA-256 hashes (1.0: plain keys, still read).
inline constexpr int Major = 1;
inline constexpr int Minor = 1;

inline const QString Manifest = QStringLiteral("manifest.json");
inline const QString Timeline = QStringLiteral("timeline.jsonl");
inline const QString SchemasDir = QStringLiteral("schemas");
inline const QString SnapshotsDir = QStringLiteral("snapshots");

} // namespace StatusReplay::Format
