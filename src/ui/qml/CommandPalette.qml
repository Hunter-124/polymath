import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// CommandPalette — the Ctrl+K quick switcher.  Type-to-filter over every page,
// Settings sub-section and a handful of one-shot actions; Up/Down + Enter to
// run, Esc to dismiss.  Everything routes through Nav / the `app` facade, so
// the palette needs no reference into Main.qml.
Popup {
    id: palette
    width: 560
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Style.overlay }

    background: Rectangle {
        radius: Style.radiusLg
        color: Style.surface
        border.width: 1
        border.color: Style.border
    }

    property var  commands: []
    property var  filtered: []
    property int  highlightIndex: 0

    function buildCommands() {
        var c = []
        const pages = [
            ["Dashboard", "home",   "Ctrl+1"], ["Chat",     "chat",  "Ctrl+2"],
            ["Cameras",   "camera", "Ctrl+3"], ["Tasks",    "tasks", "Ctrl+4"],
            ["Timeline",  "clock",  "Ctrl+5"], ["Shopping", "cart",  "Ctrl+6"],
            ["Lab",       "flask",  "Ctrl+7"], ["Settings", "settings", "Ctrl+8"]
        ]
        for (let p of pages)
            c.push({ label: "Go to " + p[0], icon: p[1], hint: p[2],
                     run: () => Nav.navigate(p[0]) })
        const sections = [
            ["Personalities", "person"], ["Models", "chip"],
            ["Privacy", "shield"], ["Mobile Access", "phone"]
        ]
        for (let s of sections)
            c.push({ label: "Settings · " + s[0], icon: s[1], hint: "",
                     run: () => Nav.goSettings(s[0]) })
        c.push({ label: "Clear conversation", icon: "trash", hint: "",
                 run: () => { app.chatModel.clear(); Nav.notify("good", "Chat", "Conversation cleared") } })
        c.push({ label: "Refresh all data", icon: "refresh", hint: "",
                 run: () => { app.refreshAll(); Nav.notify("good", "Data", "All lists refreshed") } })
        c.push({ label: "Open models folder", icon: "folder", hint: "",
                 run: () => app.openModelsFolder() })
        return c
    }

    // startsWith beats contains beats in-order subsequence; everything else drops.
    function score(q, label) {
        const l = label.toLowerCase()
        if (q.length === 0) return 1
        if (l.startsWith(q)) return 100
        const at = l.indexOf(q)
        if (at >= 0) return 60 - Math.min(at, 40)
        var i = 0
        for (var j = 0; j < l.length && i < q.length; ++j)
            if (l[j] === q[i]) ++i
        return i === q.length ? 10 : -1
    }

    function refilter() {
        const q = field.text.trim().toLowerCase()
        var out = []
        for (let cmd of commands) {
            const s = score(q, cmd.label)
            if (s >= 0) out.push({ cmd: cmd, s: s })
        }
        out.sort((a, b) => b.s - a.s)
        filtered = out.map(e => e.cmd)
        highlightIndex = 0
    }

    function runHighlighted() {
        if (highlightIndex < 0 || highlightIndex >= filtered.length) return
        const cmd = filtered[highlightIndex]
        palette.close()
        cmd.run()
    }

    onOpened: {
        commands = buildCommands()
        field.text = ""
        refilter()
        field.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 8
            PmIcon { width: 16; height: 16; name: "search"; color: Style.textFaint; Layout.leftMargin: 4 }
            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: "Type a page or command…"
                color: Style.text
                placeholderTextColor: Style.textFaint
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
                background: null
                onTextChanged: palette.refilter()
                Keys.onDownPressed: palette.highlightIndex =
                    Math.min(palette.highlightIndex + 1, palette.filtered.length - 1)
                Keys.onUpPressed: palette.highlightIndex = Math.max(palette.highlightIndex - 1, 0)
                onAccepted: palette.runHighlighted()
            }
            Rectangle {
                radius: 4; color: Style.surface2; border.width: 1; border.color: Style.border
                implicitWidth: escLbl.implicitWidth + 10; implicitHeight: 18
                Label {
                    id: escLbl; anchors.centerIn: parent
                    text: "Esc"; color: Style.textFaint
                    font.family: Style.fontFamily; font.pixelSize: 10
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Style.border }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            Layout.margins: 6
            clip: true
            model: palette.filtered
            currentIndex: palette.highlightIndex
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: AbstractButton {
                id: row
                required property var modelData
                required property int index
                width: results.width
                height: 36
                onClicked: { palette.highlightIndex = index; palette.runHighlighted() }
                HoverHandler { onHoveredChanged: if (hovered) palette.highlightIndex = row.index }

                background: Rectangle {
                    radius: Style.radiusXs
                    color: palette.highlightIndex === row.index ? Style.surface3 : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    PmIcon {
                        Layout.leftMargin: 8
                        width: 15; height: 15
                        name: row.modelData.icon
                        color: palette.highlightIndex === row.index ? Style.accent : Style.textDim
                    }
                    Label {
                        text: row.modelData.label
                        color: palette.highlightIndex === row.index ? Style.text : Style.textDim
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: row.modelData.hint.length > 0
                        text: row.modelData.hint
                        color: Style.textFaint
                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                        Layout.rightMargin: 10
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: palette.filtered.length === 0
                text: "No matches"
                color: Style.textFaint
                font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            }
        }
    }
}
