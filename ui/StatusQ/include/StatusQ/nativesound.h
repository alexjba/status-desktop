#pragma once

#include <QObject>
#include <QUrl>

// Short UI sounds through the platform (Android SoundPool) instead of QtMultimedia, which
// the Android build does not ship. available() is false where QtMultimedia must be used.
class NativeSound : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit NativeSound(QObject* parent = nullptr);

    bool available() const;
    Q_INVOKABLE void play(const QUrl& source, qreal volume);
    Q_INVOKABLE void stop(const QUrl& source);
};
