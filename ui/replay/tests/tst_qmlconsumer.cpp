#include "fixtures.h"

#include <StatusReplay/recorder.h>
#include <StatusReplay/replayer.h>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

using namespace StatusReplay;

namespace {

// Binds like the app's stores do: properties, a model through a QVariant property, and a slot.
const char* const ConsumerQml = R"(
import QtQuick

Item {
    readonly property string title: walletSection.title
    readonly property bool loading: walletSection.loading
    readonly property string currentName: walletSection.current ? walletSection.current.name : ""
    readonly property int rows: repeater.count
    readonly property var names: {
        const result = []
        for (let i = 0; i < repeater.count; ++i)
            result.push(repeater.itemAt(i).accountName + "=" + repeater.itemAt(i).accountBalance)
        return result
    }
    readonly property string formatted: walletSection.formatBalance(5, "ETH")
    property string lastTransfer

    Repeater {
        id: repeater
        model: walletSection.accounts
        delegate: Item {
            required property string name
            required property int balance
            readonly property string accountName: name
            readonly property int accountBalance: balance
        }
    }

    Connections {
        target: walletSection
        function onTransferConfirmed(txHash, amount) { lastTransfer = txHash + ":" + amount }
    }
}
)";

} // namespace

class QmlConsumerTest : public QObject
{
    Q_OBJECT

private slots:
    void qmlSeesTheReplayedStateAndItsChanges()
    {
        QTemporaryDir dir;
        {
            Recorder recorder(dir.path());
            g_recorder = &recorder;
            Hooked<WalletView> wallet;
            Hooked<Account> savings;
            savings.setName(QStringLiteral("Savings"));
            wallet.setTitle(QStringLiteral("Wallet"));
            wallet.accountsModel()->insert(0, QStringLiteral("Main"), 10);
            recorder.setContextProperty(QStringLiteral("walletSection"), QVariant::fromValue<QObject*>(&wallet));
            readLikeQml(&wallet, { "title", "loading", "current", "accounts" });
            QString formatted;
            void* argv[] = { &formatted, nullptr, nullptr };
            int amount = 5;
            QString symbol = QStringLiteral("ETH");
            argv[1] = &amount;
            argv[2] = &symbol;
            QMetaObject::metacall(&wallet, QMetaObject::InvokeMetaMethod,
                wallet.metaObject()->indexOfMethod("formatBalance(int,QString)"), argv);
            recorder.markMoment(QStringLiteral("startup"));

            wallet.setLoading(true);
            wallet.accountsModel()->insert(1, QStringLiteral("Savings"), 20, &savings);
            readLikeQml(&savings, { "name" });
            wallet.accountsModel()->setBalance(0, 11);
            wallet.setCurrent(&savings);
            wallet.notifyTransfer(QStringLiteral("0xabc"), 3);
            recorder.markMoment(QStringLiteral("wallet"));
            g_recorder = nullptr;
        }

        QString error;
        auto replayer = Replayer::open(dir.path(), &error);
        QVERIFY2(replayer, qPrintable(error));
        QVERIFY2(replayer->startAt(QStringLiteral("startup"), &error), qPrintable(error));

        QQmlEngine engine;
        for (const auto& name : replayer->contextPropertyNames())
            engine.rootContext()->setContextProperty(name, replayer->contextProperty(name));
        QQmlComponent component(&engine);
        component.setData(ConsumerQml, QUrl(QStringLiteral("qrc:/consumer.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        QCOMPARE(root->property("title"), QVariant(QStringLiteral("Wallet")));
        QCOMPARE(root->property("loading"), QVariant(false));
        QCOMPARE(root->property("currentName"), QVariant(QString()));
        QCOMPARE(root->property("rows"), QVariant(1));
        QCOMPARE(root->property("names").toStringList(), QStringList { "Main=10" });
        QCOMPARE(root->property("formatted"), QVariant(QStringLiteral("5 ETH")));

        QVERIFY(replayer->runUntilMoment(QStringLiteral("wallet")));

        QCOMPARE(root->property("loading"), QVariant(true));
        QCOMPARE(root->property("rows"), QVariant(2));
        QCOMPARE(root->property("names").toStringList(), (QStringList { "Main=11", "Savings=20" }));
        QCOMPARE(root->property("currentName"), QVariant(QStringLiteral("Savings")));
        QCOMPARE(root->property("lastTransfer"), QVariant(QStringLiteral("0xabc:3")));
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QmlConsumerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_qmlconsumer.moc"
