#include <StatusReplay/replayer.h>

#include "format.h"
#include "replayobject.h"
#include "schema.h"
#include "valuecodec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

#include <map>

namespace StatusReplay {

namespace {

QJsonObject readJson(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(path);
        return {};
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return {};
    }
    return doc.object();
}

struct RecordedCall
{
    qint64 seq;
    QJsonArray arguments;
    QJsonValue result;
    bool hasResult;
};

} // namespace

class ReplayerPrivate : public ReplayHost
{
public:
    explicit ReplayerPrivate(Replayer* q)
        : q(q)
    {
    }
    ~ReplayerPrivate() override { clear(); }

    bool open(const QString& directory, QString* error);
    bool startAt(const QString& moment, QString* error);
    void apply(const QJsonObject& event);
    void clear();

    const Schema* schema(const QString& id);
    QObject* create(int id, const QString& schemaId);
    QObject* object(int id) const { return objects.value(id); }
    DynamicPart* part(QObject* object) const;
    void setState(QObject* object, const QJsonObject& state);
    QList<ReplayModel::Row> decodeRows(const QJsonArray& rows) const;
    QVariant decode(const QJsonValue& value, QMetaType declared) const;

    void invoke(QObject* object, const Schema::Method& method, void** argv) override;

    Replayer* q;
    QDir dir;
    QJsonObject manifest;
    QList<QByteArray> lines;
    qint64 position = 0;
    bool loaded = false;

    std::map<QString, std::unique_ptr<Schema>> schemas;
    std::map<QString, QMetaObject*> metaObjects;
    QHash<int, QPointer<QObject>> objects;
    QHash<QObject*, int> ids;
    QMap<QString, QVariant> contextProperties;
    QHash<QPair<int, QByteArray>, QList<RecordedCall>> recordedCalls;
    QList<Replayer::Call> calls;
};

bool ReplayerPrivate::open(const QString& directory, QString* error)
{
    dir.setPath(directory);
    manifest = readJson(dir.filePath(Format::Manifest), error);
    if (manifest.isEmpty())
        return false;
    const QString version = manifest["formatVersion"].toString();
    if (version.section(QLatin1Char('.'), 0, 0).toInt() != Format::Major) {
        *error = QStringLiteral("unsupported recording format %1 (reader supports %2.x)").arg(version).arg(Format::Major);
        return false;
    }

    QFile timeline(dir.filePath(Format::Timeline));
    if (!timeline.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(timeline.fileName());
        return false;
    }
    while (!timeline.atEnd()) {
        QByteArray line = timeline.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.contains("\"k\":\"call\"")) {
            const QJsonObject event = QJsonDocument::fromJson(line).object();
            recordedCalls[{ event["o"].toInt(), event["m"].toString().toLatin1() }].append(
                { event["q"].toInteger(), event["a"].toArray(), event["r"], event.contains("r") });
        }
        lines.append(std::move(line));
    }
    return true;
}

const Schema* ReplayerPrivate::schema(const QString& id)
{
    auto it = schemas.find(id);
    if (it != schemas.end())
        return it->second.get();
    QString error;
    const QJsonObject json = readJson(dir.filePath(Format::SchemasDir + QLatin1Char('/') + id + QStringLiteral(".json")), &error);
    if (json.isEmpty()) {
        qWarning() << "StatusReplay:" << error;
        return nullptr;
    }
    auto schema = std::make_unique<Schema>(Schema::fromJson(json));
    schema->id = id;
    metaObjects[id] = schema->buildMetaObject();
    return schemas.emplace(id, std::move(schema)).first->second.get();
}

QObject* ReplayerPrivate::create(int id, const QString& schemaId)
{
    const Schema* s = schema(schemaId);
    if (!s)
        return nullptr;
    QObject* object = nullptr;
    if (s->isModel())
        object = new ReplayModel(*s, metaObjects[schemaId], this);
    else
        object = new ReplayObject(*s, metaObjects[schemaId], this);
    objects.insert(id, object);
    ids.insert(object, id);
    return object;
}

DynamicPart* ReplayerPrivate::part(QObject* object) const
{
    if (auto model = dynamic_cast<ReplayModel*>(object))
        return &model->part();
    if (auto plain = dynamic_cast<ReplayObject*>(object))
        return &plain->part();
    return nullptr;
}

QVariant ReplayerPrivate::decode(const QJsonValue& value, QMetaType declared) const
{
    return ValueCodec::decode(value, declared, [this](int id) { return object(id); });
}

QList<ReplayModel::Row> ReplayerPrivate::decodeRows(const QJsonArray& rows) const
{
    QList<ReplayModel::Row> result;
    result.reserve(rows.size());
    for (const auto& value : rows) {
        const QJsonObject row = value.toObject();
        ReplayModel::Row decoded;
        for (auto it = row.begin(); it != row.end(); ++it)
            decoded.insert(it.key().toInt(), decode(it.value(), QMetaType::fromType<QVariant>()));
        result.append(decoded);
    }
    return result;
}

