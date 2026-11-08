#include <StatusReplay/recorder.h>

#include <StatusReplay/fingerprint.h>

#include "format.h"
#include "schema.h"
#include "valuecodec.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSysInfo>

#include <unordered_map>

#include <private/qqmlcontextdata_p.h>

namespace StatusReplay {

namespace {

const QString Redacted = QStringLiteral("$redacted");

bool isSensitive(const QByteArray& name)
{
    static const QRegularExpression contains(
        QStringLiteral("password|passphrase|mnemonic|seedphrase|privatekey|secret|credential|connectionstring"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression exact(
        QStringLiteral("^(seed|pairingcode)$|pin$|puk$"), QRegularExpression::CaseInsensitiveOption);
    const QString n = QString::fromLatin1(name);
    return contains.match(n).hasMatch() || exact.match(n).hasMatch();
}

// Secrets are text: a sensitive name on a flag or a count (mnemonicBackedUp) stays readable.
QJsonValue redactNamed(const QByteArray& name, const QJsonValue& encoded)
{
    return isSensitive(name) && encoded.isString() ? QJsonValue(Redacted) : encoded;
}

QJsonValue redactJson(const QJsonValue& value);

// Nim passes structured data as JSON strings (e.g. the login payload); redact inside them too.
QJsonValue redactString(const QString& text)
{
    const QStringView trimmed = QStringView(text).trimmed();
    if (trimmed.isEmpty() || (trimmed.front() != QLatin1Char('{') && trimmed.front() != QLatin1Char('[')))
        return text;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return text;
    const QJsonValue original = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    const QJsonValue redacted = redactJson(original);
    // Untouched payloads keep their exact text so replayed slot arguments still match.
    if (redacted == original)
        return text;
    const QJsonDocument out = redacted.isObject() ? QJsonDocument(redacted.toObject()) : QJsonDocument(redacted.toArray());
    return QString::fromUtf8(out.toJson(QJsonDocument::Compact));
}

QJsonValue redactJson(const QJsonValue& value)
{
    if (value.isString())
        return redactString(value.toString());
    if (value.isArray()) {
        QJsonArray result;
        for (const auto& item : value.toArray())
            result.append(redactJson(item));
        return result;
    }
    if (!value.isObject())
        return value;
    QJsonObject result;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
        result.insert(it.key(),
            it.key().startsWith(QLatin1Char('$')) ? redactJson(it.value())
                                                  : redactNamed(it.key().toLatin1(), redactJson(it.value())));
    return result;
}

bool writeJson(const QString& path, const QJsonObject& json)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return true;
}

QString fileSafe(QString name)
{
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9_.-]"));
    return name.replace(unsafe, QStringLiteral("_"));
}

} // namespace

class RecorderPrivate : public QObject
{
public:
    explicit RecorderPrivate(const QString& directory);

    // Signal tap: every followed signal is connected to the first method index past QObject's.
    int qt_metacall(QMetaObject::Call call, int id, void** argv) override;

    int idFor(QObject* object);
    const Schema& schemaFor(const QMetaObject* metaObject);
    QJsonObject state(QObject* object);
    QJsonValue encodeFromQml(const QVariant& value, QMetaType declared);
    void modelDataRead(QObject* model, int role);
    QJsonValue readValue(QObject* object, const QByteArray& name);
    void propertyRead(QObject* object, const QMetaProperty& property, const void* storage);
    // Only roles QML has read (modelDataObserved) are read back, like properties.
    QJsonArray rows(QAbstractItemModel* model, int first, int last, const QList<int>& roles = {});
    QJsonValue encode(const QVariant& value, QMetaType declared);
    void writeEvent(QJsonObject event);
    void follow(QObject* object, const Schema& schema);
    void onSignal(QObject* sender, int signalIndex, void** argv);
    void enumerateRootContext();
    void writeSnapshot(const QString& name);
    void writeManifest();

    QDir dir;
    QString error;
    QFile timeline;
    QElapsedTimer clock;
    qint64 seq = 0;
    int nextId = 1;
    bool loaded = false;

