import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// CommandPalette — glass modal + fuzzy scorer (02 §F2).
// Action registry lives in Main.qml (C1); this component only consumes `actions`.
Item {
    id: root
    property var actions: []
    property bool open: false
    property string query: ""
    property int selectedIndex: 0

    anchors.fill: parent
    visible: open
    z: 50
    focus: open

    // Filtered + ranked results (top 12), with section headers injected as {header:true,section}
    property var filtered: []

    function openPalette() {
        open = true
        query = ""
        selectedIndex = 0
        recompute()
        Qt.callLater(function () { queryField.forceActiveFocus() })
    }
    function close() {
        open = false
        query = ""
        selectedIndex = 0
    }

    // Subsequence fuzzy scorer: consecutive / word-boundary / prefix bonuses; -1 = no match
    function scoreMatch(text, q) {
        if (!q || q.length === 0) return 1
        if (!text) return -1
        var t = String(text).toLowerCase()
        var s = String(q).toLowerCase()
        var ti = 0
        var score = 0
        var consecutive = 0
        var first = true
        for (var si = 0; si < s.length; ++si) {
            var ch = s.charAt(si)
            var found = -1
            for (var j = ti; j < t.length; ++j) {
                if (t.charAt(j) === ch) { found = j; break }
            }
            if (found < 0) return -1
            // consecutive bonus
            if (found === ti) {
                consecutive += 1
                score += 4 + consecutive
            } else {
                consecutive = 0
                score += 1
            }
            // word-boundary / prefix
            if (found === 0 || t.charAt(found - 1) === " " || t.charAt(found - 1) === "."
                    || t.charAt(found - 1) === "/" || t.charAt(found - 1) === "-"
                    || t.charAt(found - 1) === "_") {
                score += 6
            }
            if (first && found === 0) score += 8
            first = false
            ti = found + 1
        }
        // Prefer shorter titles slightly
        score -= Math.min(t.length, 40) * 0.05
        return score
    }

    function recompute() {
        var scored = []
        var list = root.actions || []
        for (var i = 0; i < list.length; ++i) {
            var a = list[i]
            if (!a) continue
            var title = a.title || a.id || ""
            var section = a.section || ""
            var sc = scoreMatch(title + " " + section + " " + (a.id || ""), root.query)
            if (sc < 0) continue
            scored.push({ action: a, score: sc })
        }
        scored.sort(function (x, y) {
            if (y.score !== x.score) return y.score - x.score
            var tx = (x.action.title || "")
            var ty = (y.action.title || "")
            return tx < ty ? -1 : (tx > ty ? 1 : 0)
        })
        // Top 12, group by section for display
        var top = scored.slice(0, 12)
        var out = []
        var lastSection = null
        for (var k = 0; k < top.length; ++k) {
            var sec = top[k].action.section || "General"
            if (sec !== lastSection) {
                out.push({ header: true, section: sec, title: sec })
                lastSection = sec
            }
            out.push({
                header: false,
                section: sec,
                title: top[k].action.title || top[k].action.id,
                id: top[k].action.id || "",
                action: top[k].action,
                score: top[k].score
            })
        }
        filtered = out
        // Clamp selection to first non-header
        selectedIndex = firstSelectable(0)
    }

    function firstSelectable(from) {
        for (var i = from; i < filtered.length; ++i)
            if (!filtered[i].header) return i
        for (var j = 0; j < filtered.length; ++j)
            if (!filtered[j].header) return j
        return 0
    }
    function nextSelectable(dir) {
        if (filtered.length === 0) return 0
        var i = selectedIndex
        for (var n = 0; n < filtered.length; ++n) {
            i = (i + dir + filtered.length) % filtered.length
            if (!filtered[i].header) return i
        }
        return selectedIndex
    }
    function runSelected() {
        if (selectedIndex < 0 || selectedIndex >= filtered.length) return
        var row = filtered[selectedIndex]
        if (!row || row.header) return
        var act = row.action
        close()
        if (act && typeof act.run === "function")
            act.run()
    }

    onQueryChanged: recompute()
    onActionsChanged: if (open) recompute()
    onOpenChanged: if (open) recompute()

    // Scrim
    Rectangle {
        anchors.fill: parent
        color: Style.overlay
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    // Modal panel
    GlassSurface {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 72
        width: Math.min(560, parent.width - Style.pad * 2)
        height: Math.min(420, queryField.implicitHeight + listCol.implicitHeight + Style.pad * 2 + 8)
        radius: Style.radiusLg
        elevation: 3
        section: ""

        // Eat clicks so scrim doesn't close when interacting
        MouseArea {
            anchors.fill: parent
            onClicked: { /* swallow */ }
        }

        ColumnLayout {
            id: listCol
            anchors.fill: parent
            anchors.margins: Style.padSm
            spacing: Style.gapSm

            PmTextField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: "Type a command…"
                text: root.query
                onTextChanged: root.query = text
                Keys.onPressed: function (e) {
                    if (e.key === Qt.Key_Escape) {
                        root.close(); e.accepted = true
                    } else if (e.key === Qt.Key_Down) {
                        root.selectedIndex = root.nextSelectable(1); e.accepted = true
                    } else if (e.key === Qt.Key_Up) {
                        root.selectedIndex = root.nextSelectable(-1); e.accepted = true
                    } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        root.runSelected(); e.accepted = true
                    }
                }
            }

            ListView {
                id: results
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(320, contentHeight)
                clip: true
                model: root.filtered
                spacing: 2
                currentIndex: root.selectedIndex
                ScrollBar.vertical: PmScrollBar { }

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view ? ListView.view.width : 0
                    height: modelData.header ? 22 : Style.controlH

                    // Section header
                    Text {
                        visible: row.modelData.header === true
                        anchors.left: parent.left
                        anchors.leftMargin: Style.gapSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.section || ""
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                        font.bold: true
                        font.letterSpacing: Style.letterSpaceWide
                    }

                    // Action row
                    Rectangle {
                        visible: row.modelData.header !== true
                        anchors.fill: parent
                        radius: Style.radiusSm
                        color: row.index === root.selectedIndex
                               ? Style.tint(Style.accent, 0.18)
                               : (rowHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                        border.width: row.index === root.selectedIndex ? 1 : 0
                        border.color: Style.tint(Style.accent, 0.40)
                        HoverHandler { id: rowHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Style.gapSm
                            anchors.rightMargin: Style.gapSm
                            spacing: Style.gapSm
                            Text {
                                text: row.modelData.title || ""
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: row.modelData.section || ""
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.selectedIndex = row.index
                                root.runSelected()
                            }
                            onEntered: root.selectedIndex = row.index
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.filtered.length === 0
                    text: root.query.length > 0 ? "No matching commands" : "No actions registered"
                    color: Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsBody
                }
            }
        }
    }

    // Global Esc when focused
    Keys.onPressed: function (e) {
        if (e.key === Qt.Key_Escape) { root.close(); e.accepted = true }
    }
}
