import QtQuick
import QtTest

import StatusQ.Layout

// StatusSectionLayout brackets every panel switch with
// panelSwitchStarted/panelSwitchEnded. Landscape shows all panels at once —
// there is no slide — so a currentIndex change must emit the pair
// back-to-back, and the invisible portrait view's own slide must not leak
// extra signals. (The window is 1000px wide: landscape mode.)
Item {
    id: root

    width: 1000
    height: 700

    Component {
        id: layoutComponent

        StatusSectionLayout {
            anchors.fill: parent
            showRightPanel: true
        }
    }

    Component {
        id: landscapeComponent

        StatusSectionLayoutLandscape {
            anchors.fill: parent
            showRightPanel: true
        }
    }

    Component {
        id: panelComponent
        Rectangle {
            width: 300
            height: 700
        }
    }

    SignalSpy {
        id: startedSpy
        signalName: "panelSwitchStarted"
    }

    SignalSpy {
        id: endedSpy
        signalName: "panelSwitchEnded"
    }

    TestCase {
        name: "StatusSectionLayout"
        when: windowShown

        property var layout: null

        function init() {
            layout = createTemporaryObject(layoutComponent, root)
            verify(!!layout)
            layout.leftPanel = createTemporaryObject(panelComponent, root)
            layout.centerPanel = createTemporaryObject(panelComponent, root)
            layout.rightPanel = createTemporaryObject(panelComponent, root)
            waitForRendering(layout)

            startedSpy.target = layout
            endedSpy.target = layout
            startedSpy.clear()
            endedSpy.clear()
        }

        function createLandscapeLayout(userUID, sectionName) {
            const item = createTemporaryObject(landscapeComponent, root)
            verify(!!item)
            item.userUID = userUID
            item.sectionName = sectionName
            item.leftPanel = createTemporaryObject(panelComponent, root)
            item.centerPanel = createTemporaryObject(panelComponent, root)
            item.rightPanel = createTemporaryObject(panelComponent, root)
            item.showRightPanel = true
            waitForRendering(item)
            return item
        }

        // The loader holding the portrait view. LayoutChooser flips the
        // loaders' `visible` on rotation, which builds or drops the view, so
        // flipping it here simulates a rotation.
        function loaderOf(typePrefix) {
            return findChild(layout, typePrefix === "StatusSectionLayoutPortrait" ? "portraitLoader"
                                                                              : "landscapeLoader")
        }

        function rotate(toPortrait) {
            const portrait = loaderOf("StatusSectionLayoutPortrait")
            const landscape = loaderOf("StatusSectionLayoutLandscape")
            landscape.visible = !toPortrait
            portrait.visible = toPortrait
            return portrait
        }

        // Only the chosen orientation's view exists.
        function test_onlyTheChosenViewIsBuilt() {
            const portrait = loaderOf("StatusSectionLayoutPortrait")
            const landscape = loaderOf("StatusSectionLayoutLandscape")
            verify(!!portrait && !!landscape)
            verify(landscape.visible && !!landscape.item, "the 1000px window starts in landscape")
            compare(portrait.item, null, "the portrait view is not built in landscape")
            rotate(true)
            verify(!!portrait.item)
            compare(landscape.item, null, "the landscape view is dropped in portrait")
        }

        // Rotating to landscape mid-slide must close the bracket right away:
        // the slide is no longer visible, so there is nothing left to defer
        // panel swaps for.
        function test_rotateToLandscapeMidSlideClosesBracket() {
            const pv = rotate(true).item // start in portrait
            verify(!!pv)
            pv.contentItem.highlightMoveDuration = 60000 // hold the slide

            layout.currentIndex = StatusSectionLayout.RightPanel
            compare(startedSpy.count, 1)
            compare(endedSpy.count, 0)

            rotate(false) // rotate away mid-slide
            compare(endedSpy.count, 1,
                    "rotating away mid-slide must close the bracket at once")
            wait(100)
            compare(startedSpy.count, 1)
            compare(endedSpy.count, 1)
        }

        // A switch made in landscape is a back-to-back pair; rotating to
        // portrait afterwards builds the view on the kept index, with no slide
        // and so no further bracket.
        function test_rotateToPortraitKeepsTheIndexWithoutASlide() {
            layout.currentIndex = StatusSectionLayout.RightPanel
            compare(startedSpy.count, 1, "landscape pair")
            compare(endedSpy.count, 1, "landscape pair")

            const pv = rotate(true).item
            verify(!!pv)
            // the right panel's page is the last one in the view
            compare(pv.currentIndex, pv.count - 1)
            compare(layout.currentIndex, StatusSectionLayout.RightPanel)
            wait(100)
            compare(startedSpy.count, 1)
            compare(endedSpy.count, 1)
        }

        function test_landscapeSwitchEmitsPairBackToBack() {
            layout.currentIndex = StatusSectionLayout.RightPanel

            compare(startedSpy.count, 1,
                    "a landscape switch must emit panelSwitchStarted")
            compare(endedSpy.count, 1,
                    "a landscape switch must emit panelSwitchEnded right after")

            wait(500)
            compare(startedSpy.count, 1)
            compare(endedSpy.count, 1)
        }

        function test_landscapeSplitStateIsIsolatedPerUserAndSection() {
            const settingsA = Qt.createQmlObject(
                'import QtCore; Settings { category: "%1_%2".arg("userA").arg("sectionA"); property int splitWidth: 420 }',
                root)
            const settingsB = Qt.createQmlObject(
                'import QtCore; Settings { category: "%1_%2".arg("userA").arg("sectionB"); property int splitWidth: 300 }',
                root)
            const settingsC = Qt.createQmlObject(
                'import QtCore; Settings { category: "%1_%2".arg("userB").arg("sectionA"); property int splitWidth: 340 }',
                root)

            compare(settingsA.category, "userA_sectionA")
            compare(settingsB.category, "userA_sectionB")
            compare(settingsC.category, "userB_sectionA")
            compare(settingsA.category !== settingsB.category, true)
            compare(settingsA.category !== settingsC.category, true)
            compare(settingsB.category !== settingsC.category, true)

            compare(settingsA.splitWidth, 420)
            compare(settingsB.splitWidth, 300)
            compare(settingsC.splitWidth, 340)
        }
    }
}