    QHash<QObject*, int> ids;
    // Only properties QML has read are read back by the recorder: Nim getters QML never
    // calls may not be safe to call (e.g. before their module finished loading).
    QHash<QObject*, QHash<QByteArray, QJsonValue>> readValues;
    QHash<QObject*, QSet<int>> readRoles;
    int selfReads = 0;
    QHash<int, QPointer<QObject>> objects;
    QList<int> order;
    // Stable addresses: callers hold a schema while reading values that register new types.
    std::unordered_map<const QMetaObject*, std::unique_ptr<Schema>> schemas;
    QSet<QString> writtenSchemas;
    QMap<QString, QJsonValue> contextProperties;
    QJsonArray moments;
    QJsonObject fingerprint;
    QStringList gaps;
    QPointer<QQmlEngine> engine;
    QDateTime startedAt;
};

RecorderPrivate::RecorderPrivate(const QString& directory)
    : dir(directory)
{
    if (!dir.mkpath(QStringLiteral(".")) || !dir.mkpath(Format::SchemasDir) || !dir.mkpath(Format::SnapshotsDir)) {
        error = QStringLiteral("cannot create %1").arg(directory);
        return;
    }
    timeline.setFileName(dir.filePath(Format::Timeline));
    if (!timeline.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = timeline.errorString();
        return;
    }
    clock.start();
    startedAt = QDateTime::currentDateTimeUtc();
}

int RecorderPrivate::qt_metacall(QMetaObject::Call call, int id, void** argv)
{
    id = QObject::qt_metacall(call, id, argv);
    if (id < 0 || call != QMetaObject::InvokeMetaMethod)
        return id;
    if (id == 0)
        onSignal(sender(), senderSignalIndex(), argv);
    return -1;
}

const Schema& RecorderPrivate::schemaFor(const QMetaObject* metaObject)
{
    auto it = schemas.find(metaObject);
    if (it == schemas.end()) {
        it = schemas.emplace(metaObject, std::make_unique<Schema>(Schema::fromMetaObject(metaObject))).first;
        const Schema& schema = *it->second;
        if (!writtenSchemas.contains(schema.id)) {
            writtenSchemas.insert(schema.id);
            writeJson(dir.filePath(Format::SchemasDir + QLatin1Char('/') + schema.id + QStringLiteral(".json")),
                schema.toJson());
        }
    }
    return *it->second;
}

// Values Nim hands out: objects in them are followed from now on.
QJsonValue RecorderPrivate::encode(const QVariant& value, QMetaType declared)
{
    return redactJson(ValueCodec::encode(value, declared, [this](QObject* o) { return idFor(o); }));
}

// Values QML passes in (slot arguments, property writes): objects Nim never handed out
// (QML items) are referenced by class only, not followed.
QJsonValue RecorderPrivate::encodeFromQml(const QVariant& value, QMetaType declared)
{
    return redactJson(ValueCodec::encode(value, declared, [this](QObject* o) { return ids.value(o, -1); }));
}

int RecorderPrivate::idFor(QObject* object)
{
    if (const auto it = ids.constFind(object); it != ids.cend())
        return *it;

    const int id = nextId++;
    ids.insert(object, id);
    objects.insert(id, object);
    order.append(id);

    const Schema& schema = schemaFor(object->metaObject());
    writeEvent({ { "k", "obj" }, { "o", id }, { "s", schema.id } });
    follow(object, schema);

    QJsonObject init = state(object);
    init["k"] = QStringLiteral("init");
    init["o"] = id;
    writeEvent(init);
    return id;
}

QJsonValue RecorderPrivate::readValue(QObject* object, const QByteArray& name)
{
    const QMetaObject* mo = object->metaObject();
    const QMetaProperty property = mo->property(mo->indexOfProperty(name));
    ++selfReads;
    const QVariant value = property.read(object);
    --selfReads;
    const QJsonValue encoded = redactNamed(name, encode(value, property.metaType()));
    readValues[object].insert(name, encoded);
    return encoded;
}

void RecorderPrivate::propertyRead(QObject* object, const QMetaProperty& property, const void* storage)
{
    if (selfReads > 0)
        return;
    const QByteArray name = property.name();
    const QJsonValue encoded =
        redactNamed(name, encode(ValueCodec::read(storage, property.metaType()), property.metaType()));
    auto& values = readValues[object];
    const auto known = values.constFind(name);
    if (known != values.cend() && *known == encoded)
        return;
    values.insert(name, encoded);
    writeEvent({ { "k", "val" }, { "o", ids.value(object) }, { "p", QString::fromLatin1(name) }, { "v", encoded } });
}

QJsonObject RecorderPrivate::state(QObject* object)
{
    QJsonObject values;
    const QList<QByteArray> names = readValues.value(object).keys();
    for (const auto& name : names)
        values.insert(QString::fromLatin1(name), readValue(object, name));
    QJsonObject result { { "v", values } };

    if (auto model = qobject_cast<QAbstractItemModel*>(object)) {
        QJsonObject roles;
        const auto roleNames = model->roleNames();
        for (auto it = roleNames.cbegin(); it != roleNames.cend(); ++it)
            roles.insert(QString::number(it.key()), QString::fromLatin1(it.value()));
        result["roles"] = roles;
        result["rows"] = rows(model, 0, model->rowCount() - 1);
        if (model->canFetchMore({}))
            result["fetchMore"] = true;
    }
    return result;
}

QJsonArray RecorderPrivate::rows(QAbstractItemModel* model, int first, int last, const QList<int>& roles)
{
    const QSet<int> known = readRoles.value(model);
    QList<int> wanted;
    for (int role : roles.isEmpty() ? QList<int>(known.cbegin(), known.cend()) : roles) {
        if (known.contains(role))
            wanted.append(role);
    }
    std::sort(wanted.begin(), wanted.end());
    const auto roleNames = model->roleNames();

    QJsonArray result;
    ++selfReads;
    for (int row = first; row <= last; ++row) {
        const QModelIndex index = model->index(row, 0);
        QJsonObject values;
        for (int role : wanted)
            values.insert(QString::number(role),
                redactNamed(roleNames.value(role), encode(model->data(index, role), QMetaType::fromType<QVariant>())));
        result.append(values);
    }
    --selfReads;
    return result;
}

// The first read of a role makes it known: its whole column is written, later inserts and
// changes carry it.
void RecorderPrivate::modelDataRead(QObject* object, int role)
{
    auto model = qobject_cast<QAbstractItemModel*>(object);
    if (!model || readRoles[object].contains(role))
        return;
    readRoles[object].insert(role);
    QJsonArray values;
    const QJsonArray rowValues = rows(model, 0, model->rowCount() - 1, { role });
    for (const auto& row : rowValues)
        values.append(row.toObject().value(QString::number(role)));
    writeEvent({ { "k", "col" }, { "o", ids.value(object) }, { "role", role }, { "values", values } });
}

void RecorderPrivate::writeEvent(QJsonObject event)
{
    event["q"] = seq++;
    event["t"] = clock.elapsed();
    timeline.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    timeline.write("\n");
    if (seq % 256 == 0)
        timeline.flush();
}

void RecorderPrivate::follow(QObject* object, const Schema& schema)
{
    const QMetaObject* mo = object->metaObject();
    const int tap = QObject::staticMetaObject.methodCount();
    const int first = Schema::baseMetaObject(schema.base)->methodCount();
    for (int i = first; i < mo->methodCount(); ++i) {
        if (mo->method(i).methodType() == QMetaMethod::Signal)
            QMetaObject::connect(object, i, this, tap, Qt::DirectConnection);
    }

    QObject::connect(object, &QObject::destroyed, this, [this, object] {
        const int id = ids.take(object);
        objects.remove(id);
        readValues.remove(object);
        readRoles.remove(object);
        writeEvent({ { "k", "del" }, { "o", id } });
    });

    auto model = qobject_cast<QAbstractItemModel*>(object);
    if (!model)
        return;

    auto idOf = [this, object] { return ids.value(object); };
    connect(model, &QAbstractItemModel::rowsInserted, this, [=](const QModelIndex& parent, int f, int l) {
        if (!parent.isValid())
            writeEvent({ { "k", "ins" }, { "o", idOf() }, { "f", f }, { "rows", rows(model, f, l) } });
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [=](const QModelIndex& parent, int f, int l) {
        if (!parent.isValid())
            writeEvent({ { "k", "rem" }, { "o", idOf() }, { "f", f }, { "l", l } });
    });
    connect(model, &QAbstractItemModel::rowsMoved, this,
        [=](const QModelIndex& parent, int f, int l, const QModelIndex& destination, int to) {
            if (!parent.isValid() && !destination.isValid())
                writeEvent({ { "k", "mov" }, { "o", idOf() }, { "f", f }, { "l", l }, { "to", to } });
        });
    connect(model, &QAbstractItemModel::dataChanged, this,
        [=](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) {
            if (topLeft.parent().isValid())
                return;
            QJsonArray roleArray;
            for (int role : roles)
                roleArray.append(role);
            writeEvent({ { "k", "data" }, { "o", idOf() }, { "f", topLeft.row() }, { "roles", roleArray },
                { "rows", rows(model, topLeft.row(), bottomRight.row(), roles) } });
        });
    connect(model, &QAbstractItemModel::modelReset, this, [=] {
        QJsonObject event = state(model);
        event.remove("v");
        event["k"] = QStringLiteral("reset");
        event["o"] = idOf();
        writeEvent(event);
    });
    connect(model, &QAbstractItemModel::layoutChanged, this, [=] {
        writeEvent({ { "k", "layout" }, { "o", idOf() }, { "rows", rows(model, 0, model->rowCount() - 1) } });
    });
}

void RecorderPrivate::onSignal(QObject* sender, int signalIndex, void** argv)
{
    if (!sender || !ids.contains(sender))
        return;
    const QMetaObject* mo = sender->metaObject();
    const QMetaMethod signal = mo->method(signalIndex);
    const Schema& schema = schemaFor(mo);

    const QByteArrayList names = signal.parameterNames();
    QJsonArray args;
    for (int i = 0; i < signal.parameterCount(); ++i) {
        const QMetaType type = signal.parameterMetaType(i);
        args.append(redactNamed(names.value(i), encode(ValueCodec::read(argv[i + 1], type), type)));
    }

    QJsonObject changed;
    const auto& known = readValues.value(sender);
    for (const auto& p : schema.properties) {
        if (p.notify < 0 || schema.methods[p.notify].signature != signal.methodSignature() || !known.contains(p.name))
            continue;
        changed.insert(QString::fromLatin1(p.name), readValue(sender, p.name));
    }

    QJsonObject event { { "k", "sig" }, { "o", ids.value(sender) },
        { "m", QString::fromLatin1(signal.methodSignature()) } };
    if (!args.isEmpty())
        event["a"] = args;
    if (!changed.isEmpty())
        event["p"] = changed;
    writeEvent(event);
}

void RecorderPrivate::enumerateRootContext()
{
    if (!engine)
        return;
    QQmlContext* root = engine->rootContext();
    const auto data = QQmlContextData::get(root);
    int misses = 0;
    for (int i = 0; misses < 16 && i < 10000; ++i) {
        const QString name = data->propertyName(i);
        if (name.isEmpty()) {
            ++misses;
            continue;
        }
        misses = 0;
        if (contextProperties.contains(name))
            continue;
        gaps.append(QStringLiteral("context property '%1' set outside the hook").arg(name));
        const QVariant value = root->contextProperty(name);
        contextProperties.insert(name, encode(value, QMetaType::fromType<QVariant>()));
        writeEvent({ { "k", "ctx" }, { "n", name }, { "v", contextProperties.value(name) } });
    }
}

void RecorderPrivate::writeSnapshot(const QString& requested)
{
    enumerateRootContext();

    QString name = requested;
    for (int n = 2; dir.exists(Format::SnapshotsDir + QLatin1Char('/') + fileSafe(name) + QStringLiteral(".json")); ++n)
        name = QStringLiteral("%1-%2").arg(requested).arg(n);

    // Reading values can register objects; they are part of the snapshot too.
    QJsonArray objectArray;
    for (int i = 0; i < order.size(); ++i) {
        const int id = order[i];
        QObject* object = objects.value(id);
        if (!object)
            continue;
        const Schema& schema = schemaFor(object->metaObject());
        QJsonObject entry = state(object);
        entry["o"] = id;
        entry["s"] = schema.id;
        objectArray.append(entry);
    }
    QJsonObject ctx;
    for (auto it = contextProperties.cbegin(); it != contextProperties.cend(); ++it)
        ctx.insert(it.key(), it.value());

    const qint64 momentSeq = seq;
    const QString file = Format::SnapshotsDir + QLatin1Char('/') + fileSafe(name) + QStringLiteral(".json");
    writeJson(dir.filePath(file),
        { { "name", name }, { "q", momentSeq }, { "loaded", loaded }, { "ctx", ctx }, { "objects", objectArray } });
    writeEvent({ { "k", "moment" }, { "n", name } });
    moments.append(QJsonObject { { "name", name }, { "q", momentSeq }, { "t", clock.elapsed() }, { "snapshot", file } });
    timeline.flush();
    writeManifest();
}

void RecorderPrivate::writeManifest()
{
    QJsonObject platform { { "os", QSysInfo::productType() }, { "osVersion", QSysInfo::productVersion() },
        { "kernel", QSysInfo::kernelVersion() }, { "abi", QSysInfo::buildAbi() } };
    QJsonObject app { { "name", QCoreApplication::applicationName() },
        { "version", QCoreApplication::applicationVersion() },
        { "commit", qEnvironmentVariable("STATUS_RECORD_APP_COMMIT") } };
    writeJson(dir.filePath(Format::Manifest),
        { { "formatVersion", QStringLiteral("%1.%2").arg(Format::Major).arg(Format::Minor) }, { "qt", qVersion() },
            { "platform", platform }, { "app", app }, { "startedAt", startedAt.toString(Qt::ISODate) },
            { "events", seq }, { "moments", moments }, { "fingerprint", fingerprint },
            { "importPaths", QJsonArray::fromStringList(engine ? extraImportPaths(engine) : QStringList()) },
            { "gaps", QJsonArray::fromStringList(gaps) } });
}

Recorder::Recorder(const QString& directory)
    : d(std::make_unique<RecorderPrivate>(directory))
{
}

Recorder::~Recorder()
{
    if (isValid())
        flush();
}

bool Recorder::isValid() const
{
    return d->error.isEmpty();
}

QString Recorder::errorString() const
{
    return d->error;
}

QString Recorder::directory() const
{
    return d->dir.absolutePath();
}

void Recorder::setEngine(QQmlEngine* engine)
{
    d->engine = engine;
}

void Recorder::setContextProperty(const QString& name, const QVariant& value)
{
    const QJsonValue encoded = d->encode(value, QMetaType::fromType<QVariant>());
    d->contextProperties.insert(name, encoded);
    d->writeEvent({ { "k", "ctx" }, { "n", name }, { "v", encoded } });
}

void Recorder::metacallObserved(QObject* object, QMetaObject::Call call, int index, void** argv)
{
    if (!object
        || (call != QMetaObject::InvokeMetaMethod && call != QMetaObject::WriteProperty
            && call != QMetaObject::ReadProperty))
        return;
    if (call == QMetaObject::ReadProperty && d->selfReads > 0)
        return;
    // Only objects handed to QML (and still alive: `destroyed` unregisters them) are followed.
    // The hook runs after the call, which may have deleted the object it was made on.
    if (!d->ids.contains(object))
        return;
    const QMetaObject* mo = object->metaObject();
    const Schema& schema = d->schemaFor(mo);
    const QMetaObject* base = Schema::baseMetaObject(schema.base);
    const int id = d->idFor(object);

    if (call == QMetaObject::ReadProperty) {
        if (index >= base->propertyCount())
            d->propertyRead(object, mo->property(index), argv[0]);
        return;
    }

    if (call == QMetaObject::WriteProperty) {
        if (index < base->propertyCount())
            return;
        const QMetaProperty property = mo->property(index);
        const QJsonValue value = redactNamed(
            property.name(), d->encodeFromQml(ValueCodec::read(argv[0], property.metaType()), property.metaType()));
        d->writeEvent({ { "k", "write" }, { "o", id }, { "p", QString::fromLatin1(property.name()) }, { "v", value } });
        return;
    }

    if (index < base->methodCount())
        return;
    const QMetaMethod method = mo->method(index);
    if (method.methodType() == QMetaMethod::Signal)
        return;

    const QByteArrayList names = method.parameterNames();
    QJsonArray args;
    for (int i = 0; i < method.parameterCount(); ++i) {
        const QMetaType type = method.parameterMetaType(i);
        args.append(redactNamed(names.value(i), d->encodeFromQml(ValueCodec::read(argv[i + 1], type), type)));
    }
    QJsonObject event { { "k", "call" }, { "o", id }, { "m", QString::fromLatin1(method.methodSignature()) } };
    if (!args.isEmpty())
        event["a"] = args;
    const QMetaType returnType = method.returnMetaType();
    if (argv[0] && returnType.isValid() && returnType.id() != QMetaType::Void)
        event["r"] = redactNamed(method.name(), d->encode(ValueCodec::read(argv[0], returnType), returnType));
    d->writeEvent(event);
}

void Recorder::modelDataObserved(QObject* model, int, int role, const QVariant&)
{
    if (d->selfReads > 0 || !model || !d->ids.contains(model))
        return;
    d->modelDataRead(model, role);
}

void Recorder::markLoaded()
{
    d->enumerateRootContext();
    d->loaded = true;
    d->writeEvent({ { "k", "load" } });
}

void Recorder::recordUiAction(const QJsonObject& step)
{
    d->writeEvent({ { "k", "ui" }, { "step", step } });
}

void Recorder::markMoment(const QString& name)
{
    d->writeSnapshot(name);
}

void Recorder::setFingerprint(const QJsonObject& fingerprint)
{
    d->fingerprint = fingerprint;
    d->writeManifest();
}

void Recorder::flush()
{
    d->timeline.flush();
    d->writeManifest();
}

} // namespace StatusReplay
