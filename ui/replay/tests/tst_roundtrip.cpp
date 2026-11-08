#include "fixtures.h"

#include <StatusReplay/recorder.h>
#include <StatusReplay/replayer.h>

#include <QMetaMethod>
#include <QMetaProperty>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

QObject* objectOf(const QVariant& value)
{
    return value.value<QObject*>();
}

QStringList signatures(const QMetaObject* mo, QMetaMethod::MethodType type)
{
    QStringList result;
    for (int i = QObject::staticMetaObject.methodCount(); i < mo->methodCount(); ++i) {
        const auto m = mo->method(i);
        if (m.methodType() == type && m.methodSignature() != "deleteLater()")
            result << QString::fromLatin1(m.methodSignature());
    }
    result.sort();
    return result;
}

// Calls through qt_metacall like QML does for Nim objects, whose metaobjects have no static metacall.
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

} // namespace

class RoundTripTest : public QObject
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
        if (!replayer) {
            qWarning() << "open failed:" << error;
            return {};
        }
        if (!replayer->startAt(moment, &error)) {
            qWarning() << "startAt failed:" << error;
            return {};
        }
        return replayer;
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_dir.setAutoRemove(true);
        m_recorder = std::make_unique<Recorder>(m_dir.path());
        QVERIFY2(m_recorder->isValid(), qPrintable(m_recorder->errorString()));
        g_recorder = m_recorder.get();
    }

    void cleanup()
    {
        g_recorder = nullptr;
        m_recorder.reset();
        m_dir.remove();
        m_dir = QTemporaryDir();
    }

    void snapshotKeepsPropertyValuesAndNestedObjects()
    {
        Hooked<WalletView> wallet;
        Hooked<Account> account;
        wallet.setTitle(QStringLiteral("Main wallet"));
        wallet.setLoading(true);
        account.setName(QStringLiteral("Savings"));
        account.setBalance(42);
        wallet.setCurrent(&account);

        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        m_recorder->setContextProperty(QStringLiteral("production"), false);
        readLikeQml(&wallet, { "title", "loading", "rate", "timestamp", "current", "accounts", "filter" });
        readLikeQml(&account, { "name", "balance" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);

        QCOMPARE(replayer->contextPropertyNames(), (QStringList { "production", "walletSection" }));
        QCOMPARE(replayer->contextProperty("production"), QVariant(false));

        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        QVERIFY(replayed);
        QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("Main wallet")));
        QCOMPARE(replayed->property("loading"), QVariant(true));
        QCOMPARE(replayed->property("rate"), QVariant(1.25));
        QCOMPARE(replayed->property("timestamp"), QVariant(qint64(1'700'000'000'123)));

        QObject* current = objectOf(replayed->property("current"));
        QVERIFY(current);
        QCOMPARE(current->property("name"), QVariant(QStringLiteral("Savings")));
        QCOMPARE(current->property("balance"), QVariant(42));
    }

    void replayedMetaObjectMatchesTheRecordedOne()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        QVERIFY(replayed);

        const QMetaObject* source = wallet.metaObject();
        const QMetaObject* target = replayed->metaObject();
        QCOMPARE(QString::fromLatin1(target->className()), QString::fromLatin1(source->className()));
        QCOMPARE(signatures(target, QMetaMethod::Signal), signatures(source, QMetaMethod::Signal));
        QCOMPARE(signatures(target, QMetaMethod::Slot) + signatures(target, QMetaMethod::Method),
            signatures(source, QMetaMethod::Slot) + signatures(source, QMetaMethod::Method));

        QCOMPARE(target->propertyCount(), source->propertyCount());
        for (int i = QObject::staticMetaObject.propertyCount(); i < source->propertyCount(); ++i) {
            const QMetaProperty expected = source->property(i);
            const int at = target->indexOfProperty(expected.name());
            QVERIFY2(at >= 0, expected.name());
            const QMetaProperty actual = target->property(at);
            QCOMPARE(actual.metaType(), expected.metaType());
            QCOMPARE(actual.isWritable(), expected.isWritable());
            QCOMPARE(actual.isConstant(), expected.isConstant());
            QCOMPARE(actual.hasNotifySignal(), expected.hasNotifySignal());
            if (expected.hasNotifySignal())
                QCOMPARE(actual.notifySignal().methodSignature(), expected.notifySignal().methodSignature());
        }
    }

    void snapshotKeepsModelRowsWithObjectRoles()
    {
        Hooked<WalletView> wallet;
        Hooked<Account> savings;
        savings.setName(QStringLiteral("Savings"));
        wallet.accountsModel()->insert(0, QStringLiteral("Main"), 10);
        wallet.accountsModel()->insert(1, QStringLiteral("Savings"), 20, &savings);

        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "accounts" });
        readLikeQml(wallet.accountsModel(), { "count" });
        readRolesLikeQml(wallet.accountsModel(), { AccountsModel::NameRole, AccountsModel::BalanceRole, AccountsModel::AccountRole });
        readLikeQml(&savings, { "name" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        auto model = qobject_cast<QAbstractItemModel*>(objectOf(replayed->property("accounts")));
        QVERIFY(model);

        QCOMPARE(model->roleNames(), wallet.accountsModel()->roleNames());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->property("count"), QVariant(2));
        QCOMPARE(model->data(model->index(0, 0), AccountsModel::NameRole), QVariant(QStringLiteral("Main")));
        QCOMPARE(model->data(model->index(1, 0), AccountsModel::BalanceRole), QVariant(20));
        QVERIFY(!objectOf(model->data(model->index(0, 0), AccountsModel::AccountRole)));
        QObject* account = objectOf(model->data(model->index(1, 0), AccountsModel::AccountRole));
        QVERIFY(account);
        QCOMPARE(account->property("name"), QVariant(QStringLiteral("Savings")));
    }

    void slotsReturnTheRecordedValueForTheSameArguments()
    {
        Hooked<WalletView> wallet;
        Hooked<Account> savings;
        savings.setName(QStringLiteral("Savings"));
        wallet.accountsModel()->insert(0, QStringLiteral("Savings"), 20, &savings);
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));

        QString formatted;
        formatted = call<QString>(&wallet, "formatBalance(int,QString)", 5, QStringLiteral("ETH"));
        formatted = call<QString>(&wallet, "formatBalance(int,QString)", 7, QStringLiteral("SNT"));
        QVariant account;
        account = call<QVariant>(&wallet, "accountAt(int)", 0);
        readLikeQml(&savings, { "name" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));

        QString result;
        result = call<QString>(replayed, "formatBalance(int,QString)", 5, QStringLiteral("ETH"));
        QCOMPARE(result, QStringLiteral("5 ETH"));
        result = call<QString>(replayed, "formatBalance(int,QString)", 7, QStringLiteral("SNT"));
        QCOMPARE(result, QStringLiteral("7 SNT"));

        // Unrecorded arguments fall back to the latest recorded call.
        result = call<QString>(replayed, "formatBalance(int,QString)", 1, QStringLiteral("DAI"));
        QCOMPARE(result, QStringLiteral("7 SNT"));

        QVariant replayedAccount;
        replayedAccount = call<QVariant>(replayed, "accountAt(int)", 0);
        QVERIFY(objectOf(replayedAccount));
        QCOMPARE(objectOf(replayedAccount)->property("name"), QVariant(QStringLiteral("Savings")));

        // Never-recorded slots return the type's default.
        bool loggedIn = true;
        loggedIn = call<bool>(replayed, "login(QString)", QStringLiteral("x"));
        QCOMPARE(loggedIn, false);

        const auto calls = replayer->calls();
        QCOMPARE(calls.size(), 5);
        QCOMPARE(calls[0].matched, true);
        QCOMPARE(calls[2].matched, false);
        QCOMPARE(calls[4].method, QStringLiteral("login(QString)"));
        QCOMPARE(calls[4].matched, false);
    }

    void slotCalledEarlierThanRecordedStillMatchesItsArguments()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        call<QString>(&wallet, "formatBalance(int,QString)", 1, QStringLiteral("DAI"));
        m_recorder->markMoment(QStringLiteral("startup"));
        call<QString>(&wallet, "formatBalance(int,QString)", 2, QStringLiteral("ETH"));
        m_recorder->markMoment(QStringLiteral("later"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        QCOMPARE(call<QString>(replayed, "formatBalance(int,QString)", 2, QStringLiteral("ETH")),
            QStringLiteral("2 ETH"));
        QVERIFY(replayer->calls().constLast().matched);
    }

    void sensitiveArgumentsAreNotWritten()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        bool ok = false;
        ok = call<bool>(&wallet, "login(QString)", QStringLiteral("secret"));
        QVERIFY(ok);
        m_recorder->markMoment(QStringLiteral("startup"));
        m_recorder->flush();

        for (const auto& name : QDir(m_dir.path()).entryList(QDir::Files)) {
            QFile file(m_dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY2(!file.readAll().contains("\"secret\""), qPrintable(name));
        }
        QFile timeline(m_dir.filePath("timeline.jsonl"));
        QVERIFY(timeline.open(QIODevice::ReadOnly));
        QVERIFY(timeline.readAll().contains("login(QString)"));
    }

    void sensitiveNamesOnlyHideText()
    {
        Hooked<Account> account;
        m_recorder->setContextProperty(QStringLiteral("account"), QVariant::fromValue<QObject*>(&account));
        readLikeQml(&account, { "mnemonicBackedUp", "mnemonic" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("account"));
        // A flag named after a secret is not a secret (the backup banner depends on it).
        QCOMPARE(replayed->property("mnemonicBackedUp"), QVariant(true));
        QCOMPARE(replayed->property("mnemonic"), QVariant(QStringLiteral("$redacted")));
    }

    void sensitiveFieldsInsideJsonArgumentsAreNotWritten()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        call<QString>(&wallet, "loginRequested(QString,int,QString)", QStringLiteral("0xkey"), 1,
            QStringLiteral(R"({"password":"hunter2","keycard":{"pin":"123456"},"mnemonic":"apple banana","name":"Alice"})"));
        m_recorder->flush();

        QFile timeline(m_dir.filePath("timeline.jsonl"));
        QVERIFY(timeline.open(QIODevice::ReadOnly));
        const QByteArray content = timeline.readAll();
        QVERIFY(!content.contains("hunter2"));
        QVERIFY(!content.contains("123456"));
        QVERIFY(!content.contains("apple banana"));
        QVERIFY(content.contains("Alice"));
        QVERIFY(content.contains("0xkey"));
    }

    void timelineReplaysChangesInRecordedOrder()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "title", "loading", "rate", "timestamp", "current", "accounts", "filter" });
        m_recorder->markMoment(QStringLiteral("startup"));

        auto* model = wallet.accountsModel();
        Hooked<Account> savings;
        savings.setName(QStringLiteral("Savings"));
        wallet.setTitle(QStringLiteral("Loading"));
        model->insert(0, QStringLiteral("A"), 1);
        // The first delegate reads its roles once the first row exists.
        readRolesLikeQml(model, { AccountsModel::NameRole, AccountsModel::BalanceRole, AccountsModel::AccountRole });
        model->insert(1, QStringLiteral("B"), 2, &savings);
        readLikeQml(&savings, { "name", "balance" });
        model->insert(2, QStringLiteral("C"), 3);
        wallet.setLoading(true);
        model->move(0, 2);
        model->setBalance(1, 99);
        model->remove(0);
        wallet.notifyTransfer(QStringLiteral("0xabc"), 5);
        savings.setBalance(7);
        wallet.setCurrent(&savings);
        m_recorder->markMoment(QStringLiteral("wallet"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        auto replayedModel = qobject_cast<QAbstractItemModel*>(objectOf(replayed->property("accounts")));
        QVERIFY(replayedModel);
        QCOMPARE(replayedModel->rowCount(), 0);

        QStringList seen;
        connect(replayed, SIGNAL(titleChanged()), this, SLOT(recordSeen()));
        auto log = [&](const QString& what) { seen << what; };
        connect(replayedModel, &QAbstractItemModel::rowsInserted, this,
            [&](const QModelIndex&, int f, int) { log(QStringLiteral("ins %1").arg(f)); });
        connect(replayedModel, &QAbstractItemModel::rowsMoved, this,
            [&](const QModelIndex&, int f, int, const QModelIndex&, int to) {
                log(QStringLiteral("mov %1 %2").arg(f).arg(to));
            });
        connect(replayedModel, &QAbstractItemModel::dataChanged, this,
            [&](const QModelIndex& tl, const QModelIndex&, const QList<int>& roles) {
                log(QStringLiteral("data %1 %2").arg(tl.row()).arg(roles.value(0)));
            });
        connect(replayedModel, &QAbstractItemModel::rowsRemoved, this,
            [&](const QModelIndex&, int f, int) { log(QStringLiteral("rem %1").arg(f)); });
        m_seen = &seen;
        QSignalSpy transfers(replayed, SIGNAL(transferConfirmed(QString, int)));
        connect(replayed, SIGNAL(transferConfirmed(QString, int)), this, SLOT(recordSeen()));
        connect(replayed, SIGNAL(loadingChanged()), this, SLOT(recordSeen()));
        connect(replayed, SIGNAL(currentChanged()), this, SLOT(recordSeen()));

        QVERIFY(replayer->runUntilMoment(QStringLiteral("wallet")));
        m_seen = nullptr;

        QCOMPARE(seen,
            (QStringList { "titleChanged()", "ins 0", "ins 1", "ins 2", "loadingChanged()", "mov 0 3", "data 1 258",
                "rem 0", "transferConfirmed(QString,int)", "currentChanged()" }));
        QCOMPARE(transfers.size(), 1);
        QCOMPARE(transfers[0][0], QVariant(QStringLiteral("0xabc")));
        QCOMPARE(transfers[0][1], QVariant(5));

        QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("Loading")));
        QCOMPARE(replayedModel->rowCount(), 2);
        QCOMPARE(replayedModel->data(replayedModel->index(0, 0), AccountsModel::NameRole), QVariant("C"));
        QCOMPARE(replayedModel->data(replayedModel->index(0, 0), AccountsModel::BalanceRole), QVariant(99));
        QCOMPARE(replayedModel->data(replayedModel->index(1, 0), AccountsModel::NameRole), QVariant("A"));
        QObject* current = objectOf(replayed->property("current"));
        QVERIFY(current);
        QCOMPARE(current->property("name"), QVariant(QStringLiteral("Savings")));
        QCOMPARE(current->property("balance"), QVariant(7));
    }

    void fullTimelineReachesTheSameStateAsTheMomentSnapshot()
    {
        auto wallet = std::make_unique<Hooked<WalletView>>();
        m_recorder->setContextProperty(QStringLiteral("production"), true);
        m_recorder->markLoaded();
        m_recorder->markMoment(QStringLiteral("startup"));
        wallet->setTitle(QStringLiteral("Wallet"));
        wallet->accountsModel()->resetTo({ QStringLiteral("X"), QStringLiteral("Y") });
        // Added mid-run, like the main module after login.
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(wallet.get()));
        readLikeQml(wallet.get(), { "title", "loading", "rate", "timestamp", "current", "accounts", "filter" });
        readRolesLikeQml(wallet->accountsModel(), { AccountsModel::NameRole, AccountsModel::BalanceRole, AccountsModel::AccountRole });
        auto transient = std::make_unique<Hooked<Account>>();
        wallet->setCurrent(transient.get());
        wallet->setCurrent(nullptr);
        transient.reset();
        m_recorder->markMoment(QStringLiteral("loggedIn"));

        auto fromSnapshot = replayAt(QStringLiteral("loggedIn"));
        QVERIFY(fromSnapshot);
        auto fromStart = replayAt(QString());
        QVERIFY(fromStart);
        QVERIFY(!fromStart->loadReached());
        QCOMPARE(fromStart->contextPropertyNames(), QStringList());

        QStringList changed;
        connect(fromStart.get(), &Replayer::contextPropertyChanged, this,
            [&](const QString& name, const QVariant&) { changed << name; });
        QVERIFY(fromStart->runUntilLoaded());
        QCOMPARE(changed, QStringList { "production" });
        QVERIFY(fromStart->runUntilMoment(QStringLiteral("loggedIn")));
        QCOMPARE(changed, (QStringList { "production", "walletSection" }));

        for (auto* replayer : { fromSnapshot.get(), fromStart.get() }) {
            QVERIFY(replayer->loadReached());
            QCOMPARE(replayer->contextPropertyNames(), (QStringList { "production", "walletSection" }));
            QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
            QVERIFY(replayed);
            QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("Wallet")));
            QVERIFY(!objectOf(replayed->property("current")));
            auto model = qobject_cast<QAbstractItemModel*>(objectOf(replayed->property("accounts")));
            QVERIFY(model);
            QCOMPARE(model->rowCount(), 2);
            QCOMPARE(model->data(model->index(1, 0), AccountsModel::NameRole), QVariant("Y"));
        }
        QCOMPARE(fromSnapshot->moments(), (QStringList { "startup", "loggedIn" }));
    }

    void notifyIsReplayedWhenTheReadWasRecordedFirst()
    {
        Hooked<WalletView> wallet;
        // QML's binding connected before the recorder followed the object: it re-reads on the
        // notify before the recorder sees the signal, so the read lands in the timeline first.
        QObject binding;
        connect(&wallet, &WalletView::titleChanged, &binding, [&] { readLikeQml(&wallet, { "title" }); });
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "title" });
        m_recorder->markMoment(QStringLiteral("startup"));
        wallet.setTitle(QStringLiteral("Renamed"));
        m_recorder->markMoment(QStringLiteral("end"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
        QObject* replayed = objectOf(replayer->contextProperty("walletSection"));
        QSignalSpy changed(replayed, SIGNAL(titleChanged()));
        QVERIFY(replayer->runUntilMoment(QStringLiteral("end")));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("Renamed")));
    }

    void valuesReadLateAreKnownFromTheStartOfTheirSegment()
    {
        Hooked<WalletView> wallet;
        Hooked<Account> account;
        account.setName(QStringLiteral("Savings"));
        wallet.setTitle(QStringLiteral("Before"));
        wallet.setCurrent(&account);
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        wallet.accountsModel()->insert(0, QStringLiteral("A"), 1);
        // QML reads the title only now, long after the object appeared; reading `current`
        // is also when the recorder first sees that object.
        readLikeQml(&wallet, { "title", "current" });
        readLikeQml(&account, { "name" });
        wallet.setTitle(QStringLiteral("After"));
        wallet.accountsModel()->insert(1, QStringLiteral("B"), 2);
        readLikeQml(&wallet, { "loading" });
        wallet.setLoading(true);
        m_recorder->markMoment(QStringLiteral("end"));

        auto replayer = replayAt(QString());
        QVERIFY(replayer);
        QObject* replayed = nullptr;
        connect(replayer.get(), &Replayer::contextPropertyChanged, this,
            [&](const QString&, const QVariant& value) { replayed = objectOf(value); });
        while (!replayed && replayer->step()) { }
        QVERIFY(replayed);
        // A replayed QML reading earlier than the recorded one still sees the recorded value.
        QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("Before")));
        QCOMPARE(replayed->property("loading"), QVariant(false));
        QObject* current = objectOf(replayed->property("current"));
        QVERIFY(current);
        QCOMPARE(current->property("name"), QVariant(QStringLiteral("Savings")));

        QSignalSpy titleChanged(replayed, SIGNAL(titleChanged()));
        QVERIFY(replayer->runUntilMoment(QStringLiteral("end")));
        QCOMPARE(titleChanged.size(), 1);
        QCOMPARE(replayed->property("title"), QVariant(QStringLiteral("After")));
        QCOMPARE(replayed->property("loading"), QVariant(true));
    }

    void scriptedUiActionsReplayAtTheirPoint()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        m_recorder->markLoaded();
        m_recorder->recordUiAction({ { "click", "Wallet-navbar" } });
        m_recorder->markMoment(QStringLiteral("wallet"));

        auto replayer = replayAt(QString());
        QVERIFY(replayer);
        QSignalSpy actions(replayer.get(), &Replayer::uiAction);
        QVERIFY(replayer->runUntilLoaded());
        QCOMPARE(actions.size(), 0);
        QVERIFY(replayer->runUntilMoment(QStringLiteral("wallet")));
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions[0][0].toJsonObject()["click"].toString(), QStringLiteral("Wallet-navbar"));
    }

    void slotThatDeletesItsObjectIsSafeToObserve()
    {
        auto account = new Hooked<Account>();
        m_recorder->setContextProperty(QStringLiteral("account"), QVariant::fromValue<QObject*>(account));
        call<int>(account, "dispose()");
        m_recorder->markMoment(QStringLiteral("startup"));

        auto replayer = replayAt(QStringLiteral("startup"));
        QVERIFY(replayer);
    }

    void propertiesQmlNeverReadAreNotReadByTheRecorder()
    {
        Hooked<WalletView> wallet;
        m_recorder->setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
        readLikeQml(&wallet, { "title" });
        wallet.setTitle(QStringLiteral("Changed"));
        m_recorder->markMoment(QStringLiteral("startup"));
        QCOMPARE(wallet.fragileReads(), 0);

        // Once QML reads it, the recorder follows it like any other property.
        readLikeQml(&wallet, { "fragile" });
        wallet.setTitle(QStringLiteral("Again"));
        QCOMPARE(wallet.fragileReads(), 2);
    }

    void committedFixtureLoadsAndPlays()
    {
        QString error;
        auto replayer = Replayer::open(QStringLiteral(STATUSREPLAY_FIXTURE_DIR), &error);
        QVERIFY2(replayer, qPrintable(error));
        QCOMPARE(replayer->moments(), QStringList { "startup" });
        QVERIFY2(replayer->startAt(QStringLiteral("startup"), &error), qPrintable(error));
        QVERIFY(replayer->loadReached());
        QObject* view = objectOf(replayer->contextProperty("fixtureView"));
        QVERIFY(view);
        QCOMPARE(view->property("title"), QVariant(QStringLiteral("Fixture")));
        QSignalSpy changed(view, SIGNAL(titleChanged()));
        QVERIFY(replayer->step());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(view->property("title"), QVariant(QStringLiteral("Changed")));
        QVERIFY(replayer->atEnd());
    }

    void unknownMajorFormatVersionIsRejected()
    {
        m_recorder->markMoment(QStringLiteral("startup"));
        m_recorder->flush();
        QFile manifest(m_dir.filePath("manifest.json"));
        QVERIFY(manifest.open(QIODevice::ReadWrite));
        QByteArray content = manifest.readAll();
        QVERIFY(content.contains("\"formatVersion\": \"1."));
        content.replace("\"formatVersion\": \"1.", "\"formatVersion\": \"2.");
        manifest.resize(0);
        manifest.write(content);
        manifest.close();

        QString error;
        QVERIFY(!Replayer::open(m_dir.path(), &error));
        QVERIFY2(error.contains(QStringLiteral("unsupported recording format 2.")), qPrintable(error));
    }

public slots:
    void recordSeen()
    {
        if (m_seen)
            *m_seen << QString::fromLatin1(sender()->metaObject()->method(senderSignalIndex()).methodSignature());
    }

private:
    QStringList* m_seen = nullptr;
};

QTEST_GUILESS_MAIN(RoundTripTest)
#include "tst_roundtrip.moc"
