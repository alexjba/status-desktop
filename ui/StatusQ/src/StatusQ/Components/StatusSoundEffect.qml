import QtQuick
import QtMultimedia

import StatusQ

Item {
    id: root

    property bool muted: false
    property real volume: 1.0
    property url source

    readonly property bool playing: d.soundEffect ? d.soundEffect.playing : false
    readonly property bool isError: d.status === SoundEffect.Error
    readonly property string statusString: d.status

    function play() {
        if (NativeSound.available) {
            if (!root.muted)
                NativeSound.play(root.source, root.volume)
            return
        }

        if (!d.soundEffect) {
            MediaBackend.ensureLoaded()
            d.soundEffect = soundEffectComponent.createObject(root)
        }

        d.soundEffect.play()
    }

    function stop() {
        if (NativeSound.available) {
            NativeSound.stop(root.source)
            return
        }

        if (d.soundEffect)
            d.soundEffect.stop()
    }

    function convertVolume(volume) {
        return AudioUtils.convertLogarithmicToLinearVolumeScale(volume)
    }

    QtObject {
        id: d

        property SoundEffect soundEffect: null
        readonly property int status: soundEffect ? soundEffect.status : SoundEffect.Null

        function scheduleRelease() {
            Qt.callLater(d.releaseIfIdle)
        }

        function releaseIfIdle() {
            if (!soundEffect || soundEffect.playing)
                return

            soundEffect.destroy()
            soundEffect = null
        }
    }

    Component {
        id: soundEffectComponent

        SoundEffect {
            source: root.source
            volume: root.volume
            muted: root.muted

            onPlayingChanged: {
                if (!playing)
                    d.scheduleRelease()
            }
        }
    }
}
