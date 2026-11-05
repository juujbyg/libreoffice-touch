// Root helper (tools/grab-start.sh style): renders the start screen's Languages page inside the
// app's sandbox and saves it with grabToImage.
import QtQuick 2.7
import QtQuick.Window 2.2
import Lomiri.Components 1.3
import io.thp.pyotherside 1.4
Window {
    visible: true
    width: 1080; height: 2085
    color: 'white'
    MainView {
        id: view
        anchors.fill: parent
        applicationName: 'libreoffice.juujbyg'
        backgroundColor: '#f5f3ef'
        PageStack { id: stack }
    }
    Python {
        id: py
        Component.onCompleted: {
            addImportPath('/opt/click.ubuntu.com/libreoffice.juujbyg/current/start')
            importModule('hub', function () {
                stack.push(Qt.resolvedUrl('file:///opt/click.ubuntu.com/libreoffice.juujbyg/current/start/Languages.qml'), { py: py })
            })
        }
        onError: console.log('hub: ' + traceback)
    }
    Timer {
        interval: 8000; running: true
        onTriggered: view.grabToImage(function (r) {
            r.saveToFile('/home/phablet/.cache/libreoffice.juujbyg/languages.png'); Qt.quit()
        })
    }
}
