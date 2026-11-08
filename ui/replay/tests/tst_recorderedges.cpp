#include "fixtures.h"

#include <StatusReplay/recorder.h>
#include <StatusReplay/replayer.h>

#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <private/qmetaobjectbuilder_p.h>

using namespace StatusReplay;

namespace {

QObject* objectOf(const QVariant& value)
{
    return value.value<QObject*>();
}

template <typename R, typename... Args>
R call(QObject* object, const char* signature, Args... args)
{
    const int index = object->metaObject()->indexOfMethod(signature);
    if (index < 0)
        qFatal("no method %s", signature);
    R result {};
    void* argv[] = { &result, &args... };
    QMetaObject::metacall(object, QMetaObject::InvokeMetaMethod, index, argv);
    return result;
}

QByteArray recordingContent(const QString& directory)
{
    QByteArray content;
    for (const auto& name : QDir(directory).entryList(QDir::Files)) {
        QFile file(QDir(directory).filePath(name));
        if (file.open(QIODevice::ReadOnly))
            content += file.readAll();
    }
    for (const auto& name : QDir(directory + QStringLiteral("/snapshots")).entryList(QDir::Files)) {
        QFile file(directory + QStringLiteral("/snapshots/") + name);
        if (file.open(QIODevice::ReadOnly))
            content += file.readAll();
    }
    return content;
}

// An object of a class nobody has seen yet: a fresh metaobject per instance, like the many
// Nim types a big module hands out at once.
class NewTypeObject : public QObject
{
public:
    explicit NewTypeObject(int n)
    {
        QMetaObjectBuilder builder;
        builder.setClassName("NewType" + QByteArray::number(n));
        builder.setSuperClass(&QObject::staticMetaObject);
        builder.addProperty("value", "int");
        m_metaObject = builder.toMetaObject();
    }
    ~NewTypeObject() override { free(m_metaObject); }

    const QMetaObject* metaObject() const override { return m_metaObject; }
    int qt_metacall(QMetaObject::Call call, int id, void** argv) override
    {
        id = QObject::qt_metacall(call, id, argv);
        if (id < 0)
            return id;
        if (call == QMetaObject::ReadProperty) {
            if (id == 0)
                *static_cast<int*>(argv[0]) = 42;
            return id - 1;
        }
        return id;
    }

private:
    QMetaObject* m_metaObject = nullptr;
};

} // namespace

class RecorderEdgesTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Recorder> m_recorder;

    std::unique_ptr<Replayer> replayAt(const QString& moment)
    {
        m_recorder->flush();
        QString error;
        auto replayer = Replayer::open(m_dir.path(), &error);
        if (!replayer || !replayer->startAt(moment, &error)) {
            qWarning() << error;
            return {};
        }
        return replayer;
    }

