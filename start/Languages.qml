// Languages page: add or remove LibreOffice's language packs (menus, and a spelling dictionary where
// TDF has one) and pick the menu language. The work is done by hub.py on a thread, which replies with
// "language" events that Main.qml passes to finished().
import QtQuick 2.7
import QtQuick.Layouts 1.3
import Lomiri.Components 1.3
import Lomiri.Components.Popups 1.3

Page {
    id: page
    property var py: null
    property var info: null          // hub.languages()
    property string busy: ''         // the pack being added or removed
    property real fraction: 0
    property string message: ''
    property bool failed: false
    property string filter: ''

    header: PageHeader {
        id: header
        title: 'Languages'
    }

    function nameOf(code) {
        if (!info) return code
        for (var i = 0; i < info.items.length; ++i)
            if (info.items[i].code === code) return info.items[i].name
        return code
    }

    function sizeText(bytes) {
        return bytes < 1048576 ? Math.round(bytes / 1024) + ' KB' : (bytes / 1048576).toFixed(1) + ' MB'
    }

    function load() {
        py.call('hub.languages', [], function (result) {
            if (!page) return
            info = result
            rebuild()
        })
    }

    function rebuild() {
        languageModel.clear()
        if (!info) return
        var filter = page.filter.trim().toLowerCase()
        var groups = [[], [], []]
        for (var i = 0; i < info.items.length; ++i) {
            var item = info.items[i]
            if (filter && (item.name + ' ' + item.english + ' ' + item.code).toLowerCase().indexOf(filter) < 0)
                continue
            if (item.installed) groups[0].push(item)
            else if (item.code === info.phone) groups[1].push(item)
            else groups[2].push(item)
        }
        var titles = ['Added', 'Your phone\'s language', 'Available']
        for (var g = 0; g < groups.length; ++g)
            for (var j = 0; j < groups[g].length; ++j) {
                var entry = groups[g][j]
                languageModel.append({ code: entry.code, name: entry.name, english: entry.english,
                                       size: entry.size, dictionary: entry.dictionary,
                                       installed: entry.installed, group: titles[g] })
            }
    }

    function start(method, code, text) {
        busy = code
        fraction = 0
        failed = false
        message = text
        py.call('hub.' + method, [code])
    }

    // From Main.qml: the reply to add_language or remove_language.
    function finished(code, state, text) {
        busy = ''
        failed = state !== 'done'
        message = failed ? text : ''
        load()
    }

    function choose(code) {
        py.call('hub.set_ui_language', [code], function () {
            if (!page) return
            load()
        })
    }

    Component.onCompleted: load()

    ListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        model: ListModel { id: languageModel }
        section.property: 'group'
        section.delegate: Item {
            width: list.width
            height: units.gu(5)
            Label {
                anchors { left: parent.left; leftMargin: units.gu(2); bottom: parent.bottom; bottomMargin: units.gu(1) }
                text: section
                font.weight: Font.Medium
                color: '#1f2430'
            }
        }

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
                text: !page.info ? '' : 'Menus: ' + (page.info.ui ? page.nameOf(page.info.ui)
                      : page.info.phone ? 'the phone\'s language (' + page.nameOf(page.info.phone) + ') once it\'s added, otherwise English'
                      : 'English, like the phone')
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                wrapMode: Text.WordWrap
                color: '#6b7080'
                textSize: Label.Small
                text: 'A language adds LibreOffice\'s menus in that language and, for many languages, '
                      + 'a spelling dictionary. Packs come from The Document Foundation and are checked '
                      + 'against fixed hashes before they\'re added. Tap an added language to use it for '
                      + 'the menus. Changes show the next time LibreOffice starts.'
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.info && !page.info.office
                wrapMode: Text.WordWrap
                color: theme.palette.normal.negative
                text: 'LibreOffice isn\'t downloaded yet. Open Writer or another app first.'
            }
            Label {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.message !== ''
                wrapMode: Text.WordWrap
                color: page.failed ? theme.palette.normal.negative : '#3a3f4b'
                text: page.message
            }
            ProgressBar {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.busy !== ''
                minimumValue: 0
                maximumValue: 1
                indeterminate: page.fraction <= 0
                value: page.fraction
            }
            TextField {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                placeholderText: 'Search languages'
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextChanged: { page.filter = text; page.rebuild() }
            }
        }

        delegate: ListItem {
            height: layout.height + (divider.visible ? divider.height : 0)
            enabled: page.busy === '' && page.info && page.info.office
            onClicked: PopupUtils.open(model.installed ? addedDialog : addDialog, page,
                                       { code: model.code, name: model.name, size: model.size })
            ListItemLayout {
                id: layout
                title.text: model.name
                subtitle.text: model.english + ' · ' + page.sizeText(model.size)
                               + (model.dictionary ? ' · spelling' : '')
                Icon {
                    SlotsLayout.position: SlotsLayout.Trailing
                    width: units.gu(2.5)
                    height: width
                    name: 'tick'
                    color: '#1f7a3d'
                    visible: model.installed && page.info && page.info.ui === model.code
                }
                ActivityIndicator {
                    SlotsLayout.position: SlotsLayout.Trailing
                    running: page.busy === model.code
                    visible: running
                }
            }
        }
    }

    Component {
        id: addDialog
        Dialog {
            id: dialog
            property string code
            property string name
            property real size
            title: 'Add ' + name + '?'
            text: 'This downloads ' + page.sizeText(size) + ' from The Document Foundation.'
            Button {
                text: 'Add'
                color: theme.palette.normal.positive
                onClicked: {
                    page.start('add_language', dialog.code, 'Adding ' + dialog.name + '…')
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: 'Cancel'
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: addedDialog
        Dialog {
            id: dialog
            property string code
            property string name
            property real size
            readonly property bool inUse: page.info && page.info.ui === code
            title: name
            text: inUse ? 'LibreOffice\'s menus use this language.' : ''
            Button {
                text: dialog.inUse ? 'Use the phone\'s language' : 'Use for menus'
                color: theme.palette.normal.positive
                onClicked: {
                    page.choose(dialog.inUse ? '' : dialog.code)
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: 'Remove'
                color: theme.palette.normal.negative
                onClicked: {
                    page.start('remove_language', dialog.code, 'Removing ' + dialog.name + '…')
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
