import QtQml
import QtQuick
import QtQuick.Controls

import StatusQ.Core
import StatusQ.Popups.Dialog

import AppLayouts.stores as AppStores
import AppLayouts.Chat.stores as ChatStores

import mainui
import mainui.adaptors

import utils

// The share destination picker dialog of ShareFlowLoader, which loads it by URL
// so it stays out of AppMain's type closure until the first share.

StatusDialog {
    id: shareFlowPopup

    required property AppStores.RootStore rootStore
    required property ChatStores.RootStore rootChatStore
    required property string excludedChatId
    property bool paymentRequestFeatureEnabled: false
    property bool areTestNetworksEnabled: false

    property string sharedText
    property var sharedImagePaths: []

    signal sendRequested(var destinations, string text, var imagePaths)
    signal cancelRequested

    // Portrait/mobile: the dialog's own bottom-sheet handling makes it
    // fullscreen; otherwise ~480x640 centered (the content's implicit
    // height, capped by the dialog to 80% of the window).
    width: 480
    fullScreenSheet: true
    closePolicy: Popup.NoAutoClose
    standardButtons: Dialog.NoButton
    padding: 0

    // The picker carries its own header row, so the fullscreen sheet is
    // header-less: keep that row out of the notch/status-bar area
    // (StatusDialog only safe-area-pads the bottom).
    Binding on topPadding {
        when: shareFlowPopup.bottomSheet
        value: shareFlowPopup.parent.SafeArea.margins.top
    }

    function restart() {
        sharePicker.reset()
    }

    // Only once the sections are loaded: the chat search model builds
    // itself on the first rowCount, so wiring it before the bulk chat
    // load would pull an empty build.
    RecentPostableDestinationsAdaptor {
        id: shareDestinationsAdaptor
        sourceModel: shareFlowPopup.rootStore.sectionsLoaded ? shareFlowPopup.rootStore.chatSearchModel : null
        excludedChatId: shareFlowPopup.excludedChatId
    }

    contentItem: ShareDestinationPickerPanel {
        id: sharePicker
        objectName: "shareFlowPicker"

        implicitHeight: 640 - shareFlowPopup.topPadding - shareFlowPopup.bottomPadding

        model: shareDestinationsAdaptor.model
        text: shareFlowPopup.sharedText
        imagePaths: shareFlowPopup.sharedImagePaths
        keyboardVisible: SystemUtils.androidKeyboardVisible || SystemUtils.iosKeyboardVisible
                         || Qt.inputMethod.visible
        paymentRequestModel: shareFlowPopup.rootChatStore?.sharePaymentRequestModel ?? null
        formatBalance: (amount, tokenKey) => shareFlowPopup.rootChatStore.formatTokenAmount(amount, tokenKey)
        paymentRequestFeatureEnabled: shareFlowPopup.paymentRequestFeatureEnabled
        areTestNetworksEnabled: shareFlowPopup.areTestNetworksEnabled

        onPaymentRequestModalRequested: callback => Global.openPaymentRequestModalRequested(
                                            (receiver, amount, tokenKey, symbol, logoUri) =>
                                            shareFlowPopup.rootChatStore.addSharePaymentRequest(
                                                receiver, amount, tokenKey, symbol, logoUri),
                                            callback)
        onPaymentRequestRemoveRequested: index => shareFlowPopup.rootChatStore.removeSharePaymentRequest(index)
        onSendRequested: (destinations, text, imagePaths) => shareFlowPopup.sendRequested(destinations, text, imagePaths)
        onCancelRequested: shareFlowPopup.cancelRequested()
    }
}
