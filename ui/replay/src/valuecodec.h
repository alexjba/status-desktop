#pragma once

#include <QJsonValue>
#include <QMetaType>
#include <QVariant>

#include <functional>

class QObject;

namespace StatusReplay::ValueCodec {

using ObjectToId = std::function<int(QObject*)>;
using IdToObject = std::function<QObject*(int)>;

// Plain JSON when `declared` is a concrete scalar type, tagged otherwise (see docs/replay-mode.md).
QJsonValue encode(const QVariant& value, QMetaType declared, const ObjectToId& idFor);
QVariant decode(const QJsonValue& json, QMetaType declared, const IdToObject& objectFor);

// Reads/writes a value in metacall argument storage of the given type.
QVariant read(const void* storage, QMetaType type);
void write(void* storage, QMetaType type, const QVariant& value);

} // namespace StatusReplay::ValueCodec
