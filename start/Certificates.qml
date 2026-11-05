// Certificates page: the certificates LibreOffice signs documents with (File > Digital Signatures,
// and signed PDF export). A certificate comes as a .p12 or .pfx file with a password; it's picked
// from another app (Files, the browser's downloads) through Content Hub and imported by hub.py
// (start/certs.py) into the app's own certificate store. The file itself isn't kept.
import QtQuick 2.7
import QtQuick.Layouts 1.3
import Lomiri.Components 1.3
import Lomiri.Components.Popups 1.3
import Lomiri.Content 1.3

Page {
    id: page
    property var py: null
    property var info: null          // hub.certificates()
    property var pending: null       // the transfer holding the file being added
    property bool busy: false
    property string message: ''
    property bool failed: false

    header: PageHeader {
        id: header
        title: 'Signing certificates'
        trailingActionBar.actions: [
            Action {
                iconName: 'add'
                text: 'Add a certificate'
                enabled: !page.busy
                onTriggered: picker.visible = true
            }
        ]
    }

    function load() {
        py.call('hub.certificates', [], function (result) {
            if (!page) return
            info = result
            certificateModel.clear()
            for (var i = 0; i < result.items.length; ++i) certificateModel.append(result.items[i])
        })
    }

    function dateText(seconds) {
        return Qt.formatDate(new Date(seconds * 1000), 'd MMM yyyy')
    }

    // A file to add: from the picker below, or shared to LibreOffice from another app (Main.qml).
    function offer(transfer) {
        if (pending) pending.finalize()
        pending = transfer
        var url = String(transfer.items[0].url)
        PopupUtils.open(passwordDialog, page, { fileName: decodeURIComponent(url.replace(/^.*\//, '')) })
    }

    function drop() {
        if (pending) pending.finalize()   // deletes the app's incoming copy of the file
        pending = null
    }

    function add(password) {
        busy = true
        failed = false
        message = 'Adding the certificate…'
        py.call('hub.add_certificate', [String(pending.items[0].url), password])
    }

    // From Main.qml: the reply to add_certificate.
    function finished(state, text) {
        drop()
        busy = false
        failed = state !== 'done'
        message = failed ? text
                : (text ? 'Added: ' + text + '.' : 'That certificate was already here.')
        load()
    }

    Component.onCompleted: load()
    Component.onDestruction: drop()

    ListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        model: ListModel { id: certificateModel }

        header: Column {
            width: list.width
            spacing: units.gu(1.5)
            topPadding: units.gu(2)
            bottomPadding: units.gu(1)

            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                wrapMode: Text.WordWrap
                color: '#1f2430'
                text: 'Sign documents with your own certificate: a .p12 or .pfx file and its password, '
                      + 'such as one from your national ID, your bank or your employer. Tap + to add one '
                      + 'from Files or your downloads.'
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                wrapMode: Text.WordWrap
                color: '#6b7080'
                textSize: Label.Small
                text: 'To sign in LibreOffice: File ▸ Digital Signatures ▸ Digital Signatures…, then Sign '
                      + 'Document. For a signed PDF: File ▸ Export as PDF, Digital Signatures tab. '
                      + 'The certificate and its private key are kept in the app\'s own folder, without '
                      + 'a password of their own, like in a web browser. The file you add isn\'t kept.'
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: !!page.info && page.info.running
                wrapMode: Text.WordWrap
                color: '#3a3f4b'
                text: 'LibreOffice is open: close it and open it again to sign with a certificate added now.'
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.message !== '' || (!!page.info && page.info.error !== '')
                wrapMode: Text.WordWrap
                color: page.failed || (page.info && page.info.error) ? theme.palette.normal.negative : '#1f7a3d'
                text: page.message || (page.info ? page.info.error : '')
            }
            ProgressBar {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.busy
                indeterminate: true
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: !!page.info && certificateModel.count === 0
                wrapMode: Text.WordWrap
                color: '#6b7080'
                text: 'No certificates yet.'
            }
        }

        delegate: ListItem {
            height: layout.height + (divider.visible ? divider.height : 0)
            enabled: !page.busy
            readonly property bool expired: model.validUntil * 1000 < Date.now()
            onClicked: PopupUtils.open(detailsDialog, page, { cert: certificateModel.get(index) })
            ListItemLayout {
                id: layout
                title.text: model.name
                subtitle.text: (model.selfSigned ? 'Self-signed' : 'Issued by ' + model.issuer)
                subtitle.wrapMode: Text.WordWrap
                summary.text: (expired ? 'Expired on ' : 'Valid until ') + page.dateText(model.validUntil)
                summary.color: expired ? theme.palette.normal.negative : theme.palette.normal.backgroundTertiaryText
                Icon {
                    SlotsLayout.position: SlotsLayout.Leading
                    width: units.gu(3.5)
                    height: width
                    name: 'stock_key'
                    color: '#1f2430'
                }
            }
        }
    }

    ContentPeerPicker {
        id: picker
        anchors.fill: parent
        visible: false
        z: 50
        headerText: 'Pick a .p12 or .pfx certificate'
        contentType: ContentType.All
        handler: ContentHandler.Source
        onPeerSelected: {
            visible = false
            peer.selectionType = ContentTransfer.Single
            page.picking = peer.request()
        }
        onCancelPressed: visible = false
    }
    property var picking: null
    Connections {
        target: page.picking
        onStateChanged: {
            var t = page.picking
            if (!t) return
            if (t.state === ContentTransfer.Charged) { page.picking = null; page.offer(t) }
            else if (t.state === ContentTransfer.Aborted) page.picking = null
        }
    }

    Component {
        id: passwordDialog
        Dialog {
            id: dialog
            property string fileName
            readonly property bool looksRight: /\.(p12|pfx)$/i.test(fileName)
            title: 'Add a certificate'
            text: looksRight ? fileName
                  : fileName + '\n\nThis doesn\'t look like a certificate file (.p12 or .pfx), but you can try.'
            TextField {
                id: password
                placeholderText: 'The file\'s password'
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Component.onCompleted: forceActiveFocus()
                onAccepted: addButton.clicked()
            }
            Button {
                id: addButton
                text: 'Add'
                color: theme.palette.normal.positive
                onClicked: {
                    var value = password.text
                    password.text = ''
                    PopupUtils.close(dialog)
                    page.add(value)
                }
            }
            Button {
                text: 'Cancel'
                onClicked: {
                    password.text = ''
                    PopupUtils.close(dialog)
                    page.drop()
                }
            }
        }
    }

    Component {
        id: detailsDialog
        Dialog {
            id: dialog
            property var cert
            title: cert.name
            text: [cert.organization, cert.email].filter(function (x) { return x }).join('\n')
                  + '\n\n' + (cert.selfSigned ? 'Self-signed' : 'Issued by ' + cert.issuer)
                  + '\nValid ' + page.dateText(cert.validFrom) + ' – ' + page.dateText(cert.validUntil)
                  + '\n\nSHA-256 fingerprint:\n' + cert.id.match(/../g).join(':').toUpperCase()
            Button {
                text: 'Remove'
                color: theme.palette.normal.negative
                onClicked: {
                    var id = dialog.cert.id
                    PopupUtils.close(dialog)
                    PopupUtils.open(confirmRemove, page, { certId: id, name: dialog.cert.name })
                }
            }
            Button {
                text: 'Close'
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: confirmRemove
        Dialog {
            id: dialog
            property string certId
            property string name
            title: 'Remove ' + name + '?'
            text: 'Its private key is deleted from the phone too. You can add it again from the .p12 or .pfx file.'
            Button {
                text: 'Remove'
                color: theme.palette.normal.negative
                onClicked: {
                    py.call('hub.remove_certificate', [dialog.certId], function (error) {
                        if (!page) return
                        page.failed = error !== ''
                        page.message = error || 'Removed.'
                        page.load()
                    })
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: 'Cancel'
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