private slots:
    void init()
    {
        m_recorder = std::make_unique<Recorder>(m_dir.path());
        g_recorder = m_recorder.get();
    }

    void cleanup()
    {
        g_recorder = nullptr;
        m_recorder.reset();
        m_dir.remove();
        m_dir = QTemporaryDir();
    }

    // Notify handling holds the sender's schema while reading values that register new
    // types; growing the schema table must not invalidate it (ASan catches the old crash).
    void manyNewTypesInsideNotifyHandling()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "current" });
        std::vector<std::unique_ptr<NewTypeObject>> objects;
        for (int i = 0; i < 200; ++i) {
            objects.push_back(std::make_unique<NewTypeObject>(i));
            wallet.setCurrent(objects.back().get());
        }
        m_recorder->markMoment(QStringLiteral("end"));

        auto replayer = replayAt(QStringLiteral("end"));
        QVERIFY(replayer);
        QObject* current = objectOf(objectOf(replayer->contextProperty("walletSection"))->property("current"));
        QVERIFY(current);
        QCOMPARE(QByteArray(current->metaObject()->className()), QByteArray("NewType199"));
    }

    void secretsInSignalsGettersAndRolesAreNotWritten()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        wallet.accountsModel()->insert(0, QStringLiteral("Main"), 1);
        readLikeQml(&wallet, { "connectionString", "accounts" });
        readRolesLikeQml(wallet.accountsModel(), { AccountsModel::NameRole, AccountsModel::SeedPhraseRole });
        call<QString>(&wallet, "getConnectionString()");
        call<QString>(&wallet, "validateConnectionString(QString)", QStringLiteral("cs2:typed-secret"));
        wallet.requestStoreCredential(QStringLiteral("0xkey"), QStringLiteral("hunter2"));
        wallet.authenticate(QStringLiteral("0xpub"), QStringLiteral("0xchat-private"));
        wallet.requestStayLoggedIn(QStringLiteral("0xaccount"), QStringLiteral("stored-credential"));
        wallet.accountsModel()->insert(1, QStringLiteral("Second"), 2);
        m_recorder->markMoment(QStringLiteral("end"));
        m_recorder->flush();

        const QByteArray content = recordingContent(m_dir.path());
        for (const char* secret :
            { "pairing-secret", "typed-secret", "hunter2", "0xchat-private", "stored-credential", "apple banana" })
            QVERIFY2(!content.contains(secret), secret);
        // Non-secret values next to them stay readable.
        for (const char* visible : { "0xkey", "0xpub", "0xaccount", "Second" })
            QVERIFY2(content.contains(visible), visible);
    }

    void rolesQmlNeverReadAreNotReadAndReadRolesReplay()
    {
        Hooked<WalletView> wallet;
        auto* model = wallet.accountsModel();
        model->insert(0, QStringLiteral("Main"), 10);
        model->insert(1, QStringLiteral("Savings"), 20);
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "accounts" });
        model->insert(2, QStringLiteral("Third"), 30);
        model->setBalance(0, 11);
        QCOMPARE(model->seedPhraseReads(), 0);
        // QML reads the name role only now, after the model changed.
        readRolesLikeQml(model, { AccountsModel::NameRole });
        model->insert(3, QStringLiteral("Fourth"), 40);
        m_recorder->markMoment(QStringLiteral("end"));
        QCOMPARE(model->seedPhraseReads(), 0);

        auto replayer = replayAt(QString());
        QVERIFY(replayer);
        QAbstractItemModel* replayed = nullptr;
        connect(replayer.get(), &Replayer::contextPropertyChanged, this, [&](const QString&, const QVariant& value) {
            replayed = qobject_cast<QAbstractItemModel*>(objectOf(objectOf(value)->property("accounts")));
        });
        while (!replayed && replayer->step()) { }
        QVERIFY(replayed);
        // Replayed QML reading earlier than the recorded one still gets the recorded names.
        QCOMPARE(replayed->rowCount(), 2);
        QCOMPARE(replayed->data(replayed->index(1, 0), AccountsModel::NameRole), QVariant(QStringLiteral("Savings")));
        QVERIFY(!replayed->data(replayed->index(0, 0), AccountsModel::BalanceRole).isValid());

        QVERIFY(replayer->runUntilMoment(QStringLiteral("end")));
        QCOMPARE(replayed->rowCount(), 4);
        QCOMPARE(replayed->data(replayed->index(2, 0), AccountsModel::NameRole), QVariant(QStringLiteral("Third")));
        QCOMPARE(replayed->data(replayed->index(3, 0), AccountsModel::NameRole), QVariant(QStringLiteral("Fourth")));
    }

    void replayedObjectsSurviveTheQmlGarbageCollector()
    {
        Hooked<WalletView> wallet;
        Hooked<Account> savings;
        wallet.accountsModel()->insert(0, QStringLiteral("Savings"), 20, &savings);
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        call<QVariant>(&wallet, "accountAt(int)", 0);
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("walletSection"), replayer->contextProperty("walletSection"));
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { property var account: walletSection.accountAt(0) }",
            QUrl(QStringLiteral("qrc:/gc.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QPointer<QObject> account = objectOf(root->property("account"));
        QVERIFY(account);

        root.reset();
        engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(account);
    }

    void unsignedValuesAboveInt64Survive()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "bigValue" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QCOMPARE(objectOf(replayer->contextProperty("walletSection"))->property("bigValue"),
            QVariant(std::numeric_limits<quint64>::max()));
    }

    void propertyWrittenFromQmlNotifiesOnce()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "filter" });
        m_recorder->markMoment(QStringLiteral("startup"));
        // QML writes the property; Nim stores it and notifies.
        wallet.setProperty("filter", QStringLiteral("eth"));
        m_recorder->markMoment(QStringLiteral("end"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        QSignalSpy changed(replayed, SIGNAL(filterChanged()));
        // Replayed QML makes the same write, then the recorded notify plays.
        replayed->setProperty("filter", QStringLiteral("eth"));
        QVERIFY(replayer->runUntilMoment(QStringLiteral("end")));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(replayed->property("filter"), QVariant(QStringLiteral("eth")));
    }

    void anUncoveredNotifyClearsTheQmlWrite()
    {
        // Hand-written: a notify without the property's value (never read when recorded), then
        // an equal-valued notify that must still reach QML.
        QDir dir(m_dir.path());
        dir.mkpath(QStringLiteral("schemas"));
        auto write = [&](const QString& name, const QByteArray& content) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(content);
        };
        write(QStringLiteral("manifest.json"), R"({"formatVersion":"1.0"})");
        write(QStringLiteral("schemas/0000000000000001.json"),
            R"json({"className":"V","base":"QObject","methods":[{"type":"signal","signature":"filterChanged()"}],)json"
            R"json("properties":[{"name":"filter","type":"QString","writable":true,"notify":0}]})json");
        write(QStringLiteral("timeline.jsonl"),
            "{\"k\":\"obj\",\"o\":1,\"s\":\"0000000000000001\",\"q\":0,\"t\":0}\n"
            "{\"k\":\"init\",\"o\":1,\"v\":{},\"q\":1,\"t\":0}\n"
            "{\"k\":\"ctx\",\"n\":\"v\",\"v\":{\"$obj\":1},\"q\":2,\"t\":0}\n"
            "{\"k\":\"sig\",\"o\":1,\"m\":\"filterChanged()\",\"q\":3,\"t\":0}\n"
            "{\"k\":\"sig\",\"o\":1,\"m\":\"filterChanged()\",\"p\":{\"filter\":\"eth\"},\"q\":4,\"t\":0}\n");

        QString error;
        auto replayer = Replayer::open(m_dir.path(), &error);
        QVERIFY2(replayer, qPrintable(error));
        QVERIFY(replayer->startAt(QString()));
        QVERIFY(replayer->step() && replayer->step() && replayer->step());
        QObject* replayed = objectOf(replayer->contextProperty("v"));
        QVERIFY(replayed);
        QSignalSpy changed(replayed, SIGNAL(filterChanged()));
        replayed->setProperty("filter", QStringLiteral("eth"));
        QCOMPARE(changed.size(), 1);
        while (replayer->step()) { }
        QCOMPARE(changed.size(), 3);
    }

    void objectsQmlPassesInAreNotFollowed()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        QObject qmlItem;
        qmlItem.setObjectName(QStringLiteral("someQmlItem"));
        call<int>(&wallet, "attach(QObject*)", static_cast<QObject*>(&qmlItem));
        m_recorder->flush();

        QFile timeline(m_dir.filePath(QStringLiteral("timeline.jsonl")));
        QVERIFY(timeline.open(QIODevice::ReadOnly));
        const QByteArray content = timeline.readAll();
        QVERIFY(content.contains("attach(QObject*)"));
        int objects = 0;
        for (const auto& line : content.split('\n'))
            objects += line.contains("\"k\":\"obj\"") ? 1 : 0;
        // Only the wallet: the QML object is not registered.
        QCOMPARE(objects, 1);
    }

    void replayerDestroyedWithPendingDeletionsIsSafe()
    {
        auto account = std::make_unique<Hooked<Account>>();
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "current" });
        wallet.setCurrent(account.get());
        wallet.setCurrent(nullptr);
        account.reset();
        m_recorder->markMoment(QStringLiteral("end"));

        auto replayer = replayAt(QString());
        QVERIFY(replayer);
        QVERIFY(replayer->runUntilMoment(QStringLiteral("end")));
        replayer.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    RecorderEdgesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_recorderedges.moc"
