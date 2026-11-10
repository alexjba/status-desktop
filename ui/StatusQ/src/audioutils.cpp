#include "StatusQ/audioutils.h"

#include <QtMath>

AudioUtils::AudioUtils(QObject *parent)
    : QObject(parent)
{}

// QAudio::convertVolume(volume, LogarithmicVolumeScale, LinearVolumeScale), without
// linking QtMultimedia (not shipped on Android)
qreal AudioUtils::convertLogarithmicToLinearVolumeScale(qreal volume) const
{
    constexpr qreal log100 = 4.60517018599;
    volume = qMax(qreal(0), volume);
    return volume < 0.99 ? -qLn(1 - volume) / log100 : 1;
}
