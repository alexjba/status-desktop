#pragma once

#include <QString>

namespace StatusReplay::Format {

// Major changes break readers; minor ones only add fields.
inline constexpr int Major = 1;
inline constexpr int Minor = 0;

inline const QString Manifest = QStringLiteral("manifest.json");
inline const QString Timeline = QStringLiteral("timeline.jsonl");
inline const QString SchemasDir = QStringLiteral("schemas");
inline const QString SnapshotsDir = QStringLiteral("snapshots");

} // namespace StatusReplay::Format
