#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

#include <private/qhooks_p.h>

#include <StatusQ/qmlinventory.h>

namespace QmlInventory = Status::QmlInventory;

class tst_QmlInventory : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QUrl writeQml(const QString& name, const QByteArray& source)
    {
        QFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(source);
        return QUrl::fromLocalFile(file.fileName());
    }

    static QString urlOf(const QUrl& url)
    {
        return url.toString();
    }

    static int objectCount(const QJsonObject& snapshot, const QString& type,
                           const QString& creationFile, const QString& owningFile)
    {
        for (const QJsonValue& value : snapshot[QStringLiteral("objects")].toArray()) {
            const QJsonObject group = value.toObject();
            if (group[QStringLiteral("type")].toString() == type
                    && group[QStringLiteral("creationFile")].toString() == creationFile
                    && group[QStringLiteral("owningFile")].toString() == owningFile)
                return group[QStringLiteral("count")].toInt();
        }
        return 0;
    }

    static int typeCount(const QJsonObject& snapshot, const QString& type)
    {
        int count = 0;
        for (const QJsonValue& value : snapshot[QStringLiteral("objects")].toArray()) {
            const QJsonObject group = value.toObject();
            if (group[QStringLiteral("type")].toString() == type)
                count += group[QStringLiteral("count")].toInt();
        }
        return count;
    }

    static QJsonObject unitFor(const QJsonObject& snapshot, const QUrl& url)
    {
        for (const QJsonValue& value : snapshot[QStringLiteral("compilationUnits")].toArray()) {
            const QJsonObject unit = value.toObject();
            if (unit[QStringLiteral("url")].toString() == urlOf(url))
                return unit;
        }
        return {};
    }

