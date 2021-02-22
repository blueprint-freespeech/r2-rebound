import QtQuick 2.0
import QtQuick.Controls 1.0
import im.ricochet 1.0

Column {
    id: delegate
    width: parent.width

    Loader {
        active: {
            if (model.section === "offline")
                return true

            // either this is the first message, or the message was a long time ago..
            if ((model.timespan === -1 ||
                 model.timespan > 3600 /* one hour */))
                return true

            return false
        }

        sourceComponent: Label {
            //: %1 nickname
            text: {
                if (model.section === "offline")
                    return qsTr("%1 is offline").arg(contact !== null ? contact.nickname : "")
                else
                    return Qt.formatDateTime(model.timestamp, Qt.DefaultLocaleShortDate)
            }
            textFormat: Text.PlainText
            width: background.parent.width
            elide: Text.ElideRight
            horizontalAlignment: Qt.AlignHCenter
            color: palette.mid

            Rectangle {
                id: line
                width: (parent.width - parent.contentWidth) / 2 - 4
                height: 1
                y: (parent.height - 1) / 2
                color: Qt.lighter(palette.mid, 1.4)
            }

            Rectangle {
                width: line.width
                height: 1
                y: line.y
                x: parent.width - width
                color: line.color
            }
        }
    }

    Rectangle {
        id: background
        width: Math.max(30, message.width + 12)
        height: message.height + 12
        x: model.isOutgoing ? parent.width - width - 11 : 10

        property int __maxWidth: parent.width * 0.8

        color: (model.status === ConversationModel.Error) ? "#ffdcc4" : ( model.isOutgoing ? "#eaeced" : "#c4e7ff" )
        Behavior on color { ColorAnimation { } }

        Rectangle {
            rotation: 45
            width: 10
            height: 10
            x: model.isOutgoing ? parent.width - 20 : 10
            y: model.isOutgoing ? parent.height - 5 : -5
            color: parent.color
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            opacity: (model.status === ConversationModel.Sending || model.status === ConversationModel.Queued || model.status === ConversationModel.Error) ? 1 : 0
            visible: opacity > 0
            color: Qt.lighter(parent.color, 1.15)

            Behavior on opacity { NumberAnimation { } }
        }

        Rectangle
        {
            id: message

            property Item childItem: {
                if (model.type == "text")
                {
                    return textField;
                }
                else if (model.type =="transfer")
                {
                    return transferField;
                }
            }

            width: childItem.width
            height: childItem.height
            x: Math.round((background.width - width) / 2)
            y: 6

            color: "transparent"

            // text message
            TextEdit {
                id: textField
                visible: parent.childItem === this
                width: Math.min(implicitWidth, background.__maxWidth)
                height: contentHeight

                renderType: Text.NativeRendering
                textFormat: TextEdit.RichText
                selectionColor: palette.highlight
                selectedTextColor: palette.highlightedText
                font.pointSize: styleHelper.pointSize

                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                text: LinkedText.parsed(model.text)

                onLinkActivated: {
                    textField.deselect()
                    delegate.showContextMenu(link)
                }

                // Workaround an incomplete fix for QTBUG-31646
                Component.onCompleted: {
                    if (textField.hasOwnProperty('linkHovered'))
                        textField.linkHovered.connect(function() { })
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton

                    onClicked: delegate.showContextMenu(parent.hoveredLink)
                }
            }

            // sending file transfer
            Rectangle {
                id: transferField
                visible: parent.childItem === this
                width: 256
                height: filename.height + progressBar.height + transferStatus.height

                color: "transparent"

                Label {
                    id: filename
                    x: 0
                    y: 0
                    width: transferField.width - transferField.height - 6
                    height: styleHelper.pointSize * 2.5

                    text: model.transfer.file_name
                    font.bold: true
                    font.pointSize: styleHelper.pointSize
                }

                ProgressBar {
                    id: progressBar
                    anchors.top: filename.bottom
                    width: filename.width
                    height: 8

                    indeterminate: model.transfer.status === "pending"
                    value: model.transfer.progressPercent;
                }

                Label {
                    id: transferStatus
                    anchors.top: progressBar.bottom
                    anchors.topMargin: 6
                    width: transferField.height
                    height: styleHelper.pointSize * 2.5

                    text: model.transfer.status === "in progress" ? model.transfer.progressString : qsTr(model.transfer.status)
                    font.pointSize: filename.font.pointSize * 0.8;
                    color: Qt.lighter(filename.color, 1.5)
                }

                Button {
                    id: cancelButton
                    visible: (model.transfer.status === "pending" || model.transfer.status === "in progress")
                    anchors.right : transferField.right
                    width: visible ? transferField.height : 0
                    height: visible ? transferField.height : 0

                    text: "✕"

                    onClicked: {
                        contact.conversation.cancelAttachmentTransfer(model.transfer.id);
                    }
                }
            }
        }
    }

    function showContextMenu(link) {
        var object = contextMenu.createObject(delegate, (link !== undefined) ? { 'hoveredLink': link } : { })
        // XXX QtQuickControls private API. The only other option is 'visible', and it is not reliable. See PR#183
        object.popupVisibleChanged.connect(function() { if (!object.__popupVisible) object.destroy(1000) })
        object.popup()
    }

    Component {
        id: contextMenu

        Menu {
            property string hoveredLink: textField.hasOwnProperty('hoveredLink') ? textField.hoveredLink : ""
            MenuItem {
                text: linkAddContact.visible ? qsTr("Copy ID") : qsTr("Copy Link")
                visible: hoveredLink.length > 0
                onTriggered: LinkedText.copyToClipboard(hoveredLink)
            }
            MenuItem {
                text: qsTr("Open with Browser")
                visible: hoveredLink.length > 0 && hoveredLink.substr(0,4).toLowerCase() == "http"
                onTriggered: {
                    if (uiSettings.data.alwaysOpenBrowser || contact.settings.data.alwaysOpenBrowser) {
                        Qt.openUrlExternally(hoveredLink)
                    } else {
                        var window = uiMain.findParentWindow(delegate)
                        var object = createDialog("OpenBrowserDialog.qml", { 'link': hoveredLink, 'contact': contact }, window)
                        object.visible = true
                    }
                }
            }
            MenuItem {
                id: linkAddContact
                text: qsTr("Add as Contact")
                visible: hoveredLink.length > 0 && (hoveredLink.substr(0,9).toLowerCase() == "ricochet:"
                                                    || hoveredLink.substr(0,8).toLowerCase() == "torsion:")
                onTriggered: {
                    var object = createDialog("AddContactDialog.qml", { 'staticContactId': hoveredLink }, chatWindow)
                    object.visible = true
                }
            }
            MenuSeparator {
                visible: hoveredLink.length > 0
            }
            MenuItem {
                text: qsTr("Copy Message")
                visible: textField.selectedText.length == 0
                onTriggered: {
                    LinkedText.copyToClipboard(textField.getText(0, textField.length))
                }
            }
            MenuItem {
                text: qsTr("Copy Selection")
                visible: textField.selectedText.length > 0
                shortcut: "Ctrl+C"
                onTriggered: textField.copy()
            }
        }
    }
}
