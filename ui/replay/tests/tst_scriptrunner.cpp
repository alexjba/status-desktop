#include <StatusReplay/scriptrunner.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace StatusReplay;

namespace {

const char* const PageQml = R"(
import QtQuick
import QtQuick.Controls

// A Window root like main.qml: its items are QObject children of the window, not of an item.
Window {
    id: window
    width: 400
    height: 400
    visible: true

    readonly property string entered: nameInput.text
    property string accepted
    readonly property bool saved: savePassword.checked
    readonly property bool dontAsk: dontAskAgain.checked
    property int presses: 0

    Column {
        TextField {
            id: nameInput
            objectName: "nameInput"
            width: 200
            onAccepted: window.accepted = text
        }
        Switch {
            id: savePassword
            objectName: "savePassword"
            checked: true
        }
        Switch {
            id: dontAskAgain
            // \x27: a quote character would throw moc off inside this raw string.
            text: "Don\x27t ask me again"
        }
        Button {
            objectName: "confirmButton"
            text: "Confirm"
            enabled: nameInput.text.length > 0
            onClicked: {
                window.presses += 1
                later.visible = true
            }
        }
        Button {
            id: later
            objectName: "laterButton"
            text: "Later"
            visible: false
            onClicked: window.presses += 10
        }
    }
}
)";

QJsonArray steps(const char* json)
{
    return QJsonDocument::fromJson(json).array();
}

} // namespace

class ScriptRunnerTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;

    QObject* root() const { return m_engine->rootObjects().value(0); }

private slots:
    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadData(PageQml);
        auto window = qobject_cast<QQuickWindow*>(root());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }

    void cleanup() { m_engine.reset(); }

    void drivesTheUiThroughObjectNames()
    {
        ScriptRunner runner(steps(R"([
            { "type": "nameInput", "text": "TestPassword2026\n" },
            { "setChecked": "savePassword", "value": false },
            { "setChecked": { "text": "Don\u0027t ask me again" }, "value": true },
            { "click": "confirmButton" },
            { "click": "laterButton" },
            { "moment": "done" },
            { "click": "missingButton", "optional": true, "timeout": 200 }
        ])"));
        QStringList moments;
        connect(&runner, &ScriptRunner::momentRequested, this, [&](const QString& name) { moments << name; });
        QStringList acted;
        connect(&runner, &ScriptRunner::acted, this, [&](const QJsonObject& step) { acted << step.keys().first(); });
        QSignalSpy finished(&runner, &ScriptRunner::finished);
        runner.start();
        QVERIFY(finished.wait(10000));

        QCOMPARE(finished[0][0].toBool(), true);
        QCOMPARE(root()->property("entered"), QVariant(QStringLiteral("TestPassword2026")));
        QCOMPARE(root()->property("accepted"), QVariant(QStringLiteral("TestPassword2026")));
        QCOMPARE(root()->property("saved"), QVariant(false));
        QCOMPARE(root()->property("dontAsk"), QVariant(true));
        QCOMPARE(root()->property("presses"), QVariant(11));
        QCOMPARE(moments, QStringList { "done" });
        // Typing is not replayable input (it can carry test credentials); clicks and toggles are.
        QCOMPARE(acted, (QStringList { "setChecked", "setChecked", "click", "click" }));
    }

    void stopsAtARequiredItemThatNeverAppears()
    {
        ScriptRunner runner(steps(R"([
            { "click": "missingButton", "timeout": 200 },
            { "click": "confirmButton" }
        ])"));
        QSignalSpy finished(&runner, &ScriptRunner::finished);
        runner.start();
        QVERIFY(finished.wait(5000));

        QCOMPARE(finished[0][0].toBool(), false);
        QVERIFY2(finished[0][1].toString().contains(QStringLiteral("missingButton")),
            qPrintable(finished[0][1].toString()));
        QCOMPARE(root()->property("presses"), QVariant(0));
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QT_LOGGING_RULES", "qt.qpa.fonts=false");
    QGuiApplication app(argc, argv);
    ScriptRunnerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_scriptrunner.moc"
