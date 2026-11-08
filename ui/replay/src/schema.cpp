#include "schema.h"

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <private/qmetaobjectbuilder_p.h>

namespace StatusReplay {

namespace {

const char* baseName(Schema::Base base)
{
    switch (base) {
    case Schema::Base::ListModel:
        return "QAbstractListModel";
    case Schema::Base::TableModel:
        return "QAbstractTableModel";
    case Schema::Base::ItemModel:
        return "QAbstractItemModel";
    case Schema::Base::Object:
        break;
    }
    return "QObject";
}

Schema::Base baseFromName(const QString& name)
{
    if (name == QLatin1String("QAbstractListModel"))
        return Schema::Base::ListModel;
    if (name == QLatin1String("QAbstractTableModel"))
        return Schema::Base::TableModel;
    if (name == QLatin1String("QAbstractItemModel"))
        return Schema::Base::ItemModel;
    return Schema::Base::Object;
}

const char* methodTypeName(QMetaMethod::MethodType type)
{
    switch (type) {
    case QMetaMethod::Signal:
        return "signal";
    case QMetaMethod::Slot:
        return "slot";
    default:
        return "method";
    }
}

QMetaMethod::MethodType methodTypeFromName(const QString& name)
{
    if (name == QLatin1String("signal"))
        return QMetaMethod::Signal;
    if (name == QLatin1String("slot"))
        return QMetaMethod::Slot;
    return QMetaMethod::Method;
}

QJsonArray toArray(const QByteArrayList& list)
{
    QJsonArray result;
    for (const auto& item : list)
        result.append(QString::fromLatin1(item));
    return result;
}

QByteArrayList fromArray(const QJsonArray& array)
{
    QByteArrayList result;
    for (const auto& item : array)
        result.append(item.toString().toLatin1());
    return result;
}

} // namespace

const QMetaObject* Schema::baseMetaObject(Base base)
{
    switch (base) {
    case Base::ListModel:
        return &QAbstractListModel::staticMetaObject;
    case Base::TableModel:
        return &QAbstractTableModel::staticMetaObject;
    case Base::ItemModel:
        return &QAbstractItemModel::staticMetaObject;
    case Base::Object:
        break;
    }
    return &QObject::staticMetaObject;
}

Schema Schema::fromMetaObject(const QMetaObject* metaObject)
{
    Schema schema;
    schema.className = metaObject->className();
    for (auto mo = metaObject; mo; mo = mo->superClass()) {
        if (mo == &QAbstractListModel::staticMetaObject) {
            schema.base = Base::ListModel;
            break;
        }
        if (mo == &QAbstractTableModel::staticMetaObject) {
            schema.base = Base::TableModel;
            break;
        }
        if (mo == &QAbstractItemModel::staticMetaObject) {
            schema.base = Base::ItemModel;
            break;
        }
    }
    const QMetaObject* base = baseMetaObject(schema.base);

    QList<Method> signalList;
    QList<Method> others;
    QHash<int, int> signalPosition; // absolute method index -> index in signalList
    for (int i = base->methodCount(); i < metaObject->methodCount(); ++i) {
        const QMetaMethod m = metaObject->method(i);
        Method method;
        method.type = m.methodType();
        method.signature = m.methodSignature();
        method.returnType = m.typeName();
        method.parameterTypes = m.parameterTypes();
        method.parameterNames = m.parameterNames();
        if (method.type == QMetaMethod::Signal) {
            signalPosition.insert(i, signalList.size());
            signalList.append(method);
        } else if (method.type != QMetaMethod::Constructor) {
            others.append(method);
        }
    }
    schema.methods = signalList + others;

    for (int i = base->propertyCount(); i < metaObject->propertyCount(); ++i) {
        const QMetaProperty p = metaObject->property(i);
        Property property;
        property.name = p.name();
        property.type = p.typeName();
        property.writable = p.isWritable();
        property.constant = p.isConstant();
        if (p.hasNotifySignal())
            property.notify = signalPosition.value(p.notifySignalIndex(), -1);
        schema.properties.append(property);
    }

    const QByteArray canonical = QJsonDocument(schema.toJson()).toJson(QJsonDocument::Compact);
    schema.id = QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha1).toHex().left(16));
    return schema;
}

QJsonObject Schema::toJson() const
{
    QJsonArray methodArray;
    for (const auto& m : methods) {
        QJsonObject json { { "type", methodTypeName(m.type) }, { "signature", QString::fromLatin1(m.signature) } };
        if (!m.returnType.isEmpty() && m.returnType != "void")
            json["returnType"] = QString::fromLatin1(m.returnType);
        if (!m.parameterTypes.isEmpty()) {
            json["parameterTypes"] = toArray(m.parameterTypes);
            json["parameterNames"] = toArray(m.parameterNames);
        }
        methodArray.append(json);
    }
    QJsonArray propertyArray;
    for (const auto& p : properties) {
        QJsonObject json { { "name", QString::fromLatin1(p.name) }, { "type", QString::fromLatin1(p.type) } };
        if (p.writable)
            json["writable"] = true;
        if (p.constant)
            json["constant"] = true;
        if (p.notify >= 0)
            json["notify"] = p.notify;
        propertyArray.append(json);
    }
    return { { "className", QString::fromLatin1(className) }, { "base", baseName(base) }, { "methods", methodArray },
        { "properties", propertyArray } };
}

Schema Schema::fromJson(const QJsonObject& json)
{
    Schema schema;
    schema.className = json["className"].toString().toLatin1();
    schema.base = baseFromName(json["base"].toString());
    for (const auto& value : json["methods"].toArray()) {
        const QJsonObject m = value.toObject();
        Method method;
        method.type = methodTypeFromName(m["type"].toString());
        method.signature = m["signature"].toString().toLatin1();
        method.returnType = m["returnType"].toString().toLatin1();
        method.parameterTypes = fromArray(m["parameterTypes"].toArray());
        method.parameterNames = fromArray(m["parameterNames"].toArray());
        schema.methods.append(method);
    }
    for (const auto& value : json["properties"].toArray()) {
        const QJsonObject p = value.toObject();
        Property property;
        property.name = p["name"].toString().toLatin1();
        property.type = p["type"].toString().toLatin1();
        property.writable = p["writable"].toBool();
        property.constant = p["constant"].toBool();
        property.notify = p["notify"].toInt(-1);
        schema.properties.append(property);
    }
    return schema;
}

QMetaObject* Schema::buildMetaObject() const
{
    QMetaObjectBuilder builder;
    builder.setClassName(className);
    // Every model base replays as a list model.
    builder.setSuperClass(isModel() ? &QAbstractListModel::staticMetaObject : &QObject::staticMetaObject);

    for (const auto& m : methods) {
        QMetaMethodBuilder method;
        switch (m.type) {
        case QMetaMethod::Signal:
            method = builder.addSignal(m.signature);
            break;
        case QMetaMethod::Slot:
            method = builder.addSlot(m.signature);
            break;
        default:
            method = builder.addMethod(m.signature);
            break;
        }
        if (!m.returnType.isEmpty())
            method.setReturnType(m.returnType);
        method.setParameterNames(m.parameterNames);
    }
    for (const auto& p : properties) {
        auto property = builder.addProperty(p.name, p.type, QMetaType::fromName(p.type), p.notify);
        property.setWritable(p.writable);
        property.setConstant(p.constant);
    }
    return builder.toMetaObject();
}

} // namespace StatusReplay
