#include "replayobject.h"

#include "valuecodec.h"

namespace StatusReplay {

DynamicPart::DynamicPart(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host)
    : m_schema(schema)
    , m_metaObject(metaObject)
    , m_host(host)
{
    m_values.reserve(schema.properties.size());
    for (int i = 0; i < schema.properties.size(); ++i) {
        const auto& p = schema.properties[i];
        m_propertyIndex.insert(p.name, i);
        m_values.append(ValueCodec::decode(QJsonValue::Null, QMetaType::fromName(p.type), {}));
    }
    for (int i = 0; i < schema.methods.size(); ++i)
        m_methodIndex.insert(schema.methods[i].signature, i);
}

int DynamicPart::propertyIndex(const QByteArray& name) const
{
    return m_propertyIndex.value(name, -1);
}

int DynamicPart::methodIndex(const QByteArray& signature) const
{
    return m_methodIndex.value(signature, -1);
}

void DynamicPart::setValue(int property, const QVariant& value)
{
    if (property >= 0 && property < m_values.size())
        m_values[property] = value;
}

void DynamicPart::emitSignal(QObject* self, int method, void** argv)
{
    void* noArgs[] = { nullptr };
    QMetaObject::activate(self, m_metaObject, method, argv ? argv : noArgs);
}

int DynamicPart::metacall(QObject* self, QMetaObject::Call call, int id, void** argv)
{
    const int methodCount = m_schema.methods.size();
    const int propertyCount = m_schema.properties.size();

    switch (call) {
    case QMetaObject::InvokeMetaMethod:
        if (id < methodCount) {
            const auto& method = m_schema.methods[id];
            if (method.type == QMetaMethod::Signal)
                emitSignal(self, id, argv);
            else
                m_host->invoke(self, method, argv);
        }
        return id - methodCount;
    case QMetaObject::RegisterMethodArgumentMetaType:
        if (id < methodCount)
            *static_cast<QMetaType*>(argv[0]) = QMetaType();
        return id - methodCount;
    case QMetaObject::ReadProperty:
        if (id < propertyCount)
            ValueCodec::write(argv[0], QMetaType::fromName(m_schema.properties[id].type), m_values[id]);
        return id - propertyCount;
    case QMetaObject::WriteProperty:
        if (id < propertyCount) {
            const auto& property = m_schema.properties[id];
            m_values[id] = ValueCodec::read(argv[0], QMetaType::fromName(property.type));
            if (property.notify >= 0 && m_schema.methods[property.notify].parameterTypes.isEmpty())
                emitSignal(self, property.notify, nullptr);
        }
        return id - propertyCount;
    case QMetaObject::ResetProperty:
    case QMetaObject::RegisterPropertyMetaType:
    case QMetaObject::BindableProperty:
        if (call == QMetaObject::RegisterPropertyMetaType && id < propertyCount)
            *static_cast<QMetaType*>(argv[0]) = QMetaType::fromName(m_schema.properties[id].type);
        return id - propertyCount;
    default:
        return id;
    }
}

ReplayObject::ReplayObject(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host)
    : m_part(schema, metaObject, host)
{
}

const QMetaObject* ReplayObject::metaObject() const
{
    return m_part.metaObject();
}

int ReplayObject::qt_metacall(QMetaObject::Call call, int id, void** argv)
{
    id = QObject::qt_metacall(call, id, argv);
    if (id < 0)
        return id;
    return m_part.metacall(this, call, id, argv);
}

ReplayModel::ReplayModel(const Schema& schema, const QMetaObject* metaObject, ReplayHost* host)
    : m_part(schema, metaObject, host)
{
}

const QMetaObject* ReplayModel::metaObject() const
{
    return m_part.metaObject();
}

int ReplayModel::qt_metacall(QMetaObject::Call call, int id, void** argv)
{
    id = QAbstractListModel::qt_metacall(call, id, argv);
    if (id < 0)
        return id;
    return m_part.metacall(this, call, id, argv);
}

int ReplayModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ReplayModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    return m_rows[index.row()].value(role);
}

QHash<int, QByteArray> ReplayModel::roleNames() const
{
    return m_roles;
}

bool ReplayModel::canFetchMore(const QModelIndex& parent) const
{
    return !parent.isValid() && m_canFetchMore;
}

void ReplayModel::insertRowsAt(int first, const QList<Row>& rows)
{
    if (rows.isEmpty())
        return;
    beginInsertRows({}, first, first + rows.size() - 1);
    for (int i = 0; i < rows.size(); ++i)
        m_rows.insert(first + i, rows[i]);
    endInsertRows();
}

void ReplayModel::removeRowsAt(int first, int last)
{
    beginRemoveRows({}, first, last);
    m_rows.remove(first, last - first + 1);
    endRemoveRows();
}

void ReplayModel::moveRowsTo(int first, int last, int destination)
{
    if (!beginMoveRows({}, first, last, {}, destination))
        return;
    const QList<Row> moved = m_rows.mid(first, last - first + 1);
    m_rows.remove(first, moved.size());
    const int at = destination > last ? destination - moved.size() : destination;
    for (int i = 0; i < moved.size(); ++i)
        m_rows.insert(at + i, moved[i]);
    endMoveRows();
}

void ReplayModel::updateRows(int first, const QList<Row>& rows, const QList<int>& roles)
{
    if (rows.isEmpty())
        return;
    for (int i = 0; i < rows.size(); ++i) {
        Row& row = m_rows[first + i];
        for (auto it = rows[i].cbegin(); it != rows[i].cend(); ++it)
            row.insert(it.key(), it.value());
    }
    emit dataChanged(index(first), index(first + rows.size() - 1), roles);
}

void ReplayModel::resetRows(const QList<Row>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void ReplayModel::relayoutRows(const QList<Row>& rows)
{
    emit layoutAboutToBeChanged();
    m_rows = rows;
    emit layoutChanged();
}

} // namespace StatusReplay
