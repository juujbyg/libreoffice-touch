import QtQuick 2.7
import QtQuick.Window 2.2
Window {
    visible: true
    color: 'white'
    Loader {
        id: loader
        anchors.fill: parent
        source: 'file:///opt/click.ubuntu.com/libreoffice.juujbyg/current/start/Main.qml'
    }
    Timer {
        interval: 7000; running: true
        onTriggered: loader.item.grabToImage(function (r) {
            r.saveToFile('/home/phablet/.cache/libreoffice.juujbyg/start.png'); Qt.quit()
        })
    }
}
