import QtQuick

// common contract for StatusSectionLayout loaders
Loader {
    id: root

    required property string userUID
    required property string sectionName

    property int leftPanelWidthOverride: 0

    onLoaded: {
        const name = root.sectionName
        console.info("[section-loaded]", name, Date.now())
        const w = root.Window.window
        if (!w)
            return
        const onFrame = () => {
            w.frameSwapped.disconnect(onFrame)
            console.info("[section-frame]", name, Date.now())
        }
        w.frameSwapped.connect(onFrame)
    }
}
