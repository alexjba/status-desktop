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
            const QVariant written = ValueCodec::read(argv[0], QMetaType::fromName(property.type));
            const bool changed = written != m_values[id];
            m_values[id] = written;
            // The recorded notify that followed the write replays without re-emitting.
            if (changed && property.notify >= 0 && m_schema.methods[property.notify].parameterTypes.isEmpty()) {
                m_writtenByQml.insert(id);
                emitSignal(self, property.notify, nullptr);
            }
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
    const Row& row = m_rows[index.row()];
    if (!row.contains(role) && !m_requestedRoles.contains(role)) {
        m_requestedRoles.insert(role);
        m_part.host()->roleMissing(const_cast<ReplayModel*>(this), role);
        return m_rows[index.row()].value(role);
    }
    return row.value(role);
}

void ReplayModel::setColumn(int role, const QList<QVariant>& values, bool notify)
{
    // Applied ahead of its position (QML read earlier), the row count may differ: index-mapped.
    const int count = qMin(values.size(), m_rows.size());
    if (notify && values.size() != m_rows.size())
        qWarning() << "StatusReplay: column of" << values.size() << "values for" << m_rows.size() << "rows";
    for (int i = 0; i < count; ++i)
        m_rows[i].insert(role, values[i]);
    if (notify && count > 0 && m_requestedRoles.contains(role))
        emit dataChanged(index(0), index(count - 1), { role });
}

namespace {
bool diverged(const char* operation, bool ok)
{
    if (!ok)
        qWarning() << "StatusReplay: model diverged from the recording at" << operation << "- skipped";
    return !ok;
}
} // namespace

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
    if (rows.isEmpty() || diverged("insert", first >= 0 && first <= m_rows.size()))
        return;
    beginInsertRows({}, first, first + rows.size() - 1);
    for (int i = 0; i < rows.size(); ++i)
        m_rows.insert(first + i, rows[i]);
    endInsertRows();
}

void ReplayModel::removeRowsAt(int first, int last)
{
    if (diverged("remove", first >= 0 && first <= last && last < m_rows.size()))
        return;
    beginRemoveRows({}, first, last);
    m_rows.remove(first, last - first + 1);
    endRemoveRows();
}

void ReplayModel::moveRowsTo(int first, int last, int destination)
{
    if (diverged("move", first >= 0 && first <= last && last < m_rows.size() && destination >= 0
                && destination <= m_rows.size()))
        return;
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
    if (rows.isEmpty() || diverged("dataChanged", first >= 0 && first + rows.size() <= m_rows.size()))
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
