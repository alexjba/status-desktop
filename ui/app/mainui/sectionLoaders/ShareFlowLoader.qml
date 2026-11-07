import QtQml
import QtQuick
import QtQuick.Controls

import StatusQ.Core
import StatusQ.Core.Utils as SQUtils

import AppLayouts.stores as AppStores
import AppLayouts.Chat.stores as ChatStores

import mainui

import utils

/**
  * External intake, share route: content shared to Status from another app
  * opens the one-screen destination picker; sending fans the payload out to
  * every picked destination and lands in the first one.
  * The dialog is created on the first launch and unloaded when it closes.
  * The image paths are app-private cached copies whose lifecycle ends with
  * the flow: released on cancel/replace, or by the image-send task after send.
  */
Loader {
    id: root

    // sectionsLoaded, chatSearchModel, setNavToMsgDetailsFlag, setActiveSectionChat,
    // releaseShareIntakeFiles
    required property AppStores.RootStore rootStore
    // sendSharedContent, share payment requests
    required property ChatStores.RootStore rootChatStore

    // The user's own 1-1 chat, never offered as a destination
    required property string excludedChatId

    property bool paymentRequestFeatureEnabled: false
    property bool areTestNetworksEnabled: false

    active: false

    // The share takes over the screen: open popups are dismissed, except the
    // picker itself (NoAutoClose). Last-wins: a new share while the picker is
    // open replaces the payload.
    function launch(text: string, imagePaths) {
        SystemUtils.tryCloseActivePopups()
        d.releaseImages()
        root.rootChatStore.clearSharePaymentRequests()
        d.sharedText = text
        d.sharedImagePaths = imagePaths
        if (!root.active) {
            root.setSource(Qt.resolvedUrl("ShareFlowDialog.qml"), {
                rootStore: root.rootStore,
                rootChatStore: root.rootChatStore,
                excludedChatId: Qt.binding(() => root.excludedChatId),
                paymentRequestFeatureEnabled: Qt.binding(() => root.paymentRequestFeatureEnabled),
                areTestNetworksEnabled: Qt.binding(() => root.areTestNetworksEnabled),
                sharedText: Qt.binding(() => d.sharedText),
                sharedImagePaths: Qt.binding(() => d.sharedImagePaths)
            })
            root.active = true
        } else
            root.item.restart()
        root.item.open()
    }

    QtObject {
        id: d

        property string sharedText
        property var sharedImagePaths: []

        // Nothing is sent; on Android the whole task is backgrounded so the
        // user lands back in the source app.
        function cancel() {
            releaseImages()
            root.rootChatStore.clearSharePaymentRequests()
            root.item.close()
            if (SQUtils.Utils.isAndroid)
                SystemUtils.moveAppTaskToBack()
        }

        // imagePaths is the attachment list as it left the composer: cached
        // copies detached there are released right away since no send will
        // ever consume them.
        function complete(destinations, text: string, imagePaths) {
            const cachedPaths = sharedImagePaths
            const removedCached = cachedPaths.filter(path => !imagePaths.includes(path))
            const keptCached = cachedPaths.filter(path => imagePaths.includes(path))
            sharedImagePaths = []
            if (removedCached.length > 0)
                root.rootStore.releaseShareIntakeFiles(removedCached)
            root.item.close()
            if (destinations.length > 0) {
                root.rootStore.setNavToMsgDetailsFlag(true)
                root.rootStore.setActiveSectionChat(destinations[0].sectionId, destinations[0].chatId)
            }
            if (!root.rootChatStore.sendSharedContent(destinations, text, imagePaths)) {
                if (keptCached.length > 0)
                    root.rootStore.releaseShareIntakeFiles(keptCached)
            }
        }

        function releaseImages() {
            if (sharedImagePaths.length === 0)
                return
            root.rootStore.releaseShareIntakeFiles(sharedImagePaths)
            sharedImagePaths = []
        }
    }

    Connections {
        target: root.item

        function onClosed() {
            root.active = false
        }
        function onSendRequested(destinations, text, imagePaths) {
            d.complete(destinations, text, imagePaths)
        }
        function onCancelRequested() {
            d.cancel()
        }
    }
}