void ReplayerPrivate::setState(QObject* object, const QJsonObject& state)
{
    DynamicPart* p = part(object);
    if (!p)
        return;
    const QJsonObject values = state["v"].toObject();
    for (auto it = values.begin(); it != values.end(); ++it) {
        const int index = p->propertyIndex(it.key().toLatin1());
        if (index >= 0)
            p->setValue(index, decode(it.value(), QMetaType::fromName(p->schema().properties[index].type)));
    }
    if (auto model = dynamic_cast<ReplayModel*>(object)) {
        QHash<int, QByteArray> roles;
        const QJsonObject roleJson = state["roles"].toObject();
        for (auto it = roleJson.begin(); it != roleJson.end(); ++it)
            roles.insert(it.key().toInt(), it.value().toString().toLatin1());
        model->setRoles(roles);
        model->setFetchMore(state["fetchMore"].toBool());
        model->setRows(decodeRows(state["rows"].toArray()));
    }
}

bool ReplayerPrivate::startAt(const QString& moment, QString* error)
{
    clear();
    if (moment.isEmpty()) {
        position = 0;
        return true;
    }
    QString snapshotFile;
    for (const auto& m : manifest["moments"].toArray()) {
        if (m.toObject()["name"].toString() == moment)
            snapshotFile = m.toObject()["snapshot"].toString();
    }
    if (snapshotFile.isEmpty()) {
        *error = QStringLiteral("no moment '%1' in the recording").arg(moment);
        return false;
    }
    const QJsonObject snapshot = readJson(dir.filePath(snapshotFile), error);
    if (snapshot.isEmpty())
        return false;

    const QJsonArray entries = snapshot["objects"].toArray();
    for (const auto& entry : entries) {
        if (!create(entry.toObject()["o"].toInt(), entry.toObject()["s"].toString())) {
            *error = QStringLiteral("cannot build object %1").arg(entry.toObject()["o"].toInt());
            return false;
        }
    }
    for (const auto& entry : entries)
        setState(object(entry.toObject()["o"].toInt()), entry.toObject());

    const QJsonObject ctx = snapshot["ctx"].toObject();
    for (auto it = ctx.begin(); it != ctx.end(); ++it)
        contextProperties.insert(it.key(), decode(it.value(), QMetaType::fromType<QVariant>()));

    loaded = snapshot["loaded"].toBool();
    // The snapshot is taken just before its moment marker, which is already applied.
    position = snapshot["q"].toInteger() + 1;
    return true;
}

void ReplayerPrivate::apply(const QJsonObject& event)
{
    const QString kind = event["k"].toString();
    QObject* target = object(event["o"].toInt());

    if (kind == QLatin1String("obj")) {
        create(event["o"].toInt(), event["s"].toString());
    } else if (kind == QLatin1String("init")) {
        if (target)
            setState(target, event);
    } else if (kind == QLatin1String("del")) {
        if (target) {
            ids.remove(target);
            objects.remove(event["o"].toInt());
            target->deleteLater();
        }
    } else if (kind == QLatin1String("ctx")) {
        const QString name = event["n"].toString();
        const QVariant value = decode(event["v"], QMetaType::fromType<QVariant>());
        contextProperties.insert(name, value);
        emit q->contextPropertyChanged(name, value);
    } else if (kind == QLatin1String("sig")) {
        DynamicPart* p = target ? part(target) : nullptr;
        if (!p)
            return;
        const QJsonObject changed = event["p"].toObject();
        for (auto it = changed.begin(); it != changed.end(); ++it) {
            const int index = p->propertyIndex(it.key().toLatin1());
            if (index >= 0)
                p->setValue(index, decode(it.value(), QMetaType::fromName(p->schema().properties[index].type)));
        }
        const int method = p->methodIndex(event["m"].toString().toLatin1());
        if (method < 0)
            return;
        const auto& signal = p->schema().methods[method];
        const QJsonArray args = event["a"].toArray();
        QList<QVariant> values;
        QList<QMetaType> types;
        for (int i = 0; i < signal.parameterTypes.size(); ++i) {
            types.append(QMetaType::fromName(signal.parameterTypes[i]));
            values.append(decode(args.at(i), types.last()));
        }
        QList<void*> argv { nullptr };
        for (int i = 0; i < values.size(); ++i)
            argv.append(types[i] == QMetaType::fromType<QVariant>() ? static_cast<void*>(&values[i]) : values[i].data());
        p->emitSignal(target, method, argv.data());
    } else if (kind == QLatin1String("ins")) {
        if (auto model = dynamic_cast<ReplayModel*>(target))
            model->insertRowsAt(event["f"].toInt(), decodeRows(event["rows"].toArray()));
    } else if (kind == QLatin1String("rem")) {
        if (auto model = dynamic_cast<ReplayModel*>(target))
            model->removeRowsAt(event["f"].toInt(), event["l"].toInt());
    } else if (kind == QLatin1String("mov")) {
        if (auto model = dynamic_cast<ReplayModel*>(target))
            model->moveRowsTo(event["f"].toInt(), event["l"].toInt(), event["to"].toInt());
    } else if (kind == QLatin1String("data")) {
        if (auto model = dynamic_cast<ReplayModel*>(target)) {
            QList<int> roles;
            for (const auto& role : event["roles"].toArray())
                roles.append(role.toInt());
            model->updateRows(event["f"].toInt(), decodeRows(event["rows"].toArray()), roles);
        }
    } else if (kind == QLatin1String("reset")) {
        if (auto model = dynamic_cast<ReplayModel*>(target)) {
            QHash<int, QByteArray> roles;
            const QJsonObject roleJson = event["roles"].toObject();
            for (auto it = roleJson.begin(); it != roleJson.end(); ++it)
                roles.insert(it.key().toInt(), it.value().toString().toLatin1());
            model->setRoles(roles);
            model->setFetchMore(event["fetchMore"].toBool());
            model->resetRows(decodeRows(event["rows"].toArray()));
        }
    } else if (kind == QLatin1String("layout")) {
        if (auto model = dynamic_cast<ReplayModel*>(target))
            model->relayoutRows(decodeRows(event["rows"].toArray()));
    } else if (kind == QLatin1String("load")) {
        loaded = true;
        emit q->loaded();
    } else if (kind == QLatin1String("moment")) {
        emit q->momentReached(event["n"].toString());
    }
}

