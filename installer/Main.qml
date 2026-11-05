// First-use screen: explains and runs the one-time download of LibreOffice from
// The Document Foundation. bin/office starts it when LibreOffice isn't installed yet,
// and starts LibreOffice itself once the files are in place.
import QtQuick 2.7
import QtQuick.Layouts 1.3
import Lomiri.Components 1.3
import io.thp.pyotherside 1.4

MainView {
    id: root
    applicationName: 'libreoffice.juujbyg'
    automaticOrientation: true
    width: units.gu(45)
    height: units.gu(75)
    backgroundColor: '#f5f3ef'

    // ask, working, failed, done
    property string stage: 'ask'
    property string step: 'download'
    property real fraction: 0
    property string failure: ''

    Python {
        id: py
        Component.onCompleted: {
            addImportPath(Qt.resolvedUrl('.'))
            importModule('installer', function () {})
        }
        onReceived: {
            var kind = data[0]
            if (kind === 'progress') {
                root.step = data[1]
                root.fraction = data[2]
            } else if (kind === 'done') {
                root.stage = 'done'
                // bin/office starts LibreOffice as soon as the files are in place. This screen
                // stays up until LibreOffice's window has had time to open, so the app never
                // has zero windows (Lomiri would close it).
                closeTimer.start()
            } else if (kind === 'failed') {
                root.failure = data[1]
                root.stage = 'failed'
            }
        }
        onError: {
            root.failure = traceback
            root.stage = 'failed'
        }
    }

    Timer { id: closeTimer; interval: 25000; onTriggered: Qt.quit() }

    function install() {
        root.fraction = 0
        root.step = 'download'
        root.stage = 'working'
        py.call('installer.start', [])
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.height + units.gu(8)
        clip: true

        ColumnLayout {
            id: column
            x: units.gu(3)
            y: units.gu(6)
            width: parent.width - units.gu(6)
            spacing: units.gu(2.5)

            Image {
                Layout.alignment: Qt.AlignHCenter
                source: '../icons/main.svg'
                sourceSize.width: units.gu(12)
                sourceSize.height: units.gu(12)
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.stage === 'done' ? 'Ready' : 'LibreOffice'
                textSize: Label.XLarge
                font.weight: Font.Medium
                color: '#1f2430'
            }

            Label {
                Layout.fillWidth: true
                visible: root.stage === 'ask'
                wrapMode: Text.WordWrap
                color: '#3a3f4b'
                text: 'Writer, Calc, Impress, Draw, Math and Base, as an unofficial '
                      + 'port for Ubuntu Touch.\n\n'
                      + 'LibreOffice itself isn\'t part of this download. On first use the app fetches '
                      + 'the official LibreOffice 26.8.2 for arm64 from The Document Foundation '
                      + '(downloadarchive.documentfoundation.org), checks it against a fixed '
                      + 'SHA-256 and unpacks it into the app\'s own folder.\n\n'
                      + 'Download: about 200 MB. Space used: about 730 MB. '
                      + 'Use Wi-Fi if your data plan is limited.'
            }

            Label {
                Layout.fillWidth: true
                visible: root.stage === 'working'
                wrapMode: Text.WordWrap
                color: '#3a3f4b'
                text: root.step === 'wait' ? 'Another window is installing LibreOffice. Waiting for it…'
                      : root.step === 'download'
                      ? 'Downloading LibreOffice… ' + Math.round(root.fraction * 100) + ' %'
                      : root.step === 'language'
                      ? 'Adding menus in your phone\'s language… ' + Math.round(root.fraction * 100) + ' %'
                      : root.step === 'language-failed'
                      ? 'Your phone\'s language couldn\'t be added. You can add it later from Languages.'
                      : 'Unpacking… ' + Math.round(root.fraction * 100) + ' %'
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: root.stage === 'working'
                minimumValue: 0
                maximumValue: 1
                value: root.fraction
            }

            Label {
                Layout.fillWidth: true
                visible: root.stage === 'working'
                wrapMode: Text.WordWrap
                color: '#6b7080'
                textSize: Label.Small
                text: 'Keep this screen open until it finishes. It takes a few minutes.'
            }

            Label {
                Layout.fillWidth: true
                visible: root.stage === 'failed'
                wrapMode: Text.WordWrap
                color: '#b3261e'
                text: root.failure
            }

            Label {
                Layout.fillWidth: true
                visible: root.stage === 'done'
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: '#3a3f4b'
                text: 'LibreOffice is installed. Opening it now…'
            }

            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: units.gu(6)
                visible: root.stage === 'ask' || root.stage === 'failed'
                text: root.stage === 'failed' ? 'Try again' : 'Download and install'
                color: '#f4a81d'
                onClicked: root.install()
            }

            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: units.gu(6)
                visible: root.stage !== 'done'
                text: root.stage === 'working' ? 'Cancel' : 'Not now'
                // Quitting stops the download; the half-done files are in a temporary folder
                // that the next attempt replaces.
                onClicked: Qt.quit()
            }
        }
    }
}
