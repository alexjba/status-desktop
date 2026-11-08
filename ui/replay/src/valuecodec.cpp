#include "valuecodec.h"

#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace StatusReplay::ValueCodec {

namespace {

constexpr qint64 MaxSafeInteger = (qint64(1) << 53);

bool isQObjectPointer(QMetaType type)
{
    return type.flags().testFlag(QMetaType::PointerToQObject);
}

bool isScalar(QMetaType type)
{
    switch (type.id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::QString:
        return true;
    default:
        return false;
    }
}

QJsonValue encodeScalar(const QVariant& value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::QString:
        return value.toString();
    case QMetaType::LongLong: {
        const qint64 v = value.toLongLong();
        if (v > MaxSafeInteger || v < -MaxSafeInteger)
            return value.toString();
        return v;
    }
    case QMetaType::ULongLong: {
        const quint64 v = value.toULongLong();
        if (v > quint64(MaxSafeInteger))
            return value.toString();
        return qint64(v);
    }
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    default:
        return value.toInt();
    }
}

QJsonObject tagged(const char* type, const QJsonValue& v)
{
    return { { "$t", QString::fromLatin1(type) }, { "v", v } };
}

} // namespace

QJsonValue encode(const QVariant& input, QMetaType declared, const ObjectToId& idFor)
{
    QVariant value = input;
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        value = value.value<QJSValue>().toVariant();

    if (!value.isValid())
        return QJsonValue::Null;

    const QMetaType type = value.metaType();
    if (isQObjectPointer(type)) {
        QObject* object = value.value<QObject*>();
        if (!object)
            return QJsonValue::Null;
        const int id = idFor(object);
        if (id < 0)
            return QJsonObject { { "$ext", QString::fromLatin1(object->metaObject()->className()) } };
        return QJsonObject { { "$obj", id } };
    }

    if (isScalar(type)) {
        const QJsonValue plain = encodeScalar(value);
        const bool selfDescribing = type.id() == QMetaType::QString || type.id() == QMetaType::Bool;
        if (type == declared || selfDescribing)
            return plain;
        return tagged(type.name(), plain);
    }

    switch (type.id()) {
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QJsonArray items;
        for (const auto& item : value.toList())
            items.append(encode(item, QMetaType::fromType<QVariant>(), idFor));
        QJsonObject result { { "$list", items } };
        if (type.id() == QMetaType::QStringList)
            result["$t"] = QStringLiteral("QStringList");
        return result;
    }
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash: {
        QJsonObject items;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            items.insert(it.key(), encode(it.value(), QMetaType::fromType<QVariant>(), idFor));
        QJsonObject result { { "$map", items } };
        if (type.id() == QMetaType::QVariantHash)
            result["$t"] = QStringLiteral("QVariantHash");
        return result;
    }
    case QMetaType::QByteArray:
        return tagged("bytes", QString::fromLatin1(value.toByteArray().toBase64()));
    default:
        break;
    }

    if (value.canConvert<QString>())
        return tagged(type.name(), value.toString());
    return QJsonObject { { "$t", QString::fromLatin1(type.name()) }, { "unsupported", true } };
}

QVariant decode(const QJsonValue& json, QMetaType declared, const IdToObject& objectFor)
{
    const bool declaredIsVariant = !declared.isValid() || declared == QMetaType::fromType<QVariant>();

    auto toDeclared = [&](QVariant value) {
        if (declaredIsVariant || value.metaType() == declared)
            return value;
        if (value.convert(declared))
            return value;
        return QVariant(declared);
    };

    if (json.isNull() || json.isUndefined()) {
        if (declaredIsVariant)
            return {};
        if (isQObjectPointer(declared))
            return QVariant(declared, nullptr);
        return QVariant(declared);
    }

    if (!json.isObject()) {
        QVariant plain = json.toVariant();
        if (declared.id() == QMetaType::LongLong)
            plain = json.isString() ? QVariant(json.toString().toLongLong()) : QVariant(qint64(json.toDouble()));
        else if (declared.id() == QMetaType::ULongLong)
            plain = json.isString() ? QVariant(json.toString().toULongLong()) : QVariant(quint64(json.toDouble()));
        return toDeclared(plain);
    }

    const QJsonObject object = json.toObject();
    if (object.contains("$redacted"))
        return declaredIsVariant ? QVariant() : QVariant(declared);
    if (object.contains("$ext"))
        return isQObjectPointer(declared) ? QVariant(declared, nullptr) : QVariant();
    if (object.contains("$obj")) {
        QObject* target = objectFor(object["$obj"].toInt());
        if (isQObjectPointer(declared)) {
            QVariant v = QVariant::fromValue(target);
            if (v.convert(declared))
                return v;
            return QVariant(declared, nullptr);
        }
        return QVariant::fromValue(target);
    }
    if (object.contains("$list")) {
        QVariantList items;
        for (const auto& item : object["$list"].toArray())
            items.append(decode(item, QMetaType::fromType<QVariant>(), objectFor));
        if (object["$t"].toString() == QLatin1String("QStringList"))
            return toDeclared(QVariant(items).toStringList());
        return toDeclared(items);
    }
    if (object.contains("$map")) {
        QVariantMap items;
        const QJsonObject map = object["$map"].toObject();
        for (auto it = map.begin(); it != map.end(); ++it)
            items.insert(it.key(), decode(it.value(), QMetaType::fromType<QVariant>(), objectFor));
        if (object["$t"].toString() == QLatin1String("QVariantHash")) {
            QVariantHash hash;
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                hash.insert(it.key(), it.value());
            return toDeclared(hash);
        }
        return toDeclared(items);
    }

    const QString typeName = object["$t"].toString();
    if (object["unsupported"].toBool())
        return declaredIsVariant ? QVariant() : QVariant(declared);
    const QJsonValue v = object["v"];
    if (typeName == QLatin1String("bytes"))
        return toDeclared(QByteArray::fromBase64(v.toString().toLatin1()));

    const QMetaType type = QMetaType::fromName(typeName.toLatin1());
    QVariant result = v.toVariant();
    if (v.isString() && type.id() == QMetaType::LongLong)
        result = v.toString().toLongLong();
    else if (v.isString() && type.id() == QMetaType::ULongLong)
        result = v.toString().toULongLong();
    if (type.isValid() && !result.convert(type))
        result = QVariant(type);
    return toDeclared(result);
}

QVariant read(const void* storage, QMetaType type)
{
    if (type == QMetaType::fromType<QVariant>())
        return *static_cast<const QVariant*>(storage);
    return QVariant(type, storage);
}

void write(void* storage, QMetaType type, const QVariant& value)
{
    if (!storage || !type.isValid() || type.id() == QMetaType::Void)
        return;
    if (type == QMetaType::fromType<QVariant>()) {
        *static_cast<QVariant*>(storage) = value;
        return;
    }
    QVariant converted = value;
    if (converted.metaType() != type && !converted.convert(type))
        converted = QVariant(type);
    type.destruct(storage);
    type.construct(storage, converted.constData());
}

} // namespace StatusReplay::ValueCodec