void ReplayerPrivate::invoke(QObject* object, const Schema::Method& method, void** argv)
{
    const int id = ids.value(object);
    QJsonArray args;
    QVariantList values;
    for (int i = 0; i < method.parameterTypes.size(); ++i) {
        const QMetaType type = QMetaType::fromName(method.parameterTypes[i]);
        const QVariant value = ValueCodec::read(argv[i + 1], type);
        values.append(value);
        args.append(ValueCodec::encode(value, type, [this](QObject* o) { return ids.value(o, 0); }));
    }

    const RecordedCall* exact = nullptr;
    const RecordedCall* latest = nullptr;
    const QList<RecordedCall> recordedList = recordedCalls.value({ id, method.signature });
    for (const auto& recorded : recordedList) {
        if (recorded.seq >= position && latest)
            break;
        if (recorded.arguments == args)
            exact = &recorded;
        latest = &recorded;
    }
    const RecordedCall* chosen = exact ? exact : latest;

    calls.append({ QString::fromLatin1(object->metaObject()->className()) + QLatin1Char('#') + QString::number(id),
        QString::fromLatin1(method.signature), values, exact != nullptr });

    const QMetaType returnType = QMetaType::fromName(method.returnType);
    if (!argv[0] || !returnType.isValid() || returnType.id() == QMetaType::Void)
        return;
    ValueCodec::write(argv[0], returnType,
        chosen && chosen->hasResult ? decode(chosen->result, returnType) : decode(QJsonValue::Null, returnType));
}

void ReplayerPrivate::clear()
{
    for (const auto& object : std::as_const(objects))
        delete object.data();
    objects.clear();
    ids.clear();
    contextProperties.clear();
    loaded = false;
}

Replayer::Replayer()
    : d(std::make_unique<ReplayerPrivate>(this))
{
}

Replayer::~Replayer()
{
    d->clear();
    for (auto& [id, metaObject] : d->metaObjects)
        free(metaObject);
}

std::unique_ptr<Replayer> Replayer::open(const QString& directory, QString* error)
{
    QString localError;
    std::unique_ptr<Replayer> replayer(new Replayer);
    if (!replayer->d->open(directory, &localError)) {
        if (error)
            *error = localError;
        return {};
    }
    return replayer;
}

QStringList Replayer::moments() const
{
    QStringList result;
    for (const auto& m : d->manifest["moments"].toArray())
        result.append(m.toObject()["name"].toString());
    return result;
}

QJsonObject Replayer::fingerprint() const
{
    return d->manifest["fingerprint"].toObject();
}

bool Replayer::startAt(const QString& moment, QString* error)
{
    QString localError;
    const bool ok = d->startAt(moment, &localError);
    if (!ok && error)
        *error = localError;
    return ok;
}

QStringList Replayer::contextPropertyNames() const
{
    return d->contextProperties.keys();
}

QVariant Replayer::contextProperty(const QString& name) const
{
    return d->contextProperties.value(name);
}

bool Replayer::atEnd() const
{
    return d->position >= d->lines.size();
}

bool Replayer::loadReached() const
{
    return d->loaded;
}

qint64 Replayer::nextEventTime() const
{
    if (atEnd())
        return -1;
    return QJsonDocument::fromJson(d->lines[d->position]).object()["t"].toInteger();
}

bool Replayer::step()
{
    if (atEnd())
        return false;
    d->apply(QJsonDocument::fromJson(d->lines[d->position++]).object());
    return true;
}

bool Replayer::runUntilMoment(const QString& moment)
{
    bool reached = false;
    auto connection = connect(this, &Replayer::momentReached, this, [&](const QString& name) {
        if (name == moment)
            reached = true;
    });
    while (!reached && step()) { }
    disconnect(connection);
    return reached;
}

bool Replayer::runUntilLoaded()
{
    while (!d->loaded && step()) { }
    return d->loaded;
}

QList<Replayer::Call> Replayer::calls() const
{
    return d->calls;
}

} // namespace StatusReplay
