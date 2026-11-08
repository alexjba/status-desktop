#pragma once

#include "schema.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariant>

namespace StatusReplay {

// Answers what replay objects cannot answer from their own state.
class ReplayHost
{
public:
    virtual ~ReplayHost() = default;
    // A slot or invokable called from QML; `argv` follows the metacall convention.
    virtual void invoke(QObject* object, const Schema::Method& method, void** argv) = 0;
};

// Dynamic part shared by replayed objects and models: schema-built metaobject plus values.
class DynamicPart
{
public:
    DynamicPart(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host);

    const QMetaObject* metaObject() const { return m_metaObject; }
    const Schema& schema() const { return m_schema; }

    int propertyIndex(const QByteArray& name) const;
    QVariant value(int property) const { return m_values.value(property); }
    // Stores without notifying; notification is replayed from the recorded signal.
    void setValue(int property, const QVariant& value);

    // Handles the metacall for the dynamic part; `id` is relative to the base class.
    int metacall(QObject* self, QMetaObject::Call call, int id, void** argv);

    int methodIndex(const QByteArray& signature) const;
    void emitSignal(QObject* self, int method, void** argv);

private:
    const Schema& m_schema;
    const QMetaObject* m_metaObject;
    ReplayHost* m_host;
    QList<QVariant> m_values;
    QHash<QByteArray, int> m_propertyIndex;
    QHash<QByteArray, int> m_methodIndex;
};

class ReplayObject : public QObject
{
public:
    ReplayObject(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host);

    const QMetaObject* metaObject() const override;
    int qt_metacall(QMetaObject::Call call, int id, void** argv) override;

    DynamicPart& part() { return m_part; }

private:
    DynamicPart m_part;
};

class ReplayModel : public QAbstractListModel
{
public:
    using Row = QHash<int, QVariant>;

    ReplayModel(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host);

    const QMetaObject* metaObject() const override;
    int qt_metacall(QMetaObject::Call call, int id, void** argv) override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex& parent) const override;

    DynamicPart& part() { return m_part; }

    void setRoles(const QHash<int, QByteArray>& roles) { m_roles = roles; }
    void setFetchMore(bool canFetchMore) { m_canFetchMore = canFetchMore; }
    // Silent: used while building state from a snapshot.
    void setRows(const QList<Row>& rows) { m_rows = rows; }

    void insertRowsAt(int first, const QList<Row>& rows);
    void removeRowsAt(int first, int last);
    void moveRowsTo(int first, int last, int destination);
    void updateRows(int first, const QList<Row>& rows, const QList<int>& roles);
    void resetRows(const QList<Row>& rows);
    void relayoutRows(const QList<Row>& rows);

private:
    DynamicPart m_part;
    QHash<int, QByteArray> m_roles;
    QList<Row> m_rows;
    bool m_canFetchMore = false;
};

} // namespace StatusReplay