private slots:
    void parseSchedule_data()
    {
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<QList<int>>("expected");

        QTest::newRow("unset") << QByteArray() << QList<int>{};
        QTest::newRow("single") << QByteArray("60") << QList<int>{60};
        QTest::newRow("several") << QByteArray("5, 30,120") << QList<int>{5, 30, 120};
        QTest::newRow("garbage") << QByteArray("soon") << QList<int>{};
        QTest::newRow("negative") << QByteArray("10,-1") << QList<int>{};
    }

    void parseSchedule()
    {
        QFETCH(QByteArray, value);
        QFETCH(QList<int>, expected);

        QCOMPARE(QmlInventory::parseSchedule(value), expected);
    }

    void nothingIsInstalledWithoutTheEnvironmentVariable()
    {
        QVERIFY(qEnvironmentVariableIsEmpty(QmlInventory::EnvVar));
        QVERIFY(!QmlInventory::installFromEnvironment());
        QVERIFY(!QmlInventory::isInstalled());
        QCOMPARE(qtHookData[QHooks::AddQObject], quintptr(0));
        QCOMPARE(qtHookData[QHooks::RemoveQObject], quintptr(0));
    }

    void groupsLiveObjectsAndCompilationUnits()
    {
        QVERIFY(m_dir.isValid());
        const QUrl leaf = writeQml(QStringLiteral("Leaf.qml"),
                                   "import QtQuick\n"
                                   "Item { property int tag; Rectangle {} }\n");
        const QUrl orphan = writeQml(QStringLiteral("Orphan.qml"),
                                     "import QtQml\n"
                                     "QtObject { property int tag }\n");
        const QUrl unused = writeQml(QStringLiteral("Unused.qml"),
                                     "import QtQuick\n"
                                     "Item { property int tag }\n");
        const QUrl main = writeQml(QStringLiteral("Main.qml"),
                                   "import QtQuick\n"
                                   "Item {\n"
                                   "    property var orphan: Qt.createComponent(\"Orphan.qml\").createObject(null)\n"
                                   "    Repeater { model: 3; delegate: Leaf {} }\n"
                                   "}\n");

        QmlInventory::install();
        QVERIFY(QmlInventory::isInstalled());

        QQmlEngine engine;
        QQmlComponent unusedComponent(&engine, unused);
        QVERIFY2(unusedComponent.isReady(), qPrintable(unusedComponent.errorString()));

        QQmlComponent mainComponent(&engine, main);
        std::unique_ptr<QObject> root(mainComponent.create());
        QVERIFY2(root, qPrintable(mainComponent.errorString()));

        QJsonObject snapshot = QmlInventory::snapshot();

        // A component instance is declared in the file that uses it and owned by
        // whoever instantiated that file; its insides are declared in its own file.
        QCOMPARE(objectCount(snapshot, QStringLiteral("Leaf"), urlOf(main), QString()), 3);
        QCOMPARE(objectCount(snapshot, QStringLiteral("QQuickRectangle"), urlOf(leaf), urlOf(main)), 3);
        QCOMPARE(objectCount(snapshot, QStringLiteral("QQuickRepeater"), urlOf(main), QString()), 1);
        QCOMPARE(objectCount(snapshot, QStringLiteral("Orphan"), urlOf(orphan), urlOf(main)), 1);
        QCOMPARE(typeCount(snapshot, QStringLiteral("QQmlEngine")), 1);

        const QJsonArray objects = snapshot[QStringLiteral("objects")].toArray();
        const auto orphanGroup = std::find_if(objects.begin(), objects.end(), [](const QJsonValue& v) {
            return v.toObject()[QStringLiteral("type")].toString() == QStringLiteral("Orphan");
        });
        QVERIFY(orphanGroup != objects.end());
        QCOMPARE(orphanGroup->toObject()[QStringLiteral("parentless")].toInt(), 1);

        const QJsonObject leafUnit = unitFor(snapshot, leaf);
        QVERIFY(!leafUnit.isEmpty());
        QCOMPARE(leafUnit[QStringLiteral("liveObjects")].toInt(), 6);
        QVERIFY(leafUnit[QStringLiteral("unitBytes")].toInt() > 0);
        QVERIFY(leafUnit[QStringLiteral("totalBytes")].toInt()
                >= leafUnit[QStringLiteral("unitBytes")].toInt());

        const QJsonObject unusedUnit = unitFor(snapshot, unused);
        QVERIFY(!unusedUnit.isEmpty());
        QCOMPARE(unusedUnit[QStringLiteral("liveObjects")].toInt(), 0);

        QVERIFY(unitFor(snapshot, main)[QStringLiteral("liveObjects")].toInt() > 0);

        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        snapshot = QmlInventory::snapshot();

        QCOMPARE(typeCount(snapshot, QStringLiteral("Leaf")), 0);
        QCOMPARE(typeCount(snapshot, QStringLiteral("QQuickRectangle")), 0);
        QCOMPARE(typeCount(snapshot, QStringLiteral("QQuickRepeater")), 0);
        QCOMPARE(unitFor(snapshot, leaf)[QStringLiteral("liveObjects")].toInt(), 0);
    }

    void writesTheSnapshotToTheOutputDir()
    {
        QTemporaryDir out;
        QVERIFY(out.isValid());
        QmlInventory::setOutputDir(out.path());

        const QString path = QmlInventory::writeSnapshot(2);

        QVERIFY(path.startsWith(out.path()));
        QVERIFY(QFileInfo(path).fileName().startsWith(QStringLiteral("qml-inventory-")));
        QVERIFY(path.endsWith(QStringLiteral("-2.json")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(json.contains(QStringLiteral("objects")));
        QVERIFY(json.contains(QStringLiteral("compilationUnits")));
    }

    void theEnvironmentScheduleWritesEachSnapshot()
    {
        QTemporaryDir out;
        QVERIFY(out.isValid());
        QmlInventory::setOutputDir(out.path());
        qputenv(QmlInventory::EnvVar, "0,0");

        QVERIFY(QmlInventory::installFromEnvironment());
        qunsetenv(QmlInventory::EnvVar);

        const QDir dir(out.path());
        QTRY_COMPARE(dir.entryList({QStringLiteral("qml-inventory-*-1.json")}, QDir::Files).size(), 1);
        QTRY_COMPARE(dir.entryList({QStringLiteral("qml-inventory-*-2.json")}, QDir::Files).size(), 1);
    }
};

QTEST_MAIN(tst_QmlInventory)
#include "tst_qmlinventory.moc"
