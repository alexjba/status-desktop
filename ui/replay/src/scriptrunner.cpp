#include <StatusReplay/scriptrunner.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

namespace StatusReplay {

namespace {

constexpr int PollIntervalMs = 100;
constexpr int DefaultTimeoutMs = 60000;

bool isActionable(const QQuickItem* item)
{
    return item->window() && item->isVisible() && item->isEnabled() && item->opacity() > 0 && item->width() > 0
        && item->height() > 0;
}

// Walks the visual tree of every visible window, popups included (they live in the overlay).
// QObject parents don't follow the visual tree (a Window's items are owned by the window).
template <typename Visit>
void forEachItem(Visit&& visit)
{
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        auto quickWindow = qobject_cast<QQuickWindow*>(window);
        if (!quickWindow || !quickWindow->isVisible())
            continue;
        QQuickItem* root = quickWindow->contentItem();
        while (root->parentItem())
            root = root->parentItem();
        QList<QQuickItem*> pending { root };
        while (!pending.isEmpty()) {
            QQuickItem* item = pending.takeFirst();
            if (!visit(item))
                return;
            pending.append(item->childItems());
        }
    }
}

// A target is an objectName, or {"text": "..."} for controls without one (matched on `text`).
QQuickItem* findItem(const QJsonValue& target)
{
    const bool byText = target.isObject();
    const QString wanted = byText ? target.toObject()["text"].toString() : target.toString();
    QQuickItem* found = nullptr;
    forEachItem([&](QQuickItem* item) {
        const bool matches = byText ? item->property("text").toString() == wanted : item->objectName() == wanted;
        if (matches && isActionable(item))
            found = item;
        return !found;
    });
    return found;
}

bool isTextInput(const QQuickItem* item)
{
    return item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit");
}

QQuickItem* textInputOf(QQuickItem* item)
{
    QList<QQuickItem*> pending { item };
    while (!pending.isEmpty()) {
        QQuickItem* candidate = pending.takeFirst();
        if (isTextInput(candidate) && candidate->isVisible())
            return candidate;
        pending.append(candidate->childItems());
    }
    return nullptr;
}

void click(QQuickItem* item)
{
    QQuickWindow* window = item->window();
    const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(scene);
    const QPointingDevice* mouse = QPointingDevice::primaryPointingDevice();
    QMouseEvent press(
        QEvent::MouseButtonPress, scene, scene, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, mouse);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(
        QEvent::MouseButtonRelease, scene, scene, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier, mouse);
    QCoreApplication::sendEvent(window, &release);
}

bool type(QQuickItem* item, const QString& text)
{
    QQuickItem* input = textInputOf(item);
    if (!input)
        return false;
    input->forceActiveFocus();
    for (const QChar c : text) {
        const bool enter = c == QLatin1Char('\n');
        const int key = enter ? Qt::Key_Return : 0;
        const QString keyText = enter ? QStringLiteral("\r") : QString(c);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(input, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(input, &release);
    }
    return true;
}

// What a failing script could have targeted instead, to extend scripts against a changed UI.
QStringList visibleNames()
{
    QStringList names;
    forEachItem([&](QQuickItem* item) {
        if (!item->objectName().isEmpty() && isActionable(item))
            names.append(item->objectName());
        return true;
    });
    names.removeDuplicates();
    return names;
}

} // namespace

class ScriptRunnerPrivate
{
public:
    QJsonArray steps;
    // Which steps were appended (queued input) rather than part of the script.
    QList<bool> queued;
    // Whether the current step has seen the next step's item absent: only an item that shows up
    // while this step waits makes an optional one give way.
    bool nextWasMissing = false;
    int current = 0;
    // A queued step that failed since the queue last ran dry.
    QString queuedError;
    QElapsedTimer clock;
    qint64 stepStartedAt = 0;
    QTimer timer;
    // An item is acted on once it was found at the same place on two polls in a row, so page
    // transitions (StackView pushes ignore input while animating) have settled.
    QPointer<QQuickItem> candidate;
    QPointF candidatePosition;
};

ScriptRunner::ScriptRunner(const QJsonArray& steps, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<ScriptRunnerPrivate>())
{
    d->steps = steps;
    d->queued = QList<bool>(steps.size(), false);
    d->clock.start();
    d->timer.setSingleShot(true);
    connect(&d->timer, &QTimer::timeout, this, [this] {
        if (d->current >= d->steps.size()) {
            const QString error = std::exchange(d->queuedError, {});
            emit finished(error.isEmpty(), error);
            return;
        }
        const QJsonObject step = d->steps.at(d->current).toObject();
        auto next = [this](int delay) {
            d->candidate = nullptr;
            d->nextWasMissing = false;
            ++d->current;
            d->stepStartedAt = d->clock.elapsed();
            d->timer.start(delay);
        };
        auto skip = [this, &next](const QString& name, const QString& why) {
            qInfo().noquote() << QStringLiteral("StatusReplay: skipped optional step %1 ('%2'): %3")
                                     .arg(d->current)
                                     .arg(name, why);
            next(0);
        };

        if (step.contains("sleep"))
            return next(step["sleep"].toInt());
        if (step.contains("moment")) {
            emit momentRequested(step["moment"].toString());
            return next(0);
        }

        QString action;
        for (const char* key : { "click", "type", "setChecked", "waitFor" }) {
            if (step.contains(QLatin1String(key)))
                action = QString::fromLatin1(key);
        }
        if (action.isEmpty()) {
            emit finished(false, QStringLiteral("step %1: unknown action").arg(d->current));
            return;
        }
        const QString name = step[action].isObject() ? step[action].toObject()["text"].toString()
                                                     : step[action].toString();
        QQuickItem* item = findItem(step[action]);
        if (!item) {
            const bool optional = step["optional"].toBool();
            const bool queued = d->queued.value(d->current);
            // Counted from when the step became current: a burst of queued steps doesn't share
            // one budget.
            if (d->clock.elapsed() - d->stepStartedAt >= step["timeout"].toInt(DefaultTimeoutMs)) {
                if (optional)
                    return skip(name, QStringLiteral("no visible, enabled item"));
                const QString error = QStringLiteral("step %1: no visible, enabled item '%2'; visible named items: %3")
                                          .arg(d->current)
                                          .arg(name, visibleNames().join(QStringLiteral(", ")));
                // A script stops here; queued steps after it still run, and the failure is
                // reported when the queue runs dry.
                if (!queued) {
                    emit finished(false, error);
                    return;
                }
                qWarning().noquote() << "StatusReplay:" << error;
                d->queuedError = error;
                return next(0);
            }
            // Queued input: once the next step's item shows up while this one waits, this one is
            // not coming.
            if (optional && queued && d->current + 1 < d->steps.size()) {
                const QJsonObject following = d->steps.at(d->current + 1).toObject();
                for (const char* key : { "click", "setChecked", "type", "waitFor" }) {
                    if (!following.contains(QLatin1String(key)))
                        continue;
                    if (!findItem(following[QLatin1String(key)]))
                        d->nextWasMissing = true;
                    else if (d->nextWasMissing)
                        return skip(name, QStringLiteral("the next step's item showed up"));
                }
            }
            return d->timer.start(PollIntervalMs);
        }

        const QPointF position = item->mapToScene(QPointF(0, 0));
        if (d->candidate != item || d->candidatePosition != position) {
            d->candidate = item;
            d->candidatePosition = position;
            return d->timer.start(PollIntervalMs);
        }

        if (action == QLatin1String("click")) {
            click(item);
            emit acted(step);
        } else if (action == QLatin1String("type")) {
            if (!type(item, step["text"].toString())) {
                emit finished(false, QStringLiteral("step %1: '%2' has no text input").arg(d->current).arg(name));
                return;
            }
        } else if (action == QLatin1String("setChecked")) {
            if (item->property("checked").toBool() != step["value"].toBool()) {
                click(item);
                emit acted(step);
            }
        }
        next(PollIntervalMs);
    });
}

ScriptRunner::~ScriptRunner() = default;

void ScriptRunner::append(const QJsonObject& step)
{
    d->steps.append(step);
    d->queued.append(true);
    if (d->current == d->steps.size() - 1 && !d->timer.isActive()) {
        d->stepStartedAt = d->clock.elapsed();
        d->timer.start(0);
    }
}

void ScriptRunner::start()
{
    d->current = 0;
    d->stepStartedAt = d->clock.elapsed();
    d->timer.start(0);
}

} // namespace StatusReplay
