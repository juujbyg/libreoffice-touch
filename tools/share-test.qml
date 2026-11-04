// Shares a document to LibreOffice through Content Hub, the way Document Viewer's Share does.
// Run under Document Viewer's profile and app id (tools/hub-test.sh), with the document in its cache.
import QtQuick 2.7
import Lomiri.Content 1.3

Item {
    property var transfer: null
    ContentPeerModel {
        id: peers
        contentType: ContentType.Documents
        handler: ContentHandler.Share
        onFindPeersCompleted: {
            for (var i = 0; i < peers.peers.length; ++i) {
                var peer = peers.peers[i]
                console.log('peer: ' + peer.appId)
                if (peer.appId.indexOf('libreoffice.juujbyg_libreoffice') === 0) {
                    transfer = peer.request()
                    transfer.items = [item.createObject(null, { url: Qt.resolvedUrl('Report.docx') })]
                    transfer.state = ContentTransfer.Charged
                    console.log('shared to ' + peer.appId)
                    return
                }
            }
            console.log('LibreOffice not offered as a share target')
        }
    }
    Component { id: item; ContentItem {} }
    Timer { interval: 15000; running: true; onTriggered: Qt.quit() }
}
