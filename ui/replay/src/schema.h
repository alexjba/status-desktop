#pragma once

#include <QByteArrayList>
#include <QJsonObject>
#include <QList>
#include <QMetaMethod>

namespace StatusReplay {

// The part of an object's metaobject that sits above its Qt base class, flattened.
struct Schema
{
    enum class Base
    {
        Object,
        ListModel,
        TableModel,
        ItemModel,
    };

    struct Method
    {
        QMetaMethod::MethodType type = QMetaMethod::Method;
        QByteArray signature;
        QByteArray returnType;
        QByteArrayList parameterTypes;
        QByteArrayList parameterNames;
    };

    struct Property
    {
        QByteArray name;
        QByteArray type;
        bool writable = false;
        bool constant = false;
        int notify = -1; // index into methods
    };

    QString id;
    QByteArray className;
    Base base = Base::Object;
    // Signals come first so a method's index is also its local signal index.
    QList<Method> methods;
    QList<Property> properties;

    bool isModel() const { return base != Base::Object; }

    QJsonObject toJson() const;
    static Schema fromJson(const QJsonObject& json);
    static Schema fromMetaObject(const QMetaObject* metaObject);

    static const QMetaObject* baseMetaObject(Base base);
    // Builds the replay metaobject over the replay base class; owned by the caller (free()).
    QMetaObject* buildMetaObject() const;
};

} // namespace StatusReplay
