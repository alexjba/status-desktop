#pragma once

#include <QObject>
#include <QTimer>

class QQmlEngine;
class QJSEngine;

// Spike-only (webwin reclaim experiment): polls <files>/webwin-step and runs one reclaim step.
class WebwinDebug : public QObject
{
    Q_OBJECT
public:
    explicit WebwinDebug(QQmlEngine* engine);
    static QObject* qmlInstance(QQmlEngine* engine, QJSEngine*);

signals:
    void unloadBrowserRequested();

private:
    void poll();
    void runStep(int step);

    QQmlEngine* m_engine;
    QTimer m_timer;
};
