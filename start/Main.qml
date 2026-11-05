// The LibreOffice start screen: new documents, the documents kept in the app's folder, and the way
// files get in and out of the app sandbox (Content Hub: open from other apps, share to LibreOffice,
// send a document to another app).
import QtQuick 2.7
import QtQuick.Layouts 1.3
import Lomiri.Components 1.3
import Lomiri.Components.Popups 1.3
import Lomiri.Content 1.3
import io.thp.pyotherside 1.4

MainView {
    id: root
    applicationName: 'libreoffice.juujbyg'
    automaticOrientation: true
    width: units.gu(45)
    height: units.gu(75)
    backgroundColor: '#f5f3ef'

    property var incoming: null      // a transfer from another app that isn't ready yet
    property var picking: null       // a transfer started by "Open from…"
    property var exportTo: null      // another app asked to pick a document from LibreOffice
    property string pendingDelete: ''
    property var languagesPage: null // the Languages page, while it's open
    property bool ready: false

    readonly property var apps: [
        { kind: 'writer', label: 'Writer' }, { kind: 'calc', label: 'Calc' },
        { kind: 'impress', label: 'Impress' }, { kind: 'draw', label: 'Draw' },
        { kind: 'math', label: 'Math' }, { kind: 'base', label: 'Base' }
    ]

    function refresh() {
        if (!ready) return
        py.call('hub.documents', [], function (list) {
            documentModel.clear()
            for (var i = 0; i < list.length; ++i) documentModel.append(list[i])
        })
    }

    // Files handed over by another app: copy them into the app's folder and open the first one.
    function take(transfer) {
        var urls = []
        for (var i = 0; i < transfer.items.length; ++i) urls.push(String(transfer.items[i].url))
        py.call('hub.take', [urls], function (paths) {
            transfer.finalize()   // only now: it removes the app's incoming copy
            refresh()
            if (paths.length > 0) root.openDocument(paths[0])
        })
    }

    function receive(transfer) {
        if (transfer.state === ContentTransfer.Charged) take(transfer)
        else incoming = transfer
    }

    // Opening goes through hub.py, which answers with an address when LibreOffice has to be
    // started as its own app (see hub._open).
    function openDocument(path) {
        py.call('hub.open_document', [path], function (url) { if (url) Qt.openUrlExternally(url) })
    }
    function newDocument(kind) {
        py.call('hub.new_document', [kind], function (url) { if (url) Qt.openUrlExternally(url) })
    }

    function sizeText(bytes) {
        if (bytes < 1024) return bytes + ' B'
        if (bytes < 1048576) return Math.round(bytes / 1024) + ' KB'
        return (bytes / 1048576).toFixed(1) + ' MB'
    }

    Python {
        id: py
        Component.onCompleted: {
            addImportPath(Qt.resolvedUrl('.'))
            setHandler('language', function (code, state, message) {
                if (root.languagesPage) root.languagesPage.finished(code, state, message)
            })
            setHandler('progress', function (step, fraction) {
                if (root.languagesPage && step === 'language') root.languagesPage.fraction = fraction
            })
            importModule('hub', function () { root.ready = true; root.refresh() })
        }
        onError: console.log('hub: ' + traceback)
    }

    Connections {
        target: ContentHub
        onImportRequested: root.receive(transfer)
        onShareRequested: root.receive(transfer)
        onExportRequested: root.exportTo = transfer
    }
    Connections {
        target: root.incoming
        onStateChanged: {
            if (root.incoming && root.incoming.state === ContentTransfer.Charged) {
                var t = root.incoming
                root.incoming = null
                root.take(t)
            }
        }
    }
    Connections {
        target: root.picking
        onStateChanged: {
            var t = root.picking
            if (!t) return
            if (t.state === ContentTransfer.Charged) { root.picking = null; root.take(t) }
            else if (t.state === ContentTransfer.Aborted) root.picking = null
        }
    }
    // Back from LibreOffice: show what it saved.
    Connections {
        target: Qt.application
        onStateChanged: if (Qt.application.state === Qt.ApplicationActive) root.refresh()
    }

    Component { id: contentItem; ContentItem {} }

    PageStack {
        id: stack
        Component.onCompleted: push(mainPage)
    }

    Page {
        id: mainPage
        visible: false
        header: PageHeader {
            id: header
            title: root.exportTo ? 'Choose a document to send' : 'LibreOffice'
            trailingActionBar.actions: [
                Action {
                    iconName: 'document-open'
                    text: 'Open from…'
                    visible: !root.exportTo
                    onTriggered: openPicker.visible = true
                },
                Action {
                    iconName: 'language-chooser'
                    text: 'Languages'
                    visible: !root.exportTo
                    onTriggered: root.languagesPage = stack.push(Qt.resolvedUrl('Languages.qml'), { py: py })
                },
                Action {
                    iconName: 'close'
                    text: 'Cancel'
                    visible: !!root.exportTo
                    onTriggered: { root.exportTo.state = ContentTransfer.Aborted; root.exportTo = null }
                }
            ]
        }

        ListView {
            id: list
            anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            clip: true
            model: ListModel { id: documentModel }

            header: Column {
                width: list.width
                visible: !root.exportTo
                height: visible ? implicitHeight : 0
                spacing: units.gu(1)
                topPadding: units.gu(2)

                Label {
                    x: units.gu(2)
                    text: 'New'
                    font.weight: Font.Medium
                    color: '#1f2430'
                }
                Grid {
                    id: grid
                    x: units.gu(1)
                    width: parent.width - units.gu(2)
                    columns: 3
                    readonly property real cell: width / 3
                    Repeater {
                        model: root.apps
                        delegate: AbstractButton {
                            width: grid.cell
                            height: units.gu(12)
                            onClicked: root.newDocument(modelData.kind)
                            Column {
                                anchors.centerIn: parent
                                spacing: units.gu(0.5)
                                Image {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    source: '../icons/' + modelData.kind + '.svg'
                                    sourceSize.width: units.gu(7)
                                    sourceSize.height: units.gu(7)
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.label
                                    color: '#3a3f4b'
                                }
                            }
                        }
                    }
                }
                Label {
                    x: units.gu(2)
                    text: 'Documents'
                    font.weight: Font.Medium
                    color: '#1f2430'
                }
                Label {
                    x: units.gu(2)
                    width: parent.width - units.gu(4)
                    visible: documentModel.count === 0
                    wrapMode: Text.WordWrap
                    color: '#6b7080'
                    text: 'Documents you save in LibreOffice appear here, and so do files you share to '
                          + 'LibreOffice from other apps or open with "Open from…" above. '
                          + 'Swipe a document left to send it, save it to another app or delete it.'
                }
            }

            delegate: ListItem {
                height: layout.height + (divider.visible ? divider.height : 0)
                onClicked: {
                    if (root.exportTo) {
                        var t = root.exportTo
                        root.exportTo = null
                        t.items = [contentItem.createObject(root, { url: 'file://' + model.path })]
                        t.state = ContentTransfer.Charged
                    } else {
                        root.openDocument(model.path)
                    }
                }
                ListItemLayout {
                    id: layout
                    title.text: model.name
                    subtitle.text: root.sizeText(model.size) + ' · '
                                   + Qt.formatDateTime(new Date(model.modified * 1000), 'd MMM yyyy, hh:mm')
                    Image {
                        SlotsLayout.position: SlotsLayout.Leading
                        source: '../icons/' + (model.kind === 'other' ? 'main' : model.kind) + '.svg'
                        sourceSize.width: units.gu(5)
                        sourceSize.height: units.gu(5)
                    }
                }
                trailingActions: ListItemActions {
                    actions: [
                        Action {
                            iconName: 'share'
                            text: 'Send'
                            onTriggered: root.send(model.path, ContentHandler.Share)
                        },
                        Action {
                            iconName: 'save-to'
                            text: 'Save to…'
                            onTriggered: root.send(model.path, ContentHandler.Destination)
                        },
                        Action {
                            iconName: 'delete'
                            text: 'Delete'
                            onTriggered: { root.pendingDelete = model.path; PopupUtils.open(confirmDelete) }
                        }
                    ]
                }
            }
        }
    }

    // "Open from…": pick a document in another app (Files, Downloads…).
    ContentPeerPicker {
        id: openPicker
        anchors.fill: parent
        visible: false
        z: 50
        contentType: ContentType.Documents
        handler: ContentHandler.Source
        onPeerSelected: {
            visible = false
            peer.selectionType = ContentTransfer.Single
            root.picking = peer.request()
        }
        onCancelPressed: visible = false
    }

    // "Send" (sharing apps: mail, messaging, printing) and "Save to…" (apps that store files, such
    // as the file manager): give a saved document to another app.
    function send(path, handler) {
        sendPicker.path = path
        sendPicker.handler = handler
        sendPicker.visible = true
    }
    ContentPeerPicker {
        id: sendPicker
        property string path: ''
        anchors.fill: parent
        visible: false
        z: 50
        contentType: ContentType.Documents
        handler: ContentHandler.Share
        onPeerSelected: {
            visible = false
            var t = peer.request()
            t.items = [contentItem.createObject(root, { url: 'file://' + path })]
            t.state = ContentTransfer.Charged
        }
        onCancelPressed: visible = false
    }

    Component {
        id: confirmDelete
        Dialog {
            id: dialog
            title: 'Delete this document?'
            text: root.pendingDelete.replace(/^.*\//, '')
            Button {
                text: 'Delete'
                color: theme.palette.normal.negative
                onClicked: {
                    py.call('hub.delete_document', [root.pendingDelete], function () { root.refresh() })
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
