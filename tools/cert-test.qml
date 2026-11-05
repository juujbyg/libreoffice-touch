// Root helper for tools/cert-test.sh: drives the start screen's certificate backend through
// pyotherside inside the app's sandbox (as the real page does) and grabs the Certificates page.
// CERT_TEST_MODE=add: wrong password, then the right one, then the page is saved to certificates.png.
// CERT_TEST_MODE=remove: removes every certificate and checks the store is empty.
// Output lines start with "cert-test:".
import QtQuick 2.7
import QtQuick.Window 2.2
import Lomiri.Components 1.3
import io.thp.pyotherside 1.4
Window {
    id: win
    visible: true
    width: 1080; height: 2085
    color: 'white'
    property string dir: '/home/phablet/.cache/libreoffice.juujbyg/certtest'
    property var steps: []
    function log(text) { console.log('cert-test: ' + text) }
    function next() {
        if (steps.length === 0) { Qt.quit(); return }
        steps.shift()()
    }
    MainView {
        id: view
        anchors.fill: parent
        applicationName: 'libreoffice.juujbyg'
        backgroundColor: '#f5f3ef'
        PageStack { id: stack }
    }
    Python {
        id: py
        property var waiting: null
        Component.onCompleted: {
            addImportPath('/opt/click.ubuntu.com/libreoffice.juujbyg/current/start')
            setHandler('certificate', function (state, message) {
                log('event ' + state + ': ' + message)
                if (waiting) { var w = waiting; waiting = null; w(state) }
            })
            importModule('os', function () {})
            importModule('hub', function () {
                call('os.getenv', ['CERT_TEST_MODE'], function (mode) {
                    if (mode === 'add') {
                        steps = [
                            function () { call('hub.certificates', [], function (r) { log('before: ' + r.items.length + ' running=' + r.running + ' error=' + r.error); next() }) },
                            function () { waiting = function (s) { log(s === 'failed' ? 'ok wrong password refused' : 'FAIL wrong password accepted'); next() }
                                          call('hub.add_certificate', ['file://' + dir + '/test.p12', 'nope']) },
                            function () { waiting = function (s) { log(s === 'done' ? 'ok added' : 'FAIL not added'); next() }
                                          call('hub.add_certificate', ['file://' + dir + '/test.p12', 'pässword 1']) },
                            function () { call('hub.certificates', [], function (r) {
                                              for (var i = 0; i < r.items.length; ++i) log('cert ' + r.items[i].name + ' | ' + r.items[i].subject + ' | until ' + r.items[i].validUntil)
                                              next() }) },
                            function () { stack.push(Qt.resolvedUrl('file:///opt/click.ubuntu.com/libreoffice.juujbyg/current/start/Certificates.qml'), { py: py })
                                          grab.start() }
                        ]
                    } else {
                        steps = [
                            function () { call('hub.certificates', [], function (r) {
                                              var left = r.items.length
                                              if (left === 0) { log('nothing to remove'); next(); return }
                                              for (var i = 0; i < r.items.length; ++i)
                                                  call('hub.remove_certificate', [r.items[i].id], function (error) {
                                                      log('remove: ' + (error || 'ok'))
                                                      if (--left === 0) next()
                                                  })
                                          }) },
                            function () { call('hub.certificates', [], function (r) { log('after: ' + r.items.length); next() }) }
                        ]
                    }
                    next()
                })
            })
        }
        onError: log('hub error: ' + traceback)
    }
    Timer {
        id: grab
        interval: 6000
        onTriggered: view.grabToImage(function (r) {
            r.saveToFile(win.dir + '/certificates.png'); log('grabbed'); next()
        })
    }
    Timer { interval: 90000; running: true; onTriggered: { log('FAIL timeout'); Qt.quit() } }
}
